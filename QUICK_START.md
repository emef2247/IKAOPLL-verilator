# QuickStart

1. リポジトリをクローンして移動
```bash
git clone git@github.com:emef2247/IKAOPLL-verilator.git
cd IKAOPLL-verilator
```

2. IKAOPLL の RTL と LICENSE を取得
```bash
./tools/create_rtl_dir.sh --fetch-raw https://github.com/ika-musume/IKAOPLL/blob/main
```

3. シミュレーション（ビルド及び実行）
- ビルドとシミュレーションの開始:
```bash
./build_and_run.sh ./tests/vgm/ym2413_scale_rom1.vgm
```
- 出力ファイルを確認:
```bash
ls -lh ym2413_scale_rom1.csv   # (CSV 出力が有効な場合)
ls -lh ym2413_scale_rom1.wav   # WAV（ハーネスが作成）
```
注: CSV形式のログが必要な場合は `--enable-csv` を追加してください。

4. VCD（全信号ダンプ）の生成
- VCD を有効にして実行（ファイル名を指定する例）:
```bash
./build_and_run.sh ./tests/vgm/ym2413_scale_rom1.vgm --vcd ikaopll_dump.vcd
```
- 出力（VCD）を確認:
```bash
ls -lh ikaopll_dump.vcd
```

