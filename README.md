# IKAOPLL-verilator

YM2413 / IKAOPLL の RTL（`IKAOPLL.v` とモジュール群）を Verilator でラップし、
VGM 由来のテストパタンを C プログラムから実行できるようにしたリポジトリです。
Verilog を直接扱わずに OPLL の挙動を観察・実験できることを目標としています。

---

目次
- 概要
- ディレクトリ構成
- ビルドと実行
- VGM → CSV → .vh ツール
- オーディオ出力（CSV と WAV の扱い）
- ポストプロセスツール（使い方）
- よくある操作 / トラブルシュート
- ライセンス / 貢献

---

## 概要

このプロジェクトは次を目的とします。

- IKAOPLL（YM2413 相当）の RTL を Verilator を介して C/C++ から動かすこと。
- VGM 由来の制御列を再生して IKAOPLL の動作を検証すること。
- シミュレーションで得られる DAC / ACC 出力を保存し、再構成（ポストプロセス）を通じて人間が聞ける WAV を生成するワークフローを提供すること。

目的は「OPLL を Verilog なしで触れるようにすること（エミュレーションではなく、シミュレーション由来の生の出力を扱えること）」にあります。音色の最終的な再現性追求は二次的で、まずは「正しい生データの取得と再現可能な後処理パイプライン」を優先しています。

---

## ディレクトリ構成

- `rtl/`
  - `IKAOPLL.v`
  - `IKAOPLL_modules/IKAOPLL_*.v`  
    → 元プロジェクト（IKAOPLL）からコピーした RTL 一式
- `src/`
  - `ikaopll_wrapper.*`
    - Verilated `VIKAOPLL` の薄いラッパ（クロック操作、入出力の setter/getter、VCD トレース等）
    - EMUCLK の posedge/negedge の操作を含む
  - `ym2413_bus.h`, `ym2413_bus.c`
    - C 側のテストベンチ相当レイヤ。`phiM`/WAIT ロジックや ACC/MO のログ出力を担当
    - `audio_samples.csv`（定期サンプリング）や `mo_log.csv`（cluster/peak ログ）等の出力を行う
  - `vgm_player.*`
    - `*.vgm.csv`（`delay,reg,data`）を読み、VGM のタイムラインを再生する
  - `main_vgm_csv.c`
    - C 側のエントリポイント（`./ikaopll_sim [csv_path]`）
- `vgm_data/tests/`
  - `ym2413_scale_chromatic.vgm` / `.vgm.csv` / `.vh`
    - テスト用の VGM と変換結果
- `tools/`
  - Python スクリプト群：CSV→WAV 変換、ポストプロセスのスイープ、簡易 wrapper など

---

## ビルドと実行

前提: Verilator と一般的なビルドツールがインストールされていること（Linux/WSL 推奨）。

1. ビルドと実行（デフォルト: `vgm_data/tests/ym2413_scale_chromatic.vgm.csv` を使う）
```sh
./build_and_run.sh
```
- 成功すると `build/obj_dir/ikaopll_sim` が生成され、シミュレーションが実行されます。
- 実行後に `build/ikaopll_dump.vcd`（VCD）は生成され、`gtkwave` 等で波形確認できます。

2. 別の VGM CSV を使う場合：
```sh
./build/obj_dir/ikaopll_sim path/to/other.vgm.csv
```

注意:
- シミュレーションは VCD 出力や高サンプルレートの設定により時間がかかります。短いテストファイルでの検証を先に行うことを推奨します。

---

## VGM → CSV → .vh ツール

- `vgm_to_ym2413_csv.py`：`.vgm` から YM2413 関連コマンドだけを抜き出して `*.vgm.csv` を作成するツール（既存）。
- `vgm_csv_to_vh.py`：`*.vgm.csv` を Verilog 用の `.vh` に変換するツール（TB 用）。

このリポジトリは C 側で `*.vgm.csv` を直接読み、`ym2413_bus` 経由で IKAOPLL にコマンドを流します。

---

## オーディオ出力（CSV と WAV の扱い）

このプロジェクトは 2 種類の「オーディオ出力」を生成します。それぞれ目的が異なるため両方を残す運用にしています。

1. `audio_samples.csv`（canonical raw samples）
   - フォーマット: `t_ps,mo_signed,acc_signed`
   - シミュレーションから直接取得した「生の数値データ」です。サンプルレートは C 側で制御（デフォルト 44100 Hz）。
   - 用途：検証・再現性のための一次データ。後処理パラメータを変えて比較する際の基準として使います。

