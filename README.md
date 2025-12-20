# IKAOPLL-verilator

Verilator を使った軽量な IKAOPLL (YM2413相当) のシミュレーションハーネスです。  
以下のリファレンスを提供を目的としています
- Verilatorを活用したVerilogからCへの変換 (IKAOPLLのVerilogをC++に変換して実行）
- IKAOPLLへ任意のタイミングでレジスタアクセス (vgmのデータに基づきレジスタアクセスを行うテストベンチの提供）
- VCDやcsvによる情報のダンプ

## 前提環境

- Verilator（比較的新しいバージョンを推奨）
- g++ / make
- bash 環境（Linux / WSL 等）
- Python（任意、tools 配下のユーティリティ用）

## リポジトリ構成（重要ファイル）

- rtl/ — Verilog RTL（トップ／モジュール）
- src/ — C/C++ のラッパー、プレイヤー、ユーティリティ
- build/ — ビルド出力（obj_dir）
- build_and_run.sh — デフォルトのビルド＋実行スクリプト（デフォルトで VCD は出さない）
- build_and_run_debug.sh — `build_and_run.sh` をベースに最小差分で `--trace` ビルドしたもの。VCD の ON/OFF は実行時のフラグで制御します
- tests/ — テスト用 VGM/CSV/MML
- tools/ — 補助スクリプト

## ビルドと実行

VCD（波形ダンプ）あり/なしのフローを分けて扱うため、2 つのスクリプトを用意しています。

1) 通常（VCD OFF・高速）
- 目的：波形を出力しない通常のバッチ実行／WAV 出力の確認など
- 実行例：
  ```
  ./build_and_run.sh tests/csv/ym2413_scale_chromatic.vgm.csv
  ```

2) デバッグ（trace 有効でビルド、VCD は実行時で制御）
- 目的：trace（VCD）を取りたい場合に使う。ビルド自体は `--trace` を付けて行うが、VCD の実際の生成は実行時フラグで切り替えます。
- 実行例（trace ビルドのみ、VCD は出さない）：
  ```
  ./build_and_run_debug.sh tests/csv/ym2413_scale_chromatic.vgm.csv
  ```
- 実行例（trace ビルドして VCD を出す）：
  ```
  ./build_and_run_debug.sh tests/csv/ym2413_scale_chromatic.vgm.csv --vcd mytrace.vcd
  ```
- 実行時に使える主なフラグ（`main_vgm_csv.c` による）：
  - `--vcd [filename]` — VCD を有効にする（省略時は `ikaopll_dump.vcd`）
  - `--no-csv` — ACC/Mo の CSV ログ出力を無効にする

注意：VCD を生成するには trace をサポートしたバイナリ（`--trace` でビルド）で実行し、実行時に `--vcd` を渡す必要があります。debug スクリプトはそのために `--trace` ビルドを行いますが、VCD の ON/OFF 自体はランタイムでの制御としています。

## 典型的な作業フロー

1. VCD を使わない高速確認（推奨）
   ```
   rm -rf build/obj_dir
   ./build_and_run.sh tests/csv/ym2413_scale_chromatic.vgm.csv
   ```

2. trace ビルドのみ（VCD は出さない）
   ```
   rm -rf build/obj_dir
   ./build_and_run_debug.sh tests/csv/ym2413_scale_chromatic.vgm.csv
   ```

3. trace ビルドして VCD を生成
   ```
   rm -rf build/obj_dir
   ./build_and_run_debug.sh tests/csv/ym2413_scale_chromatic.vgm.csv --vcd mytrace.vcd
   ```

## ライセンス

本リポジトリのオリジナル部分は MIT ライセンスで配布されています（詳細は `LICENSE` または `LICENSE.md` を参照してください）。

なお、本リポジトリには IKAOPLL (https://github.com/ika-musume/IKAOPLL) のソースを `rtl/` 配下に含めています。これらの IKAOPLL のファイルは元の著作権表示および BSD 2‑Clause "Simplified" License を保持しています。取り込んだ IKAOPLL のライセンス文は `rtl/IKAOPLL_LICENSE` に保存してあります。IKAOPLL のソースを含めた配布（ソース・バイナリ問わず）を行う場合は、該当の著作権表示とライセンス文を削除せずに同梱してください。

English:
The original code written for this repository is licensed under the MIT License (see LICENSE or LICENSE.md).
This repository also bundles the IKAOPLL sources under `rtl/`, which are provided by the upstream author under the BSD 2‑Clause "Simplified" License. The text of that license is available at `rtl/IKAOPLL_LICENSE`.
When redistributing this repository (source or binary), please include the third‑party copyright/license text as required.