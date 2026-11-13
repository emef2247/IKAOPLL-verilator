# Quick start

環境の準備（ツールの確認、RTLの配置、ビルド、動作確認）は一括で行えます。

```bash
./setup_env.sh
```

手動の場合は、WSLでVerilator、make、C/C++コンパイラを用意し、IKAOPLLのRTLとライセンスを`rtl/`に配置してください。

```bash
# RTLが未配置の場合のみ
bash tools/create_rtl_dir.sh --fetch-raw https://github.com/ika-musume/IKAOPLL/blob/main

# VGM → RTLシミュレーション → WAV
mkdir -p outputs
bash build_and_run.sh tests/vgm/ym2413_scale_rom1.vgm --output outputs/scale.wav

# ファイル／ディレクトリ単位の一括変換
python3 export_vgm.py /path/to/vgm_dir --outdir outputs

# 48 kHzで出力
build/obj_dir/ikaopll_sim tests/vgm/ym2413_scale_rom1.vgm --output outputs/scale48.wav --sample-rate 48000

# 問題を調査するときの中間CSVとバス記録
build/obj_dir/ikaopll_sim tests/vgm/ym2413_scale_rom1.vgm --output outputs/inspect.wav --enable-csv --debug outputs/inspect.bus.csv
```

通常はWAVだけを出力します。VGMヘッダーのクロックでIKAOPLLを動かし、ACCの更新を72クロックごとに取得して、指定のWAVレートへ実際に変換します。

詳細、対応範囲、回帰テストは[README.md](README.md)を参照してください。
