# screencap

Windows 10/11 向けのスクリーンショット取得 CLI です。  
`WGC / DXGI Desktop Duplication / GDI` を 1 つの EXE で切り替えて実行できます。

## ビルド

要件:

- Visual Studio 2022 以降
- Windows SDK
- CMake 3.20 以降

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

生成物:

- `build/Release/screencap.exe`

## 使い方（クイックスタート）

1. 取得対象を調べる（ウィンドウ/モニター一覧）
2. `cap` で方式・対象を指定して PNG を保存
3. 失敗時はログと `--json` 出力を確認

```powershell
screencap list windows --json
screencap list monitors --json

screencap cap --method dxgi-monitor --target screen --monitor primary --out a.png --json
```

## コマンド一覧

```powershell
screencap help
screencap list windows [--json] [共通オプション]
screencap list monitors [--json] [共通オプション]
screencap cap --method <method> --target <window|screen> --out <path> [オプション]
```

## `cap` の必須オプション

- `--method <name>`
- `--target window|screen`
- `--out <path>`

対象別に追加で必須条件があります:

- `--target window` の場合  
  `--hwnd` / `--pid` / `--foreground` / `--title` / `--class` のいずれか 1 つ以上が必須
- `--target screen` の場合  
  `--monitor <index|primary>` または `--virtual-screen` が必須

## キャプチャ方式（`--method`）

- WGC
  - `wgc-window`
  - `wgc-monitor`
- DXGI
  - `dxgi-window`
  - `dxgi-monitor`
- GDI
  - `gdi-printwindow`
  - `gdi-bitblt-client`
  - `gdi-bitblt-windowdc`
  - `gdi-bitblt-screen`

## オプション詳細

### 共通オプション（`list` / `cap`）

- `--json`  
  JSON 形式で結果を出力
- `--log-dir <path>`  
  ログ出力先（既定: `./logs`）
- `--log-level <trace|debug|info|warn|error>`  
  ログレベル
- `--timeout-ms <ms>`  
  タイムアウト（既定: `700`）
- `--retry <count>`  
  同一方式での再試行回数（既定: `0`）
- `--overwrite`  
  出力ファイルの上書きを許可
- `--dpi-mode <auto|per-monitor-v2|system>`  
  DPI モード（既定: `per-monitor-v2`）

### `cap` 専用オプション

- ターゲット指定
  - `--hwnd <u64>`
  - `--pid <int>`
  - `--foreground`
  - `--title <text>`
  - `--class <text>`
  - `--monitor <index|primary>`
  - `--virtual-screen`
- 切り抜き
  - `--crop <none|window|client|dwm-frame|manual>`
  - `--crop-rect <x> <y> <w> <h>` (`--crop manual` 時に必須)
  - `--pad <l> <t> <r> <b>`
- 出力
  - `--format png`（現状 `png` のみ）
  - `--force-alpha 255`（255 のみ指定可）
- ホットキー
  - `--hotkey <combo>` 例: `ctrl+shift+s`, `alt+f9`
  - `--hotkey-foreground`  
    ホットキー押下時点の最前面ウィンドウを対象にする（`--target window` 必須）

## 実用例

### 1. 前面ウィンドウを GDI で保存

```powershell
screencap cap --method gdi-printwindow --target window --foreground --out fg.png --json
```

### 2. プライマリモニターを DXGI で保存

```powershell
screencap cap --method dxgi-monitor --target screen --monitor primary --out mon.png --json
```

### 3. 特定 PID のウィンドウをクライアント領域で切り抜く

```powershell
screencap cap --method dxgi-window --target window --pid 15796 --crop client --out client.png --json
```

### 4. 手動切り抜き

```powershell
screencap cap --method gdi-bitblt-screen --target screen --monitor primary --crop manual --crop-rect 100 100 800 600 --out crop.png --json
```

### 5. ホットキーで前面ウィンドウを 1 回キャプチャ

```powershell
screencap cap --method dxgi-window --target window --hotkey ctrl+shift+s --hotkey-foreground --out hotkey.png --overwrite --json
```

起動後は待機状態になり、ホットキー押下で 1 回だけ撮影して終了します。  
`--method dxgi-window` なら、フルスクリーン表示のウィンドウでも同じ経路で扱えます。

## エラー時の確認ポイント

- `cap needs --method` などのメッセージ  
  必須オプション不足
- `window target needs one of ...`  
  ウィンドウ指定条件不足
- `screen target needs --monitor or --virtual-screen`  
  スクリーン指定条件不足
- `manual crop needs --crop-rect`  
  手動切り抜きのパラメータ不足
- `only --format png is supported`  
  非対応フォーマット指定

## ログ

- 毎回新規作成
- 既定: `./logs`
- ファイル名: `YYYYMMDD_HHMMSS_mmm_<pid>_<command>.log`

主な記録内容:

- バージョン / ビルド日時 / OS / DPI モード
- 実行引数
- 解決した window/monitor 情報
- 方式別診断（HRESULT / Win32 エラー）
- 画像統計値（`black_ratio` / `transparent_ratio` / `avg_luma`）
- 成功/失敗と処理時間

## 現在の制限

- `--stdout` は未対応
- `--format` は `png` のみ
- 方式の自動フォールバック（`auto`）未実装
- 他方式への自動切り替え再試行は未実装（`--retry` は同方式のみ）
