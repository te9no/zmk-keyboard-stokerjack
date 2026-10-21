# zmk-keyboard-stokerjack

Stokerjack（MDBT50Q-1MV2 / nRF52840）のファームウェアとソースコード。
60キー・全1u／58キー・2uに対応します。

## ファームウェアの選択

| 版 | コード | 書き込みファイル | 状況 |
|---|---|---|---|
| ZMK / OST4ML5B32A砲弾型×4 | [OSTソース](firmware/ost4ml5b32a) | [60・58キー用HEX/UF2](dist/ost4ml5b32a) | ビルド・プロトコル試験済み。OST版全体の実機検証は未完了 |
| ZMK / 従来SMD LED×4 | [SMDソース](firmware/status-led) | [60・58キー用HEX/UF2](dist/status-led) | 60キーの書き込み・USB認識とLED動作の確認履歴あり。58キーはビルド確認 |
| RMK / OST4ML5B32A砲弾型×4 | [RMKソース](firmware/rmk-stokerjack) | [60キー候補版BIN/HEX](dist/rmk-ost60) | ビルド・ホスト試験済み、実機未検証 |

**OST版とSMD版は互換ではありません。** 基板のLED種別と60／58キーを照合してください。
OST版はP0.27のDINに100Ω、DIN–LED_VDDとDIN–GNDに各10kΩの抵抗を使う構成です。
OSTの3.3V駆動は部品の保証電圧外の試験構成です。

## ZMKの書き込み

Stokerjack用UF2ブートローダー導入済みの基板が対象です。
RESETをすばやく2回押し、`STOKERJACK`ドライブへ対応するUF2をコピーします。
HEXはアプリのみ（開始0x26000）。アドレス0へずらしたり、全消去して書き込まないでください。
ブートローダー初回導入用のバイナリはこのリポジトリに含めていません。

LEDはFn＋LEDキー、Caps Lock、レイヤーや接続状態の変更で約2.4秒表示します。
通常のキー入力では最大30秒に1回。消灯状態でもキーボードは動作します。
NiMH単3×2本の電圧から残量を推定します。充電機能はありません。

## ZMKのビルド

ZMK/Zephyrの開発環境（west、Zephyr SDK、Python依存パッケージ）を用意します。
既存バイナリはZephyr SDK 0.16.9でビルドしたものです。

```sh
west init -m https://github.com/cormoran/zmk --mr e5c9b6915b56801193e359dd9bad4a167ce0d1b8 --mf app/west.yml zmk-workspace
cd zmk-workspace
west update
west zephyr-export
export ZMK_WORKSPACE="$PWD"
bash /path/to/zmk-keyboard-stokerjack/firmware/ost4ml5b32a/build.sh 60
```

SMD版は`status-led/build.sh`を使用。58キーは引数を58にします。
この公開用の汎用ビルド手順は、クリーンな環境でのフルビルド再検証は未実施です。

## RMK候補版と自動書き込み

初回ZMK→RMK移行はST-LinkによるSWD書き込み用です。通常のCDC DFUへそのまま渡せません。
RMKは0x1000開始で、既存SoftDeviceの領域を使用します。ペアリングとキー設定は初期化されます。
Python 3.12、hidapi、OpenOCD 0.12を用意し、リポジトリ直下から実行します。

```sh
python -m pip install hidapi==0.15.0
python tools/rmk-flasher/stokerjack_flash.py flash --migrate --package dist/rmk-ost60 --openocd /path/to/openocd --scripts /path/to/openocd/scripts
```

WindowsでST-Linkと基板USBを接続して使用するツールです。
書き込み前に全フラッシュとUICRを保存し、既知ブートローダー・MCUを照合します。
書き込み後は読み戻し、動作カウンターとチャレンジ応答、USB Vial識別を確認します。
OpenOCD本体・ドライバーは含みません。`verify`で再確認、`restore --backup <保存フォルダー>`で復元できます。
`--package`・`--openocd`・`--scripts`は再確認時も指定してください。
バックアップはWindowsの`%LOCALAPPDATA%/Stokerjack/RMK/日時`へ保存され、ペアリング情報を含むので公開しないでください。
ZMKへ戻すには全バックアップを復元します。旧UF2単体ではSoftDeviceは戻りません。

## 検証・ライセンス

`SHA256SUMS.json`に配布ファームのSHA-256を記録しています。
ホスト試験はハードウェア動作の保証ではありません。実機未検証の版を製品版として扱わないでください。
RMK由来のライセンスは`firmware/rmk-stokerjack/LICENSE-*`、バイナリ依存物の通知は`licenses/`を参照。
上流は[ZMK](https://github.com/cormoran/zmk)と[RMK](https://github.com/rmk-rs/rmk)です。
元プロジェクト全体への包括的なライセンス付与は、この公開では行っていません。
