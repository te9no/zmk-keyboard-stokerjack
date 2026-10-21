# Stokerjack RMK / OST60

OST4ML5B32A×4・60キー用の実機未検証候補版。RMK 0.9.0の依存コミットを固定しています。
ZMKからの初回移行はSWD用ツールを使用。ZMK用UF2と配置が異なります。

Linux/WSLでRust 1.94.1、clang/libclang、ARM GCC、liblzma開発パッケージを用意し、次を実行します。

```sh
rustup target add thumbv7em-none-eabihf
export CC_thumbv7em_none_eabihf=arm-none-eabi-gcc
export AR_thumbv7em_none_eabihf=arm-none-eabi-ar
cargo build --release --locked
rustc --edition=2024 --test src/animation.rs -o /tmp/stokerjack-animation-test
/tmp/stokerjack-animation-test
```

`Dockerfile`はOS側依存パッケージの定義です。Rustは別途必要です。
アプリ0x1000–0xEBFFF、保存領域0xEC000–0xF3FFF。既存ブートローダーは0xF4000から。
4レイヤー、USB/BLE、5プロファイル、NiMH2本の残量換算、約2.4秒の4灯グラデーション。
Vialの編集ロック解除キーはEsc＋1。旧BT全消去キーはUSB/BLE切替。
VID/PID 1209:5354はローカル試験用識別値です。製品用の割り当てを受けたIDではありません。
実機での書き込み・Bluetooth・LED波形・電池計測・消費電流の確認はまだです。
