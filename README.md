# IKAOPLL-verilator

## 環境構築（rtl ディレクトリの作成）

本プロジェクトでは IKAOPLL の RTL ソースを `rtl/` 配下に置いて使います。手元にソースが無い場合は付属スクリプトで upstream から取得できます。fetch モードでは GitHub の `blob` ベース URL を与えてください（スクリプトが raw.githubusercontent 用 URL に変換します）。

例：
```bash
./tools/create_rtl_dir.sh --fetch-raw https://github.com/ika-musume/IKAOPLL/tree/main
```

---

## 実行方法

ビルドと実行用スクリプト: `./build_and_run.sh`  
（スクリプトは Verilator によるビルドとシミュレーションの起動を行います）

基本的な使い方
- CSV 入力の場合（CSV は後述の方法で作成できます）
```bash
./build_and_run.sh tests/csv/ym2413_scale_rom1.vgm.csv
```

- VGM 入力ファイルの場合
```bash
./build_and_run.sh tests/vgm/ym2413_scale_rom1.vgm
```

よく使うオプション
- `--vcd <file>` : VCD を出力します（デフォルトファイル名は `ikaopll_dump.vcd`）。全信号ダンプは大きなサイズになります（full dump に注意）。
- `--fst <file>` : FST 形式のトレース出力を行います（Verilator の FST トレースを利用する場合）。
- `--enable-csv` : CSV ログ（MO/ACC 等）を有効にします。
- `--no-csv` : CSV ログを無効にします（デフォルトは CSV 無効になっています）。

デフォルトについて
- スクリプトは「デフォルトで CSV ログを出力しない」設定になっています。CSV を取りたい場合は実行時に `--enable-csv` を指定してください。

CSV / VCD の注意
- VCD の出力はファイルサイズが非常に大きくなることがあります（full dump）。ディスク容量に注意してください。
- CSV ログ（特に EMUCLK 毎の高頻度ログ）は多数の行を吐くため、必要な区間だけログする運用を推奨します。

MO（DAC）ログについて
- `--enable-csv` を付けて実行すると、シミュレータは MO に関する差分ログを出力します（ファイル名: `mo_value_changes.csv`）。
- `mo_value_changes.csv` のフォーマット（列）
  - t_ps : タイムスタンプ（ピコ秒）
  - mo_signed : DAC 出力（signed）の変化後の値
  - （実装により他の列を含める場合があります。必要に応じてヘッダを確認してください）
- `mo_value_changes.csv` は value-change（変化のみ）ログなので、そのままでは等間隔サンプリングになっていません。音声化する際はデシメーション（重み付き平均や ZOH）などの処理が必要です。

---

## 実行後の WAV 生成方法（ログ → WAV）

いくつかツールを用意しています。標準的な流れと例を示します。

1) `audio_samples.csv`（シミュレータ側でサンプリングして出力した ACC 等）から WAV を作る（デフォルト）
```bash
python3 ./tools/csv_to_wav.py -i audio_samples.csv -o out_acc.wav --col-name acc_signed --scale 1
```
- `--col-name` で CSV 内のカラムを指定します（例: `acc_signed`）。
- `--scale` は値のスケーリング係数です（実機や用途に合わせ調整）。

2) DAC の差分ログ (`mo_value_changes.csv`) を使って WAV を作る（より忠実にしたい場合）
- このログは値変化のみを記録しているため、重み付き平均などでデシメーションするのが妥当です（短いパルスの寄与を正しく反映するため）。
- スクリプト例（重み付け平均デシメーション）:
```bash
python3 ./tools/mo_changes_to_wav_weighted.py -i mo_value_changes.csv -o mo_avg.wav
```
- 使い方の主なオプション:
  - `--sr` : サンプリング周波数（デフォルト 44100）
  - `--scale` : 値のスケーリング（例: 64）
  - `--start-ps` / `--end-ps` : 取り出す時間範囲（ps 単位）

3) 単純な ZOH（直前の値を保持）でリサンプリングする場合
```bash
python3 ./tools/mo_changes_to_wav.py -i mo_value_changes.csv -o mo.wav --samplerate 44100 --scale 64
```

補足
- `mo_changes_to_wav_weighted.py` は、各オーディオサンプル区間に対して時間で重み付けした平均を計算することで、短いパルスのエネルギーを正しく集約する方式です。value-change だけのログから基音成分を取り出す際に有効です。
- それでも期待する音程が見えない場合は、より高密度なログ（EMUCLK 毎）を取るか、シミュレータ側で追加ログを行うことを検討してください（ただしデータ量は大幅に増えます）。

