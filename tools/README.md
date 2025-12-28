# tools/README

このディレクトリ (`tools/`) には、シミュレータ出力（VCD / CSV / mo change logs 等）を扱うための補助スクリプトを置いています。ここでは各スクリプトの目的、前提、代表的な使い方、ワークフロー例、よくあるトラブルと対処法を日本語でまとめます。

> 注意：各スクリプトは呼び出し時に `--help`（または `-h`）でヘルプを出力するものが多いです。まずはローカルのスクリプトでヘルプを確認してください（実装差異がある可能性があります）。

---

## 前提（Prerequisites）

- Python 3（3.6 以上を推奨）
- 一部スクリプトは NumPy / Pandas / SciPy / tqdm 等を利用する場合があります。ヘルプに依存パッケージ記載があれば `pip` でインストールしてください。
  ```
  pip install numpy pandas scipy tqdm
  ```
- GTKWave 等の VCD/FST ビューワ（トレース参照用）
- 実行はリポジトリのルートで行うことを想定しています（`build_and_run.sh` 実行後に生成されるファイルを扱いやすいため）。

---

## 収録スクリプト（概要）

以下は本リポジトリに含まれる代表的なツールと、想定される使い方の例です。詳細は各スクリプトの `--help` を参照してください。

### 1) stream_vcd_byname.py
- 目的：VCD（波形ダンプ）から指定した信号を逐次抽出し、CSV に吐き出す／監視するためのスクリプト。
- 主な用途：
  - 特定信号（例：`o_IMP_FLUC_SIGNED_MO`, `OP_PHASE`）を抽出して CSV にする。
  - メモリ効率重視で VCD をストリーミング処理する実装（大きな VCD を扱うため）。
- 代表例：
  - 単一信号を CSV に:
    ```
    python3 tools/stream_vcd_byname.py -i ikaopll_dump.vcd -n o_IMP_FLUC_SIGNED_MO -o mo.csv
    ```
  - 複数信号を同時出力:
    ```
    python3 tools/stream_vcd_byname.py -i ikaopll_dump.vcd -n o_IMP_FLUC_SIGNED_MO,o_ACC_SIGNED -o outputs.csv
    ```
  - 信号名を正規表現で指定（実装が対応している場合）:
    ```
    python3 tools/stream_vcd_byname.py -i ikaopll_dump.vcd -r '^o_OP_PHASE' -o phase_signals.csv
    ```
- 使う前に：必ず `python3 tools/stream_vcd_byname.py --help` を確認してください。オプション名や振る舞いはローカル版によって多少異なる可能性があります。

---

### 2) mo_changes_to_wav.py / mo_changes_to_wav_weighted.py
- 目的：`mo_value_changes.csv`（MO の value-change ログ）を WAV に変換するためのスクリプト群。
- 補足：`mo_value_changes.csv` は変化のみを記録するため、そのまま ZOH（直前の値を保持）でリサンプリングすると短パルスが失われる／エネルギー表現が変わる場合があります。`mo_changes_to_wav_weighted.py` は各オーディオサンプル区間に対する時間重み付き平均を採ることで短パルスのエネルギーを保持する方式を提供します。
- 代表例：
  - ZOH（単純リサンプリング）:
    ```
    python3 tools/mo_changes_to_wav.py -i mo_value_changes.csv -o mo_zoh.wav --samplerate 44100 --scale 64
    ```
  - 重み付き平均デシメーション:
    ```
    python3 tools/mo_changes_to_wav_weighted.py -i mo_value_changes.csv -o mo_avg.wav --sr 44100 --scale 64
    ```
- オプションの例：`--sr`（サンプリングレート）, `--scale`（値のスケーリング）, `--start-ps` / `--end-ps`（ps 単位の時間範囲指定）など。

---

### 3) join_and_fit_filtered
- 目的：`audio_samples.csv` のような等間隔サンプリング CSV から WAV を生成するユーティリティ。
- 代表例：
  ```
  python3 tools/csv_to_wav.py -i audio_samples.csv -o out_acc.wav --col-name acc_signed --scale 1 --sr 44100
  ```
- CSV にカラム名ヘッダがあることを前提に動作する場合があります。ヘッダ名（`mo_signed`, `acc_signed` など）をスクリプトのオプションで指定してください。

---

## 典型的なワークフロー（例）

1. シミュレーション実行（リポジトリルートで）
```bash
./build_and_run.sh tests/vgm/ym2413_scale_rom1.vgm --enable-csv
# またはトレースを取りたい場合
./build_and_run.sh tests/vgm/ym2413_scale_rom1.vgm --vcd ikaopll_dump.vcd
```

2. VCD から信号抽出（例: MO 出力）
```bash
python3 tools/stream_vcd_byname.py -i ikaopll_dump.vcd -n o_IMP_FLUC_SIGNED_MO -o mo_vcd.csv
```

3. value-change ログから WAV を作る（より忠実に）
```bash
python3 tools/mo_changes_to_wav_weighted.py -i mo_value_changes.csv -o mo_avg.wav --sr 44100 --scale 64
```

4. `audio_samples.csv`（等間隔サンプリング）を直接 WAV に
```bash
python3 tools/csv_to_wav.py -i audio_samples.csv -o audio_samples.wav --col-name mo_signed --scale 64 --sr 44100
```

---

