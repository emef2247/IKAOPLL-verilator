# IKAOPLL-verilator

YM2413 / IKAOPLL の RTL (`IKAOPLL.v` + modules) を Verilator でラップして、
VGM 由来のテストパタンを C プログラムから実行するためのリポジトリです。

## ディレクトリ構成

- `rtl/`
  - `IKAOPLL.v`
  - `IKAOPLL_modules/IKAOPLL_*.v`  
    → 元プロジェクト（IKAOPLL）からコピーした RTL 一式
- `src/`
  - `ikaopll_wrapper.h`, `ikaopll_wrapper.cpp`  
    - Verilated `VIKAOPLL` の薄いラッパ
    - 機能:
      - EMUCLK クロック生成（posedge/negedge）
      - リセットシーケンス (`i_IC_n`)
      - YM2413 バスポート (`i_CS_n`, `i_WR_n`, `i_A0`, `i_D`) の setter
      - ACC/MO 出力取得 (`o_ACC_SIGNED`, `o_IMP_FLUC_SIGNED_MO`)
      - VCD トレース (`--trace` / `ikaopll_dump.vcd`)
  - `ym2413_bus.h`, `ym2413_bus.c`  
    - テストベンチ `IKAOPLL_vgm_tb.sv` の `phiM`/WAIT ロジックを C に移植した層
    - 機能:
      - EMUCLK を 1cycle ずつ進めつつ、TB と同じ `clkdiv`/`phiMref`/`phiM_cnt` をソフト側で再現
      - YM2413 バスの WAIT ルール実装:
        - `LAST_ADDR` → 次アクセスまで最小 12 φM
        - `LAST_DATA` → 次アクセスまで最小 84 φM
      - `ym2413_bus_write_addr` / `ym2413_bus_write_data` が 1 回分の IKAOPLL_write に対応
  - `vgm_player.h`, `vgm_player.c`  
    - `*.vgm.csv`（`delay,reg,data`）を読み、VGM のタイムラインを `ym2413_bus` で再生
    - 機能:
      - CSV パーサ
      - VGM サンプル単位の delay を φM カウントに変換（約 20 φM / sample）
  - `main_vgm_csv.c`  
    - C 側のエントリポイント（`./ikaopll_sim [csv_path]`）
    - 処理の流れ:
      1. `ikaopll_init()` / `ikaopll_trace_init("ikaopll_dump.vcd")`
      2. `ikaopll_reset()`
      3. `ym2413_bus_init(&bus)`
      4. `vgm_player_run_csv("vgm_data/tests/ym2413_scale_chromatic.vgm.csv", &bus)`
      5. 末尾で少し φM を進めて ACC/MO を 1 サンプル読む

- `vgm_data/tests/`
  - `ym2413_scale_chromatic.vgm`  
    → MSX MSGDRV 用データを openMSX で再生し VGM 化したもの
  - `ym2413_scale_chromatic.vgm.csv`  
    → `vgm_to_ym2413_csv.py` で YM2413 コマンドだけを抜き出した CSV
  - `ym2413_scale_chromatic.vh`  
    → `vgm_csv_to_vh.py` で `.vh` に変換したもの（Verilog TB 用）

## ビルドと実行

前提: Verilator がインストールされていること。

```sh
./build_and_run.sh
```

- `build/obj_dir/ikaopll_sim` が生成され、`vgm_data/tests/ym2413_scale_chromatic.vgm.csv` を読み込んでシミュレーションを実行します。
- 実行後、`build/ikaopll_dump.vcd` が生成されます。`gtkwave` などで波形を確認できます。

別の CSV を使いたい場合は、引数で指定します:

```sh
./build/obj_dir/ikaopll_sim path/to/other.vgm.csv
```

（`build_and_run.sh` を編集しても良いです。）

## VGM → CSV → .vh ツール

`vgm_csv_to_vh.py` / `vgm_to_ym2413_csv.py` は元リポジトリからコピーした Python スクリプトで、

1. `.vgm` → `vgm_to_ym2413_csv.py` → `*.vgm.csv`
2. `*.vgm.csv` → `vgm_csv_to_vh.py` → `*.vh`

と変換することで、Verilog テストベンチ用の `IKAOPLL_write` 列を生成できます。  
C 側では `*.vgm.csv` を直接 `vgm_player` で読み込み、`ym2413_bus` 経由で IKAOPLL に流し込んでいます。