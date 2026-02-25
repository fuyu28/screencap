# screencap

Windows 10/11 向けのスクリーンショット方式比較CLIです。`WGC / DXGI Desktop Duplication / GDI` を単一EXE内で切り替えて実行できます。

## Build

要件:

- Visual Studio 2022+
- Windows SDK
- CMake 3.20+

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

生成物:

- `build/Release/screencap.exe`

## コマンド

```powershell
screencap list windows [--json]
screencap list monitors [--json]

screencap cap --method <name> --target window|screen --out <path> [options]
```

### method

- WGC
  - `wgc-window`
  - `wgc-monitor`
- DXGI
  - `dxgi-monitor`
  - `dxgi-window`
- GDI
  - `gdi-printwindow`
  - `gdi-bitblt-client`
  - `gdi-bitblt-windowdc`
  - `gdi-bitblt-screen`

### 例

```powershell
screencap list windows --json
screencap list monitors --json

screencap cap --method dxgi-monitor --target screen --monitor primary --out a.png --json
screencap cap --method gdi-printwindow --target window --foreground --out b.png --json
screencap cap --method dxgi-window --target window --pid 15796 --crop client --out c.png --force-alpha 255 --json
```

## ログ

- 毎回新規ログを作成
- 既定: `./logs`
- ファイル名: `YYYYMMDD_HHMMSS_mmm_<pid>_<command>.log`

主な記録内容:

- バージョン、ビルド日時、OS、DPIモード
- 全引数
- 解決ターゲット情報（window/monitor）
- 方式別診断（HRESULT / Win32エラーなど）
- `black_ratio` / `transparent_ratio` / `avg_luma`
- 成功/失敗、処理時間

## 制限

- `--stdout` は未対応（将来用）
- `--format` は `png` のみ対応
- 方式の自動フォールバック（`auto`）は未実装
- 失敗時の他方式再試行は未実装（`--retry` は同方式のみ）
