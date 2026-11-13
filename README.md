# IKAOPLL-verilator

OPLL/YM2413のVGMを読み込み、Verilator上でIKAOPLLのRTLをシミュレーションして、結果をWAVとして取得します。音を生成するのはRTLです。C/C++側はVGMの解読、クロックとレジスタ書き込み、PCM取得、WAVへの変換を担当します。

## 準備と実行

WSL上のBash、Verilator、make、C/C++コンパイラ、zlib開発ライブラリが必要です。回帰テストにはPython 3を使います（追加Pythonパッケージは不要）。Verilator 4.038でビルド・動作を確認しています。

環境の準備は`setup_env.sh`で一括して行えます。必要なツールの確認（不足分はaptで導入）、RTLの取得、シミュレータのビルド、同梱フィクスチャでのスモークレンダリングまでを行います。再実行しても既にあるものはそのまま残ります。

```bash
./setup_env.sh
```

手動でRTLだけを用意する場合は、IKAOPLLのソースとライセンスを`rtl/`に配置してください。既に配置されているものを上書きする必要はありません。新規取得の場合は、上流ソースを確認したうえで次を使用できます。

```bash
bash tools/create_rtl_dir.sh --fetch-raw https://github.com/ika-musume/IKAOPLL/blob/main
```

リポジトリのルートから実行します。

```bash
bash build_and_run.sh tests/vgm/ym2413_scale_rom1.vgm
```

`ym2413_scale_rom1.wav`が生成されます。CSVや巨大な波形トレースは通常出力しません。出力先やレートは指定できます。出力先のディレクトリは先に作成してください。

```bash
mkdir -p outputs
bash build_and_run.sh tests/vgm/ym2413_scale_rom1.vgm --output outputs/scale.wav --sample-rate 48000
```

既にビルド済みなら、実行ファイルを直接使えます。

```bash
build/obj_dir/ikaopll_sim input.vgm --output outputs/result.wav
```

### 一括変換

`export_vgm.py`で、ファイルまたはディレクトリ単位でまとめてWAVへ変換できます。入力は`.vgm`/`.vgz`で、ディレクトリは直下だけを見ます。各入力は`<stem>.wav`として`--outdir`へ出力され、結果一覧が`results.csv`に残ります。既にWAVがある場合はスキップし、`--force`で上書きします。シミュレータが未ビルドなら自動でビルドします（RTLが無い場合は`./setup_env.sh`を先に実行してください）。

通常の出力はWAVと`results.csv`のみです。診断用のCSVやバス記録は、解析用のオプションを指定した場合だけ生成されます。

```bash
python3 export_vgm.py input.vgm --outdir outputs
python3 export_vgm.py /path/to/vgm_dir --outdir outputs --sample-rate 48000
```

入力、WAV、診断ファイルには異なるパスが必要です。既存の出力ファイルは上書きします。入力と同じファイルを指す別名・シンボリックリンク等は拒否します。

## 音声と時間の扱い

- VGMの待ち時間は1/44,100秒単位です。
- マスタークロックはVGMヘッダーのYM2413クロック値を使用します。通常は3,579,545 Hzです。
- `i_phiM_PCEN_n=0`で全マスタークロックを有効にします。`ACC_SIGNED`の更新は72クロックごと（通常約49,715.903 Hz）です。
- ストローブの立ち上がりごとに符号付き16-bitの加算済みPCMを取得し、整数時刻に基づく線形補間で44,100 Hz（既定）または48,000 Hzへ変換します。
- MOへの自動切り替え、ピーク正規化、音色補正は行いません。
- リセット時間を再生から除外し、VGM末尾の待ち時間を保持します。終端はマスタークロック境界へ切り上げます。

線形補間はPCMの変換方法です。実機のアナログ出力やSPDIF録音との完全一致を保証するものではありません。

## 入力のVGMについて

入力はVGMファイルのみです（CSV形式のイベント入力は廃止しました）。VGM 1.00–1.71の単一YM2413、最初の1周を対象とします。非圧縮ファイルとgzip圧縮のVGM/VGZを内容から判別します（圧縮済みの`.vgm`も可）。ループ反復、デュアルYM2413、VRC7指定は未対応です。他チップの既知コマンドは安全に読み飛ばしますが、その音声は出力しません。未知・未対応のコマンドや壊れたデータはエラーとして停止します。すべてのVGM命令への対応を意味しません。

## ライセンス

Copyright (c) 2025 emef2247.

Licensed under the [BSD 2-Clause License](LICENSE).

本プロジェクトで利用または参照している第三者コンポーネントのライセンスおよび著作権表示については、[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)を参照してください。IKAOPLLのRTL本体は本リポジトリに含まず、各自で取得する形です（取得時に上流のライセンス文が`rtl/IKAOPLL_LICENSE`へ保存されます）。

## 謝辞

本プロジェクトの開発、検証、およびテスト生成では、以下のツールやプロジェクトを利用または参考にしています。開発者・メンテナー・貢献者の皆様に感謝します。

とりわけ、YM2413（OPLL）のRTL再実装であるIKAOPLLを公開してくださったika-musumeさんに感謝します。本プロジェクトはIKAOPLLのRTLそのものを音源として鳴らすもので、その公開がなければ成り立ちませんでした。

また、IKAOPLLの動作サンプルを提供してくださり、折に触れて相談にも乗っていただいている[ひろゆきさん](https://d4.princess.ne.jp/)にも感謝します。サンプルが明示していたパラメータ構成が、RTLを正しく鳴らすための土台になりました。

| プロジェクト | 用途 |
| --- | --- |
| [IKAOPLL](https://github.com/ika-musume/IKAOPLL) | 音源となるYM2413のRTL本体（ika-musumeさん） |
| [Verilator](https://www.veripool.org/verilator/) | RTLシミュレーション |
| [EMU2413／libvgm](https://github.com/ValleyBell/libvgm) | 参照WAVとの比較検証（任意。レンダラ本体ではありません） |

使用した第三者コンポーネントのバージョン、参照元、著作権表示、ライセンス条件、および再配布時の注意事項は[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)に記録しています。

