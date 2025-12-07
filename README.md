# IKAOPLL (Verilator harness)

このリポジトリは IKAOPLL を Verilator でシミュレーションするためのハーネスを含みます。
主に以下を提供します:
- Verilator 用の SystemVerilog トップラッパー (`IKAOPLL_vltb.sv`)
- Verilog/C++ テストベンチ (`tb/tb_ikaopll_vgm_verilator.cpp`)
- ビルド・実行スクリプトの例 (`build_and_run_vltb.sh`)
- CSV イベントから WAV を生成するワークフロー

重要な注意点（ハンドシェイク）
- 実チップモデルは tri-state を使うため、Verilator では tri-state を直接扱えません。
- そのため `IKAOPLL_reg_wrapper.v` が非侵襲的に観測用信号（o_BUSY, o_WRITE_DONE 等）を生成します。
- TB 側は BUSY/WRITE_DONE ハンドシェイクでレジスタ書き込みを待ちます。`o_BUSY` が立ち上がらない／WRITE_DONE が来ない場合は、phiM/phi1 の位相・サンプリングタイミング、または wrapper の queued フラグロジックを確認してください。

クイックスタート（ローカル）
1. リポジトリをクローン/配置
2. Verilator と必要ツールをインストール
   - Ubuntu 例: `sudo apt-get update && sudo apt-get install -y verilator g++ make`
3. ビルド・実行
   - `./build_and_run_vltb.sh`
   - 実行例: `./obj_dir/VIKAOPLL_vltb 47000000 3579545 44100 ym2413_scale_chromatic.delta.csv 8192`
4. 生成物
   - `dump.vcd` （波形）
   - `out_from_vgm.wav` （出力 WAV）

CI（GitHub Actions）を使うと、pushごとにビルドと簡単な実行が自動化できます（例は .github/workflows/verilator.yml を参照）。

大きなファイルについて
- WAV や生成される大きなバイナリは Git LFS を使って管理してください。

もしこちらで GitHub にリポジトリを作成してファイルを push したい場合、もしくは Pull Request を作りたい場合は教えてください。push するブランチ名（`main`/`master`/`dev` など）と公開可否（public/private）を教えてください。