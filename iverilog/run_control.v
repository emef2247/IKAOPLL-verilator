// run_control.v
// Controls VCD dump and simulation timeout via +plusargs passed to vvp.
// Instantiated from within the testbench and given the clock as an input.

module run_control (
    input wire clk
);
    integer timeout;
    integer i;
    reg do_dump;

    initial begin
        // default
        timeout = 100000;
        do_dump = 0;

        if ($value$plusargs("TIMEOUT=%d", timeout)) begin
            $display("[run_control] TIMEOUT set to %0d posedges", timeout);
        end else begin
            $display("[run_control] TIMEOUT not provided, using default %0d posedges", timeout);
        end

        if ($test$plusargs("DUMP")) begin
            do_dump = 1;
            $display("[run_control] DUMP requested, will open dump.vcd (full tree).");
        end else begin
            $display("[run_control] DUMP not requested.");
        end

        if (do_dump) begin
            $dumpfile("dump.vcd");
            // dump the entire design tree (be careful: can be huge)
            $dumpvars(0);
        end

        // wait timeout posedges of the provided clock
        for (i = 0; i < timeout; i = i + 1) begin
            @(posedge clk);
        end

        $display("[run_control] timeout reached (%0d posedges), finishing at $time=%0t", timeout, $time);
        $finish;
    end
endmodule