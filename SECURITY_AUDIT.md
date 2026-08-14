# アーカイブ前セキュリティ調査

調査日: 2026-08-15

本書は、Rust 版への移行に伴い本リポジトリをアーカイブする前に実施した、旧 C++ 実装の静的調査結果です。

## 結論

- Critical に該当する問題は確認されませんでした。
- `main` に High 1件、その他のブランチに High 2種を確認しました。
- 秘密鍵、API キー、トークンなどの機密情報は確認されませんでした。
- 本実装のバイナリ配布および新規利用は停止し、Rust 版を利用してください。

## `main`

調査対象: `9785a4fa48afc97f5a89d1a9a37164a6aee2898c`

### High: 回転モニターでの DXGI バッファ境界外読み取り

場所: `src/capture_dxgi.cpp:102-142`

90度または270度回転したディスプレイでは、Desktop Duplication API が返す未回転 surface の寸法と、GDI のモニター矩形の寸法が入れ替わる場合があります。実装はコピー量をモニター矩形から算出しているため、マップされた surface の行数を超えて読み取る可能性があります。

影響:

- プロセスのクラッシュ
- PNG への隣接データ混入
- ドライバー実装に依存する不定動作

対策案:

- `desc.Width`、`desc.Height`、`map.RowPitch` に基づいてコピー範囲を検証する
- `IDXGIOutputDuplication::GetDesc()` の回転値に従って画像を回転する

### Medium: 出力ファイル存在確認の TOCTOU

場所: `src/encode_wic_png.cpp:11-18,53`

`GetFileAttributesW()` による存在確認と WIC によるファイルオープンが別操作です。特権プロセスが第三者の操作可能なディレクトリへ出力する場合、確認後にリンクや reparse point を差し替えられる可能性があります。

対策案:

- `CreateFileW(..., CREATE_NEW, ...)` で原子的に作成する
- 作成したハンドルを `IStream` として WIC へ渡す
- 必要に応じて reparse point を拒否する

### Medium: タイムアウトと再試行回数の範囲制限不足

場所: `src/cli.cpp:20-26,197-209`、`src/capture_dxgi.cpp:85`、`src/capture_wgc.cpp:224`、`src/main.cpp:294`

負のタイムアウトが `DWORD` / `UINT` に変換されると約49日間の待機になり得ます。また、極端に大きな再試行回数は CPU / GPU DoS やループ変数の符号付きオーバーフローにつながります。

対策案:

- 厳密な整数解析を行う
- `timeout-ms` と `retry` に現実的な上限・下限を設ける

### Low: ログへの情報記録とログインジェクション

場所: `src/main.cpp:231-241,431-439,474-475`

全引数とウィンドウタイトルが無加工で記録されます。ファイルパスや文書名の漏洩、改行を含む値によるログ偽装が起こり得ます。

対策案:

- 制御文字をエスケープした構造化ログを使用する
- 記録する引数を許可リスト方式にする
- ログディレクトリの ACL を実行ユーザーに限定する

## その他のブランチ

### High: クリップボード DIB 処理の整数オーバーフロー

対象: `feat/hotkey`、`origin/feat/hotkey`

場所: `src/main.cpp:515-551`

検証には `SIZE_T` を使用していますが、実際の確保量やコピー量を符号付き `int` で再計算しています。細工した巨大な `CF_DIB` / `CF_DIBV5` により過小確保後の `memcpy` が発生し、ヒープ破壊につながる可能性があります。

### High: WGC フレーム受け渡しのデータ競合

対象: `origin/experiment/wgc-content-size`、`experiment/window-picker-gui`

場所: `src/capture_wgc.cpp:267-320`

`FrameArrived` コールバックと呼び出し元が、WinRT フレームを同期なしで読み書き・解放します。タイミングによって COM 参照カウントの競合、use-after-free、クラッシュまたはメモリ破壊が起こり得ます。

### Medium: 最新 WGC 実装に残る終了時の競合

対象: `origin/experiment/window-picker-gui`

場所: `src/capture/capture_wgc.cpp:245-253,300-303`

mutex 導入後も `captured = nullptr` がロック外で実行されます。既に実行中のコールバックと終了処理が競合する可能性があります。

### Medium: クリップボード撮影結果の差し替え

対象: `feat/hotkey`、`origin/feat/hotkey`

場所: `src/main.cpp:558-579`

`ms-screenclip:` 起動後、発行元を確認せず次に更新されたクリップボード DIB を撮影結果として受理します。別プロセスが先に DIB を設定すると、攻撃者指定画像を正規の撮影結果として保存できます。

### Low: Windows コマンドライン引数の引用不備

対象: `experiment/window-picker-gui`

場所: `src/gui.cpp:173-183,246-250`

引用符直前のバックスラッシュを正しく処理しません。これは最新の `origin/experiment/window-picker-gui` では修正済みです。

## 調査範囲と制限

- `main` の全追跡ファイル
- `main` 以外のローカルおよび remote-tracking ブランチ
- Git 履歴内の代表的な機密情報パターン
- 外部入力、パス操作、コマンド実行、権限、メモリ安全性、並行処理

本リポジトリに第三者パッケージのマニフェストはなく、依存先は主に Windows SDK のシステムライブラリです。Linux 環境で調査したため、Windows 実機でのビルド、ASan、回転モニター、reparse point 競合、WGC コールバック競合の動的検証は実施していません。

## アーカイブ時の推奨事項

1. 本実装が非推奨であることと Rust 版の移行先を README に明記する
2. 旧バイナリの配布を停止する
3. 問題を含む未マージブランチを不要であれば削除する
4. 旧版を利用可能な状態で残す場合は、少なくとも DXGI の境界外読み取りを修正する