---

## 例：フルワークフロー

1. RTL の準備
```bash
./tools/create_rtl_dir.sh --fetch-raw https://github.com/ika-musume/IKAOPLL/tree/main
```

2. ビルド＋シミュレーション（CSV 生成なし；VCD なし）
```bash
./build_and_run.sh tests/csv/ym2413_scale_rom1.vgm.csv
```

3. CSV（MO の変化ログ）を取りたい場合（`--enable-csv` を付ける）
```bash
./build_and_run.sh tests/csv/ym2413_scale_rom1.vgm.csv --enable-csv
# これにより mo_value_changes.csv 等が生成されます（出力先は実行ディレクトリ）
```

4. mo_value_changes.csv を WAV に変換（重み付け平均）
```bash
python3 ./tools/mo_changes_to_wav_weighted.py -i mo_value_changes.csv -o mo_avg.wav
```
---

##出力ファイル名・生成されるWavについて

このプロジェクトでは、実行時に指定した入力ファイル名（パス）の「末尾の拡張子を一つだけ取り除いた文字列」を出力ファイルのベース名として使用します。生成される主なファイルは以下の通りです。

- `<inputbase>.csv`  
  - 内容: IKAOPLL のサンプリング結果（`t_ps,mo_signed,acc_signed` 等の列）  
  - 例: 入力 `tests/csv/ym2413_scale_chromatic.vgm.csv` を指定した場合、出力 CSV は `ym2413_scale_chromatic.vgm.csv` になります（末尾の拡張子 `.csv` を一つ剥いたベースは `ym2413_scale_chromatic.vgm` であるため、さらに `.csv` を付けます）。  
  - 備考: この挙動は「最後の拡張子のみを剥く」仕様です（`foo.vgm.csv` の場合はベースが `foo.vgm` になります）。必要であれば追加の拡張子ルール（例: `.vgm.csv` をまとめて剥く）に変更可能です。

- `<inputbase>.wav`  
  - 内容: シミュレータ側で決定論的に（同じシミュレーション入力なら常に同じ）収集した `mo_signed`（DAC 相当）を簡易スケーリングして 16-bit PCM に変換した「バニラな WAV」ファイルです。  
  - 重要: 生成される WAV は「ローパス等のアナログ再構成フィルタを通していない、生の（vanilla）サンプル」を直接 PCM 化したものです。つまり：
    - 出力は ZOH（あるいはサンプリング時のホールド）による離散的な値の並びになっており、そのままでは高調波が多く鋭い音に聞こえることがあります。  
    - 実機や `vgm2wav` 等で得られる音と聴感が違うのは、実機側で行われるローパス再構成や DC カット・正規化・エンベロープ処理が未適用のためです。  
  - 推奨ポスト処理（聴感向上）:
    - オーバーサンプリング + ローパスフィルタ（復調）→ ダウンサンプリング
    - DC 除去（highpass 約 5-20 Hz）
    - ピーク正規化（例: -1 dB）
    - これらは `tools/` にあるスクリプト、または外部ツール（sox 等）で実行できます。将来的にこのリポジトリ内で決定論的な FIR ローパスを組み込むことも可能です。
  - 決定論性: WAV はソフト側（ハーネス）で「同じアルゴリズム・同じ入力」によって再生されるため、同じコマンドで複数回実行すればバイナリレベルで同一の WAV が生成されます。

- 生成場所:  
  - `build_and_run.sh` は実行後にリポジトリルートをカレントディレクトリとしてシミュレータを起動するため、出力ファイル（CSV/WAV）は基本的に実行時の作業ディレクトリ（リポジトリのルート）に生成されます。  
  - 実行ログにも生成ファイル名を出力するようにしているので、実行後に `build/sim_last_output.txt` を確認すると `Audio outputs will be: <base>.csv and <base>.wav` のような行が出力されます。また、WAV 書き出し時には `WAV written: <path>` と標準出力に表示されます。

---

## 参考・注意事項

- 出力されるタイムスタンプはピコ秒（ps）単位です。WAV 変換スクリプトは ps を秒に変換してサンプリング間隔を計算します。
- VCD の全信号ダンプは巨大になります。ディスク容量に余裕がない場合は不要なトレースを無効化してください。
- IKAOPLL の RTL ソースは本リポジトリに含まれていません。必要な場合はローカルで取得してください。取得には付属のスクリプトを使えます（例: ./tools/create_rtl_dir.sh --fetch-raw https://github.com/ika-musume/IKAOPLL/tree/main）。
