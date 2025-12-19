
#include <stdio.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "ikaopll_wrapper.h"
#include "ym2413_bus.h"
#include "vgm_player.h"

/*
 Minimal runtime flag support:
  - --vcd [filename]   : enable VCD output (legacy)
  - --fst [filename]   : enable FST trace output (shorthand)
  - --trace-fmt fmt    : set trace format (vcd|fst), enables trace; optional filename controlled by --vcd/--fst arg
  - --no-csv           : disable ACC / Mo CSV logging (default: disabled now)
  - --debug [file]     : enable bus debug logging (optional filename; default: ym2413_bus_calls.log)
  - --fst-file [filename] : (note) this is NOT the trace format; kept for backward compat (unused here)
 Other args preserved: first non-option argument is treated as CSV or VGM path.
*/

static void print_usage(const char *progname)
{
    printf("Usage: %s [vgm_csv_or_vgm_path] [--vcd [vcd_file]] [--fst [trace_file]] [--trace-fmt vcd|fst] [--no-csv] [--debug [debug_log]] [--fst-file [fst_file]]\n", progname);
    printf("  If path is omitted, default vgm_data/tests/ym2413_scale_chromatic.vgm.csv is used.\n");
    printf("  --vcd [file]   : enable VCD output; optional filename (default: ikaopll_dump.vcd)\n");
    printf("  --fst [file]   : enable FST trace output; optional filename (default: ikaopll_dump.fst)\n");
    printf("  --trace-fmt    : set trace format (vcd|fst); enables trace\n");
    printf("  --no-csv       : disable ACC/Mo CSV log output (default: disabled)\n");
    printf("  --debug [file] : enable bus debug logging (default: ym2413_bus_calls.log)\n");
}

static bool has_vgm_extension_or_none(const char *p_filename) {
    size_t len = strlen(p_filename);
    if (len > 4 && strcasecmp(p_filename + len - 4, ".vgm") == 0) return true;
    if (len > 4 && strcasecmp(p_filename + len - 4, ".vgm") != 0) return false;
    return false;
}

