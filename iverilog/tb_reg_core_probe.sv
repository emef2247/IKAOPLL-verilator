`timescale 1ns/1ps
module tb_reg_core_probe ();

initial begin
    // small, focused VCD for the reg core
    $dumpfile("dump_reg_core.vcd");

    // correct hierarchy as you reported:
	$dumpvars(0, tb_iverilog_audio_improved.DUT.u_DUT.u_DAC);
    $dumpvars(0, tb_iverilog_audio_improved.DUT.u_DUT.u_REG.core);

    // tiny delay so hierarchical instances exist
    #1;
end

// monitor ACC_SIGNED strobe and print ACC_SIGNED when strobe rises.
// Path matches the dumpvars scope above.
always @ (posedge tb_iverilog_audio_improved.DUT.o_ACC_SIGNED_STRB) begin
    $display("[ACC_DBG] %0t ACC_SIGNED_STRB -> o_ACC_SIGNED = 0x%0h",
             $time, tb_iverilog_audio_improved.DUT.o_ACC_SIGNED);
end

endmodule

