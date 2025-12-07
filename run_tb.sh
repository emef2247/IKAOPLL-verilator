#!/usr/bin/env bash
set -e
# Build and run with iverilog / vvp
# Requires: iverilog, vvp, (optionally gtkwave to view dump.vcd)
iverilog -o tb.vvp tb_iverilog.v dut_stub.v
vvp tb.vvp
echo "Simulation finished. VCD: dump.vcd (open with gtkwave dump.vcd)"