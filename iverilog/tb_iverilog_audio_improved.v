`timescale 10ps/10ps
// Testbench with full DUT VCD dump and time-limited simulation.
//
// Simulation will automatically finish when simulation time reaches
// 567,943,460 ns (timescale 10ps => 567,943,460 * 100 = 56,794,346,000 ticks).
// NOTE: full DUT subtree is dumped (dump.vcd can be large).

module tb_iverilog_audio_improved;

    localparam integer AUDIO_SR = 44100;
    localparam integer TIMESCALE_PS = 10;
    localparam integer SAMPLE_PERIOD_PS = 1000000000000 / AUDIO_SR;
    localparam integer SAMPLE_TICKS = (SAMPLE_PERIOD_PS + (TIMESCALE_PS/2)) / TIMESCALE_PS;
    localparam integer HALF_TICKS = 140000 / TIMESCALE_PS;
    localparam integer CYCLES_PER_SAMPLE = 81; // adjust to match DUT's EMU clock mapping
    localparam integer TAIL_SAMPLES = 44100;

    // Simulation stop target (ticks, timescale units = 10ps)
    // 567,943,460 ns * 100 ticks/ns = 56,794,346,000 ticks
    reg [63:0] TARGET_TICKS = 64'd56794346000;

    reg clk = 0;
    initial begin
        clk = 0;
        forever begin
            #(HALF_TICKS) clk = ~clk;
        end
    end

    // instantiate run_control to allow runtime timeout + optional dump control
    // run_control must be compiled/linked in (see iverilog/run_control.v)
    // It listens on the provided clk and will call $finish after +TIMEOUT posedges.
    // Add: run_control u_run_control (.clk(clk));  (inserted below)
    // (Placed after clk declaration so clk symbol is visible)

    // I/O signals
    reg rstn = 0;
    reg i_CS_n = 1;
    reg i_WR_n = 1;
    reg i_A0 = 0;
    reg [7:0] i_D = 8'h00;

    wire o_D9REG_WRDATA_QUEUED_N;
    wire o_WRITE_DONE;
    wire o_BUSY;
    wire o_D9REG_ADDR_MATCH;
    wire o_ADDRREG_WRRQ;
    wire o_DATAREG_WRRQ;
    wire o_ACC_SIGNED_STRB;
    wire signed [15:0] o_ACC_SIGNED;

    // instantiate DUT (adjust if needed)
	IKAOPLL_vltb DUT (
		.i_XIN_EMUCLK         (clk),
		.o_XOUT               (),                // not used in TB
		.phiMref_out          (phiMref),         // connect phiMref_out to TB
		.i_CS_n               (i_CS_n),
		.i_WR_n               (i_WR_n),
		.i_A0                 (i_A0),
		.i_D                  (i_D),
		.o_D_out              (),                // if you want to inspect YM outputs, connect here
		.o_D_OE_out           (), 
		.i_ACC_SIGNED_MOVOL   (5'sd0),           // tie offs (or connect real vectors if you have them)
		.i_ACC_SIGNED_ROVOL   (5'sd0),
		.o_ACC_SIGNED_STRB    (o_ACC_SIGNED_STRB),
		.o_ACC_SIGNED         (o_ACC_SIGNED),
		.o_BUSY               (o_BUSY),
		.o_WRITE_DONE         (o_WRITE_DONE),
		.o_D9REG_WRDATA_QUEUED_N (o_D9REG_WRDATA_QUEUED_N),
		.o_D9REG_ADDR_MATCH      (o_D9REG_ADDR_MATCH),
		.o_ADDRREG_WRRQ          (o_ADDRREG_WRRQ),
		.o_DATAREG_WRRQ          (o_DATAREG_WRRQ)
	);

    // instantiate run_control now that clk symbol exists
    // Note: ensure iverilog/run_control.v is compiled as part of the build.
    run_control u_run_control (.clk(clk));

    // file for samples
    integer fh;
    integer sample_cnt;
    // global sample counter (single source of truth for "which audio sample index")
    reg [63:0] curr_sample;

    initial begin
        fh = $fopen("samples.txt","w");
        if (fh == 0) begin $display("[TB:ERR] cannot open samples.txt"); $finish; end
        sample_cnt = 0;
        curr_sample = 0;
    end

    // wait_samples task: waits N audio samples (synchronized to main loop's curr_sample)
    // NOTE: this implementation waits until the global curr_sample advances by n.
    // This avoids relying on $time / SAMPLE_TICKS and keeps all scheduling in units of samples.
    task wait_samples(input integer n);
        integer target;
    begin
        target = curr_sample + n;
        $display("[%0t] [TB:WAIT] wait_samples(%0d) -> waiting until curr_sample >= %0d (now=%0d)",
                 $time, n, target, curr_sample);
        // block until main loop increments curr_sample to the target
        wait (curr_sample >= target);
    end
    endtask

    // --- WRITE helpers synchronized to phiMref (DUT sampling clock)
    // This matches the original IKAOPLL_csv_tb timing semantics.

    // write_pulse synchronized to phiMref (match IKAOPLL_csv_tb.sv style)
    task write_pulse;
    begin
        // assume A0 and D have been set by caller before invoking write_pulse
        // sequence:
        //  posedge phiMref: set A0 stable (caller)
        //  negedge phiMref: assert CS (we assert CS at negedge in original TB; here we follow a safe sequence)
        @(posedge phiMref);
        // assert CS at negedge phiMref in original pattern; here do CS assert just before negedge
        i_CS_n = 1'b0;
        $display("[%0t] [TB:PULSE] i_A0=%0b i_D=0x%02x i_CS_n=%0b (CS asserted)", $time, i_A0, i_D, i_CS_n);
        @(negedge phiMref);
        // assert WR at negedge
        i_WR_n = 1'b0;
        // hold for one posedge
        @(posedge phiMref);
        // release WR and CS
        i_WR_n = 1'b1;
        i_CS_n = 1'b1;
        // after write, optionally release data
        i_D = 8'h00;
        @(posedge phiMref);
        $display("[%0t] [TB:PULSE] write done i_A0=%0b i_D=0x%02x i_CS_n=%0b", $time, i_A0, i_D, i_CS_n);
    end
    endtask

    // write address then data using phiMref timing
    task write_addr_then_data(input [7:0] addr, input [7:0] data);
    begin
        // address phase: set A0 low and set i_D = addr on posedge phiMref
        @(posedge phiMref);
        i_A0 = 1'b0;
        i_D = addr;
        $display("[%0t] [TB:PREWRITE] setting ADDR i_A0=%0b i_D=0x%02x (addr)", $time, i_A0, i_D);
        write_pulse(); // this sequence will perform CS/WR according to phiMref

        // data phase: set A0 high and set i_D = data on posedge phiMref
        @(posedge phiMref);
        i_A0 = 1'b1;
        i_D = data;
        $display("[%0t] [TB:PREWRITE] setting DATA i_A0=%0b i_D=0x%02x (data)", $time, i_A0, i_D);
        write_pulse();

        // small settle after write
        @(posedge phiMref);
        $display("[%0t] [TB:POST] post-write probe o_ACC_SIGNED=%0d", $time, $signed(o_ACC_SIGNED));
    end
    endtask

    // schedule queue (arrays)
    integer pending_addr [0:1000000];
    integer pending_data [0:1000000];
    integer pending_target [0:1000000];
    integer pending_head;
    integer pending_tail;
    initial begin
        pending_head = 0;
        pending_tail = 0;
    end

    // schedule_write now uses curr_sample as the reference (no $time division)
    task schedule_write(input [7:0] addr, input [7:0] data);
        integer target_sample;
    begin
        // schedule at the current sample index (semantics: schedule for "this sample")
        // If you prefer "next sample", change to curr_sample + 1
        target_sample = curr_sample;
        pending_addr[pending_tail] = addr;
        pending_data[pending_tail] = data;
        pending_target[pending_tail] = target_sample;
        $display("[%0t] [TB:SCHED] idx=%0d addr=0x%02x data=0x%02x target_sample=%0d (curr_sample=%0d)",
                 $time, pending_tail, addr, data, target_sample, curr_sample);
        pending_tail = pending_tail + 1;
    end
    endtask

    // --- Control initial: include events and reset
    initial begin
        // reset
        rstn = 1'b0; i_CS_n = 1; i_WR_n = 1; i_A0 = 0; i_D = 8'h00;
        repeat (1024) @(posedge clk);
        rstn = 1'b1;
        $display("[%0t] [TB:CTRL] rstn released", $time);

        // include the generated statements file (wait_samples(); schedule_write(); ...)
        // NOTE: wait_samples now waits on curr_sample; main loop increments curr_sample.
        `include "iverilog/tests/events_for_tb_iverilog.statements.v"

        $display("[%0t] [TB:CTRL] events done", $time);
    end

    // --- Time monitor: enforce global simulation end at TARGET_TICKS ---
    initial begin
        // wait until simulation time >= target ticks, then close & finish.
        wait ($time >= TARGET_TICKS);
        $display("[%0t] [TB:CTRL] TARGET time reached (%0d ticks) -> finishing", $time, TARGET_TICKS);
        if (fh != 0) $fclose(fh);
        $finish;
    end

    // main loop: step by sample, apply queued writes, record sample
    initial begin
        @(posedge clk);
        repeat (10) @(posedge clk);
        forever begin
            // advance by one audio sample (clock edges)
            repeat (CYCLES_PER_SAMPLE) @(posedge clk);

            // increment the global sample counter (single source of truth)
            curr_sample = curr_sample + 1;

            // apply pending writes scheduled for this sample or earlier (FIFO)
            while (pending_head < pending_tail && pending_target[pending_head] <= curr_sample) begin
                $display("[%0t] [TB:APPLY] idx=%0d addr=0x%02x data=0x%02x target_sample=%0d curr_sample=%0d",
                         $time, pending_head, pending_addr[pending_head], pending_data[pending_head], pending_target[pending_head], curr_sample);
                write_addr_then_data(pending_addr[pending_head][7:0], pending_data[pending_head][7:0]);
                pending_head = pending_head + 1;
            end

            // record sample after queue applied
            $fwrite(fh, "%0d\n", $signed(o_ACC_SIGNED));
            sample_cnt = sample_cnt + 1;
        end
    end

endmodule