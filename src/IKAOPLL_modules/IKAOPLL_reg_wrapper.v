`timescale 10ps/10ps
// IKAOPLL_reg_wrapper.v (async-sampler + robust busy/done FSM with bus-sample-triggered pending)
//
// This wrapper samples CPU bus asynchronously, synchronizes & stretches to phi1 windows,
// and exposes non-invasive monitor signals. The busy/done generator below uses
// explicit edge detection + bus-sample trigger to guarantee exactly one WRITE_DONE per data write.

module IKAOPLL_reg_wrapper #(
    parameter FULLY_SYNCHRONOUS = 1,
    parameter ALTPATCH_CONFIG_MODE = 0,
    parameter INSTROM_STYLE = 0,
    parameter integer POST_RESET_HOLD_CYCLES = 4,
    parameter integer STRETCH_PHI1_WINDOWS = 2
)(
    input  wire           i_EMUCLK,
    input  wire           i_phiM_PCEN_n,
    input  wire           i_RST_n,
    input  wire           i_phi1_PCEN_n,
    input  wire           i_phi1_NCEN_n,

    input  wire           i_CS_n,
    input  wire           i_WR_n,
    input  wire           i_A0,
    input  wire   [7:0]   i_D,
    output wire   [1:0]   o_D,
    output wire           o_D_OE,

    input  wire           i_ALTPATCH_EN,

    input  wire           i_CYCLE_00, i_CYCLE_12, i_CYCLE_21, i_CYCLE_D3_ZZ, i_CYCLE_D4_ZZ, i_MnC_SEL,

    output wire   [3:0]   o_TEST,
    output wire           o_RHYTHM_EN,
    output wire   [8:0]   o_FNUM,
    output wire   [2:0]   o_BLOCK,
    output reg            o_KON,
    output wire           o_SUSEN,
    output reg    [5:0]   o_TL,
    output reg            o_DC, o_DM,
    output reg    [2:0]   o_FB,
    output reg            o_AM, o_PM, o_ETYP, o_KSR,
    output reg    [3:0]   o_MUL,
    output reg    [1:0]   o_KSL,
    output wire   [3:0]   o_AR, o_DR, o_RR,
    output reg    [3:0]   o_SL,

    output wire           o_EG_ENVCNTR_TEST_DATA,
    input  wire   [9:0]   i_REG_TEST_PHASE,
    input  wire   [6:0]   i_REG_TEST_ATTNLV,
    input  wire   [8:0]   i_REG_TEST_SNDDATA,

    output wire           o_WRITE_DONE,
    output wire           o_BUSY,

    output wire           o_D9REG_WRDATA_QUEUED_N,
    output wire           o_D9REG_ADDR_MATCH,
    output wire           o_ADDRREG_WRRQ,
    output wire           o_DATAREG_WRRQ
);

    // Core-facing wires (pass-through)
    wire [1:0] core_o_D; wire core_o_D_OE;
    wire [3:0] core_o_TEST; wire core_o_RHYTHM_EN;
    wire [8:0] core_o_FNUM; wire [2:0] core_o_BLOCK;
    wire core_o_SUSEN; wire core_o_EG_ENVCNTR_TEST_DATA;
    wire [3:0] core_o_AR, core_o_DR, core_o_RR;
    wire core_o_D9REG_WRDATA_QUEUED_N;
    wire core_o_D9REG_ADDR_MATCH;
    wire core_o_ADDRREG_WRRQ;
    wire core_o_DATAREG_WRRQ;

    // =========================================================================
    // busy / write-done generator (edge-detection + bus-sample trigger)
    // - pending_write is set when either:
    //     * we observe a DATAREG_WRRQ rising edge from core, or
    //     * wrapper's own bus-sampler captures a data write (latched_a0 == 1)
    // - when queued_n rises (0->1) and pending_write is set, emit single-cycle WRITE_DONE and clear pending.
    // - BUSY asserted while queued_n==0 or while pending_write == 1.
    // =========================================================================

    reg datawrq_prev;
    reg queued_prev;
    reg pending_write;
    reg write_done_reg;
    reg busy_reg;

    always @(posedge i_EMUCLK) begin
        if (!i_RST_n) begin
            datawrq_prev <= 1'b0;
            queued_prev <= 1'b1; // assume idle
            pending_write <= 1'b0;
            write_done_reg <= 1'b0;
            busy_reg <= 1'b0;
        end else begin
            // default clear single-cycle pulse
            write_done_reg <= 1'b0;

            // detect DATAREG_WRRQ rising edge from core (safety)
            if (core_o_DATAREG_WRRQ & ~datawrq_prev) begin
                pending_write <= 1'b1;
            end

            // queued rising edge detection (0 -> 1)
            if ((core_o_D9REG_WRDATA_QUEUED_N == 1'b1) && (queued_prev == 1'b0)) begin
                if (pending_write) begin
                    // consume pending and produce one-shot WRITE_DONE
                    write_done_reg <= 1'b1;
                    pending_write <= 1'b0;
                end
            end

            // busy: true while queued is active (0) or while we have a pending write waiting to complete
            busy_reg <= (~core_o_D9REG_WRDATA_QUEUED_N) | pending_write;

            // update previous samples
            datawrq_prev <= core_o_DATAREG_WRRQ;
            queued_prev <= core_o_D9REG_WRDATA_QUEUED_N;
        end
    end

    // =========================================================================
    // post-reset hold, async sampler, stretch & core input steering
    // =========================================================================

    // post-reset hold counter
    reg [15:0] post_reset_hold_cnt;
    always @(posedge i_EMUCLK) begin
        if (!i_RST_n) post_reset_hold_cnt <= POST_RESET_HOLD_CYCLES;
        else if (!i_phi1_NCEN_n && post_reset_hold_cnt != 0) post_reset_hold_cnt <= post_reset_hold_cnt - 1;
    end
    wire hold_active = (post_reset_hold_cnt != 0);

    // async sampler
    reg        sampled_valid_async;
    reg        sampled_a0_async;
    reg [7:0]  sampled_d_async;

    always @(negedge i_CS_n or negedge i_WR_n or posedge i_RST_n) begin
        if (i_RST_n == 1'b0) begin
            sampled_valid_async <= 1'b0;
            sampled_a0_async <= 1'b0;
            sampled_d_async <= 8'h00;
        end else begin
            if (i_CS_n == 1'b0) begin
                sampled_a0_async <= i_A0;
                sampled_d_async  <= i_D;
                sampled_valid_async <= 1'b1;
            end else if (i_WR_n == 1'b0 && i_CS_n == 1'b0) begin
                sampled_a0_async <= i_A0;
                sampled_d_async  <= i_D;
                sampled_valid_async <= 1'b1;
            end
        end
    end

    // synchronize sample & stretch; also set pending_write when we capture a data write
    reg latched_valid;
    reg latched_a0;
    reg [7:0] latched_d;
    reg [7:0] stretch_cnt;

    always @(posedge i_EMUCLK) begin
        if (!i_RST_n) begin
            latched_valid <= 1'b0;
            latched_a0    <= 1'b0;
            latched_d     <= 8'h00;
            stretch_cnt   <= 0;
            sampled_valid_async <= 1'b0;
            // note: do not alter pending_write here (preserve state across reset handling above)
        end else begin
            if (!hold_active && sampled_valid_async && !latched_valid) begin
                latched_a0 <= sampled_a0_async;
                latched_d  <= sampled_d_async;
                latched_valid <= 1'b1;
                if (stretch_cnt == 0) stretch_cnt <= STRETCH_PHI1_WINDOWS;
                // IMPORTANT: when our sampler captures a DATA write (A0==1), set pending_write here.
                if (sampled_a0_async == 1'b1) begin
                    pending_write <= 1'b1;
                end
                sampled_valid_async <= 1'b0;
            end

            if (i_CS_n == 1'b1) latched_valid <= 1'b0;

            if (!i_phi1_NCEN_n && stretch_cnt != 0) stretch_cnt <= stretch_cnt - 1;
        end
    end

    // core input priority: hold_active > stretch(latched) > direct
    wire core_i_CS_n  = hold_active ? 1'b1 : (stretch_cnt != 0 ? 1'b0 : i_CS_n);
    wire core_i_WR_n  = hold_active ? 1'b1 : (stretch_cnt != 0 ? 1'b0 : i_WR_n);
    wire core_i_A0    = hold_active ? 1'b0 : (stretch_cnt != 0 ? latched_a0 : i_A0);
    wire [7:0] core_i_D = hold_active ? 8'h00 : (stretch_cnt != 0 ? latched_d : i_D);

    // instantiate core (named ports)
    IKAOPLL_reg #(
        .FULLY_SYNCHRONOUS(FULLY_SYNCHRONOUS),
        .ALTPATCH_CONFIG_MODE(ALTPATCH_CONFIG_MODE),
        .INSTROM_STYLE(INSTROM_STYLE)
    ) core (
        .i_EMUCLK(i_EMUCLK),
        .i_phiM_PCEN_n(i_phiM_PCEN_n),
        .i_RST_n(i_RST_n),
        .i_phi1_PCEN_n(i_phi1_PCEN_n),
        .i_phi1_NCEN_n(i_phi1_NCEN_n),

        .i_CS_n(core_i_CS_n),
        .i_WR_n(core_i_WR_n),
        .i_A0(core_i_A0),
        .i_D(core_i_D),

        .o_D(core_o_D),
        .o_D_OE(core_o_D_OE),

        .i_ALTPATCH_EN(i_ALTPATCH_EN),

        .i_CYCLE_00(i_CYCLE_00),
        .i_CYCLE_12(i_CYCLE_12),
        .i_CYCLE_21(i_CYCLE_21),
        .i_CYCLE_D3_ZZ(i_CYCLE_D3_ZZ),
        .i_CYCLE_D4_ZZ(i_CYCLE_D4_ZZ),
        .i_MnC_SEL(i_MnC_SEL),

        .o_TEST(core_o_TEST),
        .o_RHYTHM_EN(core_o_RHYTHM_EN),
        .o_FNUM(core_o_FNUM),
        .o_BLOCK(core_o_BLOCK),

        .o_KON(o_KON),
        .o_SUSEN(core_o_SUSEN),
        .o_TL(o_TL),

        .o_DC(o_DC),
        .o_DM(o_DM),

        .o_FB(o_FB),
        .o_AM(o_AM),
        .o_PM(o_PM),
        .o_ETYP(o_ETYP),
        .o_KSR(o_KSR),
        .o_MUL(o_MUL),
        .o_KSL(o_KSL),
        .o_AR(core_o_AR),
        .o_DR(core_o_DR),
        .o_RR(core_o_RR),
        .o_SL(o_SL),

        .o_EG_ENVCNTR_TEST_DATA(core_o_EG_ENVCNTR_TEST_DATA),
        .i_REG_TEST_PHASE(i_REG_TEST_PHASE),
        .i_REG_TEST_ATTNLV(i_REG_TEST_ATTNLV),
        .i_REG_TEST_SNDDATA(i_REG_TEST_SNDDATA),

        .o_D9REG_WRDATA_QUEUED_N(core_o_D9REG_WRDATA_QUEUED_N),
        .o_D9REG_ADDR_MATCH(core_o_D9REG_ADDR_MATCH),
        .o_ADDRREG_WRRQ(core_o_ADDRREG_WRRQ),
        .o_DATAREG_WRRQ(core_o_DATAREG_WRRQ)
    );

    // pass-through outputs
    assign o_D = core_o_D;
    assign o_D_OE = core_o_D_OE;
    assign o_TEST = core_o_TEST;
    assign o_RHYTHM_EN = core_o_RHYTHM_EN;
    assign o_FNUM = core_o_FNUM;
    assign o_BLOCK = core_o_BLOCK;
    assign o_SUSEN = core_o_SUSEN;
    assign o_EG_ENVCNTR_TEST_DATA = core_o_EG_ENVCNTR_TEST_DATA;
    assign o_AR = core_o_AR;
    assign o_DR = core_o_DR;
    assign o_RR = core_o_RR;

    assign o_D9REG_WRDATA_QUEUED_N = core_o_D9REG_WRDATA_QUEUED_N;
    assign o_D9REG_ADDR_MATCH      = core_o_D9REG_ADDR_MATCH;
    assign o_ADDRREG_WRRQ          = core_o_ADDRREG_WRRQ;
    assign o_DATAREG_WRRQ          = core_o_DATAREG_WRRQ;

    // expose busy/done
    assign o_BUSY = busy_reg;
    assign o_WRITE_DONE = write_done_reg;

endmodule