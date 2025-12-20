# IKAOPLL-verilator

（既存の README 内容はそのまま残してください。以下は追記するライセンス／使い方のセクションです）

---

## 環境構築（rtl ディレクトリの作成）

本プロジェクトでは IKAOPLL の RTL ソースを `rtl/` 配下に置いて使います。手元にソースが無い場合は付属スクリプトで upstream から取得できます。fetch モードでは GitHub の `blob` ベース URL を与えてください（スクリプトが raw.githubusercontent 用 URL に変換します）。

例：
```bash
./tools/create_rtl_dir.sh --fetch-raw https://github.com/ika-musume/IKAOPLL/tree/main
```

- 上のコマンドは `rtl/IKAOPLL.v` と `rtl/IKAOPLL_modules/*` を取得します。
- 併せて upstream の LICENSE を `rtl/IKAOPLL_LICENSE` に保存します（サードパーティのライセンスを保持するため）。
- `curl` または `wget` が必要です。ない場合はインストールしてください。
- 既にファイルがある場合は上書きしません。強制上書きしたいときはスクリプトの `--force` オプションを使ってください。

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
- VCD/FST の出力はファイルサイズが非常に大きくなることがあります（full dump）。ディスク容量に注意してください。
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

## 参考・注意事項

- 出力されるタイムスタンプはピコ秒（ps）単位です。WAV 変換スクリプトは ps を秒に変換してサンプリング間隔を計算します。
- VCD の全信号ダンプは巨大になります。ディスク容量に余裕がない場合は不要なトレースを無効化してください。
- IKAOPLL の RTL ソースは本リポジトリに含まれていません。必要な場合はローカルで取得してください。取得には付属のスクリプトを使えます（例: ./tools/create_rtl_dir.sh --fetch-raw https://github.com/ika-musume/IKAOPLL/tree/main）。