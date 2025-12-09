This folder contains Icarus/iverilog testbenches and helper scripts used for quick local validation.

Files:
- dut_stub.v, dut_stub_audio.v, dut_stub_audio_improved.v : simple DUT stubs for iverilog testing
- tb_iverilog*.v : various iverilog testbenches
- tools/ : converter scripts (events -> statements, samples -> wav)

Usage:
1. Generate events_for_tb_iverilog.statements.v (if not present) with the script:
   python3 tools/events_to_tb_iverilog_samplerate.py --input tests/events_csv_tb.v --output tests/events_for_tb_iverilog.statements.v --sample-rate 44100

2. Run iverilog tests:
   iverilog -g2012 -o tb_audio_improved.vvp dut_stub_audio_improved.v tb_iverilog_audio_improved.v
   vvp tb_audio_improved.vvp

3. Convert samples.txt to WAV:
   python3 tools/samples_txt_to_wav.py samples.txt out_from_iverilog_improved.wav --sr 44100

Notes:
- Keep Verilator TB as the authoritative WAV generator (tb/tb_ikaopll_vgm_verilator.cpp).
- The iverilog folder is for lightweight comparisons and fast iteration.

