`timescale 10ps/10ps

module IKAOPLL_vltb (
    input  wire        i_XIN_EMUCLK,
    output wire        o_XOUT,

    output wire        phiMref_out,    // phi reference (derived from EMUCLK)

    input  wire        i_CS_n,
    input  wire        i_WR_n,
    input  wire        i_A0,
    input  wire [7:0]  i_D,

    output wire [7:0]  o_D_out,
    output wire        o_D_OE_out,

    input  wire signed [4:0] i_ACC_SIGNED_MOVOL,
    input  wire signed [4:0] i_ACC_SIGNED_ROVOL,

    output wire        o_ACC_SIGNED_STRB,
    output wire signed [15:0] o_ACC_SIGNED,
    // Test-only passthrough
    output wire        o_BUSY,
    output wire        o_WRITE_DONE,
	
	// Expose core-level handshakes to TB
    output wire        o_D9REG_WRDATA_QUEUED_N,
    output wire        o_D9REG_ADDR_MATCH,
    output wire        o_ADDRREG_WRRQ,
    output wire        o_DATAREG_WRRQ
);

    // phiMref generation: divide EMUCLK by 4 (match original TB behaviour)
    reg [1:0] clkdiv_phi = 2'd0;
    reg       phiMref_reg = 1'b0;

    always @(posedge i_XIN_EMUCLK) begin
        if (clkdiv_phi == 2'd3) begin
            clkdiv_phi  <= 2'd0;
            phiMref_reg <= 1'b1;
        end else begin
            clkdiv_phi <= clkdiv_phi + 2'd1;
            if (clkdiv_phi == 2'd1) phiMref_reg <= 1'b0;
        end
    end

    assign phiMref_out = phiMref_reg;

    // simple o_XOUT (inverted emuclk) for DUT if needed
    assign o_XOUT = ~i_XIN_EMUCLK;

    // --- generate an internal reset pulse for DUT (simulation-only) ---
    // Keep DUT in reset for RESET_CYCLES cycles of i_XIN_EMUCLK, then release.
    parameter integer RESET_CYCLES = 1024;
    reg rst_n_reg = 1'b0;
    reg [15:0] reset_counter = 16'd0;

    always @(posedge i_XIN_EMUCLK) begin
        if (reset_counter < RESET_CYCLES) begin
            reset_counter <= reset_counter + 16'd1;
            rst_n_reg <= 1'b0;
        end else begin
            rst_n_reg <= 1'b1;
        end
    end

    wire rst_n = rst_n_reg;

    // Tied defaults for optional ports
    wire i_phiM_PCEN_n = 1'b0;
    wire i_ALTPATCH_EN = 1'b0;

    // DUT small outputs
    wire [1:0] dut_o_D;
    wire       dut_o_D_OE;

    // Wires for low-level signals from DUT
    wire       dut_o_D9REG_WRDATA_QUEUED_N;
    wire       dut_o_D9REG_ADDR_MATCH;
    wire       dut_o_ADDRREG_WRRQ;
    wire       dut_o_DATAREG_WRRQ;
	
    // DAC / audio outputs required by DUT instance (internal wires)
    wire       dut_o_DAC_EN_MO;
    wire       dut_o_DAC_EN_RO;
    wire       dut_o_IMP_NOFLUC_SIGN;
    wire [7:0] dut_o_IMP_NOFLUC_MAG;
    wire signed [9:0] dut_o_IMP_FLUC_SIGNED_MO;
    wire signed [9:0] dut_o_IMP_FLUC_SIGNED_RO;

    // debug hooks (if DUT exposes these)
    wire [8:0] dut_o_FNUM;
    wire [2:0] dut_o_BLOCK;
    wire [3:0] dut_o_AR;
    wire [3:0] dut_o_DR;

    // Map DUT outputs to wrapper outputs (pad to 8 bits)
    assign o_D_out = {6'b0, dut_o_D};
    assign o_D_OE_out = dut_o_D_OE;

    wire       dut_o_BUSY;
    wire       dut_o_WRITE_DONE;
	
    // Instantiate DUT (named port connections)
    IKAOPLL u_DUT (
        .i_XIN_EMUCLK            (i_XIN_EMUCLK),
        .o_XOUT                  (o_XOUT),

        .i_phiM_PCEN_n           (i_phiM_PCEN_n),
        .i_IC_n                  (rst_n), /* reset input wired to rst_n_reg */
        .i_ALTPATCH_EN           (i_ALTPATCH_EN),

        .i_CS_n                  (i_CS_n),
        .i_WR_n                  (i_WR_n),
        .i_A0                    (i_A0),

        .i_D                     (i_D),
        .o_D                     (dut_o_D),
        .o_D_OE                  (dut_o_D_OE),

        .o_DAC_EN_MO             (dut_o_DAC_EN_MO),
        .o_DAC_EN_RO             (dut_o_DAC_EN_RO),

        .o_IMP_NOFLUC_SIGN       (dut_o_IMP_NOFLUC_SIGN),
        .o_IMP_NOFLUC_MAG        (dut_o_IMP_NOFLUC_MAG),

        .o_IMP_FLUC_SIGNED_MO    (dut_o_IMP_FLUC_SIGNED_MO),
        .o_IMP_FLUC_SIGNED_RO    (dut_o_IMP_FLUC_SIGNED_RO),

        .i_ACC_SIGNED_MOVOL      (i_ACC_SIGNED_MOVOL),
        .i_ACC_SIGNED_ROVOL      (i_ACC_SIGNED_ROVOL),
        .o_ACC_SIGNED_STRB       (o_ACC_SIGNED_STRB),
        .o_ACC_SIGNED            (o_ACC_SIGNED),
        .o_BUSY                  (dut_o_BUSY),
        .o_WRITE_DONE            (dut_o_WRITE_DONE),

        // new connections
        .o_D9REG_WRDATA_QUEUED_N (dut_o_D9REG_WRDATA_QUEUED_N),
        .o_D9REG_ADDR_MATCH      (dut_o_D9REG_ADDR_MATCH),
        .o_ADDRREG_WRRQ          (dut_o_ADDRREG_WRRQ),
        .o_DATAREG_WRRQ          (dut_o_DATAREG_WRRQ)
    );
	
	// map DUT wires to wrapper outputs
    assign o_BUSY = dut_o_BUSY;
    assign o_WRITE_DONE = dut_o_WRITE_DONE;


    // expose new signals to tb
    assign o_D9REG_WRDATA_QUEUED_N = dut_o_D9REG_WRDATA_QUEUED_N;
    assign o_D9REG_ADDR_MATCH      = dut_o_D9REG_ADDR_MATCH;
    assign o_ADDRREG_WRRQ          = dut_o_ADDRREG_WRRQ;
    assign o_DATAREG_WRRQ          = dut_o_DATAREG_WRRQ;
	
endmodule