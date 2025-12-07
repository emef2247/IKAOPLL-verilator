#!/usr/bin/env bash
# Compile and run IKAOPLL testbench (iverilog + vvp)
# Assumes:
#  - Verilog sources are in ./src (change SRCDIR if different)
#  - Testbench file you edited is IKAOPLL_tb.sv in the current dir (or give correct path)
#  - dump_control.sv is present (to produce dump.vcd)
set -e
SRCDIR=./src
TB=./IKAOPLL_tb.sv
DUMPCTL=./dump_control.sv

echo "Compiling Verilog sources..."
# Use -g2012 to allow SystemVerilog constructs
iverilog -g2012 -o tb.vvp $SRCDIR/*.v $TB $DUMPCTL

echo "Running simulation..."
vvp tb.vvp

echo "Simulation finished. VCD: dump.vcd"
echo "Open with: gtkwave dump.vcd &"