int main(int argc, char** argv)
{
    const char* csv_path = "vgm_data/tests/ym2413_scale_chromatic.vgm.csv";
    bool enable_trace = false;
    char trace_filename[256] = "ikaopll_dump.vcd";

    /* Changed default: disable ACC/Mo CSV logging by default (user requested) */
    bool enable_csv = false;

    bool enable_debug = false;
    char debug_filename[256] = "ym2413_bus_calls.log";

    /* New option: request compressed audio export (placeholder) */
    bool enable_fst_request = false;
    char fst_filename[256] = "audio_samples.fst";

    char trace_fmt[16] = "vcd";

    /* Parse arguments simply: accept positional csv_path and options anywhere */
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--vcd") == 0) {
            enable_trace = true;
            strncpy(trace_fmt, "vcd", sizeof(trace_fmt)-1);
            if (i + 1 < argc && argv[i+1][0] != '-') {
                strncpy(trace_filename, argv[i+1], sizeof(trace_filename)-1);
                trace_filename[sizeof(trace_filename)-1] = '\0';
                ++i;
            }
        } else if (strcmp(argv[i], "--fst") == 0) {
            enable_trace = true;
            strncpy(trace_fmt, "fst", sizeof(trace_fmt)-1);
            /* default filename for fst */
            strncpy(trace_filename, "ikaopll_dump.fst", sizeof(trace_filename)-1);
            if (i + 1 < argc && argv[i+1][0] != '-') {
                strncpy(trace_filename, argv[i+1], sizeof(trace_filename)-1);
                trace_filename[sizeof(trace_filename)-1] = '\0';
                ++i;
            }
        } else if (strcmp(argv[i], "--trace-fmt") == 0) {
            if (i + 1 < argc) {
                strncpy(trace_fmt, argv[i+1], sizeof(trace_fmt)-1);
                trace_fmt[sizeof(trace_fmt)-1] = '\0';
                ++i;
                enable_trace = true;
            }
        } else if (strcmp(argv[i], "--no-csv") == 0) {
            enable_csv = false;
        } else if (strcmp(argv[i], "--debug") == 0) {
            enable_debug = true;
            if (i + 1 < argc && argv[i+1][0] != '-') {
                strncpy(debug_filename, argv[i+1], sizeof(debug_filename)-1);
                debug_filename[sizeof(debug_filename)-1] = '\0';
                ++i;
            }
        } else if (strcmp(argv[i], "--fst-file") == 0) {
            enable_fst_request = true;
            if (i + 1 < argc && argv[i+1][0] != '-') {
                strncpy(fst_filename, argv[i+1], sizeof(fst_filename)-1);
                fst_filename[sizeof(fst_filename)-1] = '\0';
                ++i;
            }
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else if (argv[i][0] == '-') {
            /* unknown option: ignore or extend as needed */
            fprintf(stderr, "Warning: unknown option '%s' (ignored)\n", argv[i]);
        } else {
            /* first non-option token treated as csv or vgm path */
            csv_path = argv[i];
        }
    }

    /* Determine input type for display */
    bool input_is_vgm = strstr(csv_path, ".vgm") != NULL;

    printf("IKAOPLL-verilator: YM2413 bus + VGM CSV player\n");
    printf("  INPUT: %s\n", csv_path);
    printf("  Input type: %s\n", input_is_vgm ? "VGM" : "CSV");
    if (enable_trace) {
        printf("  Trace: enabled -> %s (format=%s)\n", trace_filename, trace_fmt);
    } else {
        printf("  Trace: disabled\n");
    }
    printf("  CSV logging: %s\n", enable_csv ? "enabled" : "disabled (default)");
    printf("  Debug log: %s\n", enable_debug ? debug_filename : "disabled");
    printf("  FST request: %s\n", enable_fst_request ? fst_filename : "disabled");

    /* Verilated IKAOPLL インスタンス初期化 */
    ikaopll_init();

    /* Trace init if requested */
    if (enable_trace) {
        /* note: ikaopll_trace_init takes filename and format string */
        if (ikaopll_trace_init(trace_filename, trace_fmt) != 0) {
            fprintf(stderr, "[main] Warning: trace init failed; continuing without trace\n");
        }
    }

    /* phiM_PCEN_n は TB と同様 0 固定 */
    ikaopll_set_phiM_pcen_n(0);

    /* リセットシーケンス */
    ikaopll_reset();

    /* YM2413 バスコンテキスト初期化 */
    ym2413_bus_t bus;
    ym2413_bus_init(&bus);

    /* Debug log open if requested */
    if (enable_debug) {
        ym2413_bus_debug_open(debug_filename);
    }

    /* ACC / Mo ログ開始（runtime で制御可能） */
    if (enable_csv) {
        ym2413_bus_acc_log_open("acc_log.csv");
        ym2413_bus_mo_log_open("mo_log.csv");
    }

    /* VGM CSV または VGM を読み込んでシーケンスを実行 */
    int rv = 0;
    if (input_is_vgm) {
        /* Input ends with .vgm -> parse VGM directly */
        rv = vgm_player_run_vgm(csv_path, &bus);
    } else {
        /* Treat as CSV */
        rv = vgm_player_run_csv(csv_path, &bus);
    }

    if (rv != 0) {
        fprintf(stderr, "[main] vgm_player run failed.\n");
        if (enable_csv) {
            ym2413_bus_mo_log_close();
            ym2413_bus_acc_log_close();
        }
        if (enable_debug) {
            ym2413_bus_debug_close();
        }
        if (enable_trace) {
            ikaopll_trace_close();
        }
        ikaopll_release();
        return 1;
    }

    /* ログ終了 */
    if (enable_csv) {
        ym2413_bus_mo_log_close();
        ym2413_bus_acc_log_close();
    }

    /* close debug log if open */
    if (enable_debug) {
        ym2413_bus_debug_close();
    }

    /* g_main_time は 1ps 単位の tick 数 */
    uint64_t sim_ticks = ikaopll_get_sim_time();
    double   sim_sec   = sim_ticks * 1e-12;

    printf("Simulation finished. sim_time = %" PRIu64 " ticks (%.6f s)\n",
           sim_ticks, sim_sec);

    /* close trace if open */
    if (enable_trace) {
        ikaopll_trace_close();
    }
    ikaopll_release();

    /* Post-run: handle requested fst (placeholder) */
    if (enable_fst_request) {
        fprintf(stderr, "[main] FST audio request specified (%s) — audio conversion not implemented in main (use tools/csv_to_fst.py)\n", fst_filename);
    }

    return 0;
}