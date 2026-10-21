# Stokerjack ZMK / status-led

60キー・58キー用。ソースは同梱バイナリに付属するsource.zipから抽出しています。
テストを現在の0x26000開始のパーティションに合わせ、個人環境に依存するビルド手順を整理しました。
キー配列・LED処理・ボード定義は元ソースを保持しています。

対象LEDと書き込み方法は[リポジトリの説明](../../README.md)を参照してください。
ビルドは初期化済みZMKワークスペースで `ZMK_WORKSPACE=/path/to/workspace bash build.sh 60`。
58キーは最後の引数を58にします。固定するZMKのコミットは `config/west.yml` に記載。
単体テストは `python -m unittest discover -s tests -v`。
