`timescale 1ns/1ps
// DUT stub with attack ramp, stable busy clearing, and continuous sample output.
// Log messages tagged for easier parsing.

module dut_stub_audio_improved #(
    parameter integer QUEUED_RETURN_CYCLES = 10,
    parameter integer BUSY_CYCLES = 120,
    parameter integer IMMEDIATE_QUEUED = 1,
    parameter integer CYCLES_PER_AUDIO_SAMPLE = 162,
    parameter integer MAX_SAMPLE_TICKS = 8192,
    parameter integer ATTACK_SAMPLES = 8
) (
    input  wire clk,
    input  wire rstn,
    input  wire i_CS_n,
    input  wire i_WR_n,
    input  wire i_A0,
    input  wire [7:0] i_D,
    output reg  o_D9REG_WRDATA_QUEUED_N,
    output reg  o_WRITE_DONE,
    output reg  o_BUSY,
    output reg  [7:0] d9data,
    output reg  [5:0] d9addr,
    output reg        o_ACC_SIGNED_STRB,
    output reg signed [15:0] o_ACC_SIGNED
);

    // internal regs
    reg i_WR_n_d;
    reg [31:0] queued_timer;
    reg [31:0] busy_timer;
    // latched address/data
    reg [7:0] latched_data;
    reg [5:0] latched_addr;

    integer sample_ticks_left;
    integer sample_amp;   // scaled signed amplitude
    integer mag;
    integer audio_cycle_cnt;

    // temporaries
    integer new_amp;
    integer addlen;

    integer busy_next;

    // attack handling
    integer attack_counter;
    integer delta_amp;

    // tuning constants
    localparam integer SCALE_FACTOR = 32;
    localparam integer BASE_LEN = 256;
    localparam integer K_DIV = 512;
    localparam integer DECAY_NUM = 250;
    localparam integer DECAY_DEN = 256;

    // initialize
    initial begin
        o_D9REG_WRDATA_QUEUED_N = 1;
        o_WRITE_DONE = 0;
        o_BUSY = 0;
        d9data = 8'h00;
        d9addr = 6'h00;
        i_WR_n_d = 1;
        queued_timer = 0;
        busy_timer = 0;
        sample_ticks_left = 0;
        sample_amp = 0;
        mag = 0;
        audio_cycle_cnt = 0;
        o_ACC_SIGNED_STRB = 1'b0;
        o_ACC_SIGNED = 16'sd0;
        new_amp = 0;
        addlen = 0;
        attack_counter = 0;
        delta_amp = 0;
        busy_next = 0;
        latched_data = 8'h00;
        latched_addr = 6'h00;
    end

    // synchronous process
    always @(posedge clk or negedge rstn) begin
        if (!rstn) begin
            // reset all
            i_WR_n_d <= 1;
            queued_timer <= 0;
            busy_timer <= 0;
            o_D9REG_WRDATA_QUEUED_N <= 1;
            o_WRITE_DONE <= 0;
            o_BUSY <= 0;
            d9data <= 8'h00;
            d9addr <= 6'h00;
            sample_ticks_left <= 0;
            sample_amp <= 0;
            mag <= 0;
            audio_cycle_cnt <= 0;
            o_ACC_SIGNED_STRB <= 1'b0;
            o_ACC_SIGNED <= 16'sd0;
            latched_data <= 8'h00;
            latched_addr <= 6'h00;
            new_amp <= 0;
            addlen <= 0;
            attack_counter <= 0;
            delta_amp <= 0;
            busy_next <= 0;
        end else begin
            // default signals
            o_ACC_SIGNED_STRB <= 1'b0;
            i_WR_n_d <= i_WR_n;

            // queued_timer decrement
            if (queued_timer > 0) queued_timer <= queued_timer - 1;
            if (queued_timer == 1) o_D9REG_WRDATA_QUEUED_N <= 1;

            // stable busy clear
            busy_next = (busy_timer > 0) ? busy_timer - 1 : 0;
            busy_timer <= busy_next;
            if (busy_next == 0 && o_BUSY) begin
                o_BUSY <= 0;
                o_WRITE_DONE <= 0;
                $display("[%0t] [DUT:BUSY] busy cleared (busy_next==0)", $time);
            end

            // handle bus writes BEFORE audio_tick handling
            if (i_WR_n_d == 1 && i_WR_n == 0 && i_CS_n == 0) begin
                if (i_A0 == 1'b0) begin
                    // address write
                    d9addr <= i_D[5:0];
                    latched_addr <= i_D[5:0];
                    $display("[%0t] [DUT:ADDR] ADDR_WR latched addr=0x%02x", $time, i_D[5:0]);
                end else begin
                    // data write
                    latched_data <= i_D;
                    d9data <= i_D;

                    // queued / busy behaviour
                    o_D9REG_WRDATA_QUEUED_N <= 0;
                    queued_timer <= QUEUED_RETURN_CYCLES;

                    // set busy/write_done and busy timer (immediate)
                    o_BUSY <= 1;
                    o_WRITE_DONE <= 1;
                    busy_timer <= BUSY_CYCLES;
                    $display("[%0t] [DUT:DATA] DATA_WR latched data=0x%02x queued=0 busy=1 write_done=1", $time, i_D);

                    // compute amplitude and length (additive)
                    new_amp = ($signed({1'b0, i_D}) - 8'sd128) * SCALE_FACTOR;
                    mag = (new_amp < 0) ? -new_amp : new_amp;
                    addlen = (mag / K_DIV) + BASE_LEN;
                    sample_ticks_left <= sample_ticks_left + addlen;
                    if (sample_ticks_left > MAX_SAMPLE_TICKS) sample_ticks_left <= MAX_SAMPLE_TICKS;

                    $display("[%0t] [DUT:DATA] compute new_amp=%0d mag=%0d addlen=%0d sample_ticks_left(after)=%0d sample_amp(before)=%0d",
                             $time, new_amp, mag, addlen, sample_ticks_left, sample_amp);

                    // attack ramp setup (original behavior)
                    if (ATTACK_SAMPLES > 0) begin
                        attack_counter <= ATTACK_SAMPLES;
                        delta_amp <= (new_amp - sample_amp) / (ATTACK_SAMPLES);
                    end else begin
                        sample_amp <= new_amp;
                        attack_counter <= 0;
                        delta_amp <= 0;
                    end
                end
            end

            // audio sample-rate handling (after write handling)
            if (audio_cycle_cnt + 1 >= CYCLES_PER_AUDIO_SAMPLE) begin
                // audio tick
                audio_cycle_cnt <= 0;

                // apply attack ramp if in progress
                if (attack_counter > 0) begin
                    sample_amp <= sample_amp + delta_amp;
                    attack_counter <= attack_counter - 1;
                end

                // if samples remaining, emit and decay
                if (sample_ticks_left > 0) begin
                    $display("[%0t] [DUT:AUDIO] AUDIO_TICK sample_amp=%0d new_amp=%0d sample_ticks_left=%0d o_ACC_SIGNED(before)=%0d",
                             $time, sample_amp, new_amp, sample_ticks_left, o_ACC_SIGNED);
                    // strobe indicates fresh audio sample
                    o_ACC_SIGNED_STRB <= 1'b1;
                    sample_ticks_left <= sample_ticks_left - 1;
                    // apply decay to amplitude for next sample
                    sample_amp <= (sample_amp * DECAY_NUM) / DECAY_DEN;
                    if (sample_ticks_left <= 1) sample_amp <= 0;
                    $display("[%0t] [DUT:AUDIO] AUDIO_TICK_POST o_ACC_SIGNED=%0d", $time, o_ACC_SIGNED);
                end
            end else begin
                audio_cycle_cnt <= audio_cycle_cnt + 1;
            end

            // Continuously present current sample_amp (clamped) on o_ACC_SIGNED
            if (sample_amp > 32767) o_ACC_SIGNED <= 32767;
            else if (sample_amp < -32768) o_ACC_SIGNED <= -32768;
            else o_ACC_SIGNED <= sample_amp;
        end
    end

endmodule