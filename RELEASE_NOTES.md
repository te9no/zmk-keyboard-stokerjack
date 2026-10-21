# Firmware snapshot — 2026-10-21

ZMK OST版・SMD版、およびRMK OST60候補版のソースと既存ビルド済みファームを公開します。

- `Stokerjack_OST_gradient_60/58.uf2`：OST4ML5B32A砲弾型×4。
- `Stokerjack_60/58.uf2`：従来SMD LED×4。OST版へは使用しないでください。
- `stokerjack-rmk.bin/hex`：RMK OST60の実機未検証候補版。ST-Link移行用。
- ZMK用HEXは0x26000、RMK用HEXは0x1000開始です。

ZMK用UF2は導入済みのStokerjackブートローダーで使用します。
初回RMK移行とZMKへの復元はREADMEの手順に従ってください。
RMKのBIN/HEXをZMK用UF2ドライブへ直接コピーしても移行できません。

公開前にZMK配列・領域テスト4件、書き込みツール模擬試験12件、RMKロジック試験3件、
OSTプロトコルCテストを実行しました。既存ファームは再ビルドせず元ファイルを維持しています。
RMK実機確認、OST電気波形と消費電流の評価、公開用汎用ビルド手順のクリーン環境検証は未完了です。

`SHA256SUMS.json`はリポジトリ内dist相対パスで配布ファームの照合値を記載しています。