2. WAV（ポストプロセス済み）
   - `audio_samples.csv` を元に Python ツールでアナログ的な再構成（IIR / LPF / soft‑clip 等）を適用して WAV に変換します。
   - デフォルトの後処理（本リポジトリの推奨パラメータ）：
     - 指数応答（exp IIR）での再構成（tau = 1.5 ms）
     - mo_gain = 1.0, acc_gain = 0.30
     - 2-pole Butterworth ローパス（fc = 1500 Hz）
     - optional soft‑clip（tanh ベース、オプション）
     - 正規化（-1 dBFS）
   - 用途：人間による試聴、DAW（DTM）での加工、簡易確認。WAV は「この後処理を適用した IKAOPLL の出力近似」である点に注意してください（実機そのままではありません）。

理由（なぜ両方を残すのか）：
- エンジニアや検証者は数値（CSV）を使って厳密な比較を行いたい一方で、音としてすぐ確認したい場合は WAV が便利です。両方を残すことで「再現性」と「利便性」の両立を図ります。

---

## ポストプロセスツール（使い方）

必要な Python パッケージ（例）:
```sh
pip3 install numpy scipy soundfile
```

主要ツール：
- `tools/make_wav_from_audio_csv_expdecay.py`
  - CSV から WAV を生成するメインツール（デフォルトは上記の推奨パラメータ）。
  - 使い方（例）：
    ```sh
    python3 tools/make_wav_from_audio_csv_expdecay.py audio_samples.csv out.wav
    ```
  - オプションで `--tau_ms`, `--mo-gain`, `--acc-gain`, `--lpf-fc`, `--softclip`, `--no-normalize` などを指定できます。

- `tools/postprocess_sweep.py`
  - 複数のパラメータ組合せ（tau / acc_gain / lpf / softclip）を一括生成して比較するツール。
  - 出力フォルダに複数の WAV を書き出し、それぞれについて低域ピークの簡易解析を行います：
    ```sh
    python3 tools/postprocess_sweep.py audio_samples.csv post_out
    ```

- `tools/make_pretty_wav.sh`
  - ワンコマンドの wrapper。ビルド → シミュレーション → CSV 生成 → デフォルト後処理 → WAV 出力（`out_pretty.wav`）を実行します：
    ```sh
    ./tools/make_pretty_wav.sh out_pretty.wav
    ```

推奨ワークフロー（短期）
1. シミュレーションを実行して `audio_samples.csv` を得る：
   ```sh
   ./build_and_run.sh
   ```
2. デフォルト後処理で WAV を生成して試聴：
   ```sh
   ./tools/make_pretty_wav.sh out_pretty.wav
   ```
3. 必要に応じてパラメータを変えて比較（`postprocess_sweep.py` や `make_wav_from_audio_csv_expdecay.py` のオプションを使用）。

再現性の注意点
- WAV は「どの後処理を適用したか」に依存します。生成した WAV とそのパラメータ（tau, lpf, acc_gain, softclip）を README やコミットメッセージ、または別ファイルに残すことを推奨します。

---

## よくある操作 / トラブルシュート

- audio_samples.csv が見つからない / 空である:
  - シミュレーションが正しく終了しているか、`./build_and_run.sh` のログを確認してください。
  - VCD 出力をオフにして短いテストを回し、CSV が出るか試してください（VCD が I/O を圧迫することがあります）。

- WAV に高域ノイズが多い・音が鋭い:
  - デフォルト後処理ではインパルス列を滑らかにする処理（exp IIR + LPF）を行います。`--lpf-fc` を低め（1000Hz 等）に設定すると高域が抑えられます。
  - soft‑clip を試すことで音の柔らかさが増す場合があります。

- サンプルレートを上げたい:
  - C 側（`ym2413_bus.c`）の `AUDIO_SAMPLE_RATE` を 44100 → 96000 に変更して再ビルドすると高分解能 CSV が得られます。ただし CSV ファイルと後処理の処理時間・サイズが大きくなる点に注意してください。

---

## ライセンス / 貢献

- 本リポジトリのコードは（あなたが設定したライセンスに従ってください）。  
- 貢献：Issue / Pull Request を歓迎します。特に後処理のパラメータ最適化や C 側のサンプリング改善、DAW 用の変換スクリプトなどは有用です。



