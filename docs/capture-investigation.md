# Capture investigation

2026-07-08 に Snipping Tool 依存を外すため、既存の `WGC` / `DXGI` / `GDI`
経路を同じ環境で検証した。

## 結論

- 第一候補は `wgc-window` / `wgc-monitor`。
- `gdi-bitblt-screen` は比較用・フォールバックとして有効。
- `dxgi-monitor` は API 呼び出しとしては成功したが、この検証環境では RGB が全黒だった。
- Snipping Tool 経由の `ms-screenclip:` は引き続き外部依存なので、本線にはしない。

## 原因

### PNG 保存後の異常終了

`SavePngWic` で `CoUninitialize()` が WIC の `ComPtr` 破棄より先に実行されていた。
そのため PNG は作られていても、プロセス終了時に異常終了し、JSON が出ないことがあった。

修正:

- `CoInitGuard` を追加し、関数スコープを抜ける最後に `CoUninitialize()` する。
- WIC の COM オブジェクト破棄後に COM を uninitialize する。

### WGC 終了時の異常終了

`Direct3D11CaptureFramePool` の `FrameArrived` handler と最後に取得した frame を保持したまま
`session.Close()` / `frame_pool.Close()` していた。

修正:

- `revoker.revoke()` で handler を解除する。
- `captured = nullptr` で frame を明示的に解放する。
- その後に `session.Close()`、`frame_pool.Close()` の順で閉じる。

### WGC のサイズ・透明混入

以前は `Direct3D11CaptureFrame` の texture 全体をコピーしていた。
WGC の実コンテンツは `frame.ContentSize()` で示されるため、pool texture の余りを含めると
サイズ不一致や未定義領域の混入につながる。

修正:

- `frame.ContentSize()` を確認する。
- `CopySubresourceRegion` で有効領域だけ staging texture にコピーする。
- 最大 5 フレームまで待ち、黒率・透明率が極端に高い初期フレームを避ける。

### DXGI の全黒

`dxgi-monitor` は `DuplicateOutput`、`AcquireNextFrame`、PNG 保存までは成功した。
ただし検証環境では `black_ratio=1`、`avg_luma=0` で、RGB が全黒だった。
`--force-alpha 255` により透明率は 0 になるため、alpha だけの問題ではない。

この段階では DXGI を主経路にしない。

## 検証結果

検証環境:

- OS: Windows 10.0 build 26200
- DPI mode: `per-monitor-v2`
- Primary monitor: `1920x1080`

代表コマンド:

```powershell
.\build\Release\screencap.exe cap --method wgc-monitor --target screen --monitor primary --out wgc-monitor.png --overwrite --json --timeout-ms 2000 --log-level debug --force-alpha 255
.\build\Release\screencap.exe cap --method wgc-window --target window --foreground --out wgc-window.png --overwrite --json --timeout-ms 2000 --log-level debug --force-alpha 255
.\build\Release\screencap.exe cap --method gdi-bitblt-screen --target screen --monitor primary --out gdi-screen.png --overwrite --json --timeout-ms 2000 --log-level debug --force-alpha 255
.\build\Release\screencap.exe cap --method dxgi-monitor --target screen --monitor primary --out dxgi-monitor.png --overwrite --json --timeout-ms 2000 --log-level debug --force-alpha 255
```

結果:

| Method | Result | Notes |
| --- | --- | --- |
| `wgc-monitor` | OK | `1920x1080`, 黒/透明ではない |
| `wgc-window` | OK | 前面 Explorer を正常保存 |
| `gdi-bitblt-screen` | OK | 比較用として正常保存 |
| `dxgi-monitor` | NG | 正常終了するが RGB が全黒 |

`wgc-window2` / `wgc-monitor2` は実験時のエイリアスとして残しているが、
現在は通常の `wgc-window` / `wgc-monitor` と同じ WGC 改善経路を通る。

## Hotkey capture

以前のホットキー経路は、キー押下後に `ms-screenclip:` を起動して Snipping Tool の
クリップボード画像を保存していた。現在は Snipping Tool を使わず、キー押下後に通常の
`cap` と同じキャプチャ経路を実行する。
ホットキー待機中にコンソールを表示したくない場合は、console subsystem の `screencap.exe`
ではなく、window subsystem の `screencapw.exe` を使う。

例:

```powershell
.\build\Release\screencap.exe cap --method wgc-window --target window --foreground --hotkey ctrl+shift+s --out hotkey-window.png --overwrite --json
.\build\Release\screencap.exe cap --method wgc-monitor --target screen --monitor primary --hotkey alt+f9 --out hotkey-monitor.png --overwrite --json
.\build\Release\screencap.exe cap --method gdi-bitblt-screen --target screen --monitor primary --hotkey alt+f10 --out hotkey-gdi.png --overwrite --json --force-alpha 255
```

No-console example:

```powershell
.\build\Release\screencapw.exe cap --method wgc-window --target window --foreground --hotkey ctrl+shift+s --out hotkey-window.png --overwrite --log-level debug
```

`--target window` では `--foreground` / `--hwnd` / `--pid` / `--title` / `--class` のいずれかが必要。
`--foreground` を指定した場合、対象は起動時ではなくホットキー押下後に解決される。

## 次に試すこと

- ゲーム画面で `wgc-monitor`、`wgc-window`、`gdi-bitblt-screen` を比較する。
- ゲームが排他的フルスクリーンの場合は、まず borderless/windowed と exclusive fullscreen を分けて記録する。
