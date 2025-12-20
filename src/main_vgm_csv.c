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
  - --vcd [filename]   : enable VCD output, optional filename (default: ikaopll_dump.vcd)
  - --no-csv           : disable ACC / Mo CSV logging (default: enabled)
  - --debug [file]     : enable bus debug logging (optional filename; default: ym2413_bus_calls.log)
  Other args preserved: first non-option argument is treated as CSV or VGM path.
*/

static void print_usage(const char *progname)
{
    printf("Usage: %s [vgm_csv_or_vgm_path] [--vcd [vcd_file]] [--no-csv] [--debug [debug_log]]\n", progname);
    printf("  If path is omitted, default vgm_data/tests/ym2413_scale_chromatic.vgm.csv is used.\n");
    printf("  --vcd [file]   : enable VCD output; optional filename (default: ikaopll_dump.vcd)\n");
    printf("  --no-csv       : disable ACC/Mo CSV log output\n");
    printf("  --debug [file] : enable bus debug logging (default: ym2413_bus_calls.log)\n");
}

static bool has_vgm_extension_or_none(const char *p_filename) {
    size_t len = strlen(p_filename);
    if (len > 4 && strcasecmp(p_filename + len - 4, ".vgm") == 0) return true;
    return false;
}

int main(int argc, char** argv)
{
    const char* csv_path = "vgm_data/tests/ym2413_scale_chromatic.vgm.csv";
    bool enable_vcd = false;
    char vcd_filename[256] = "ikaopll_dump.vcd";
    bool enable_csv = true;
    bool enable_debug = false;
    char debug_filename[256] = "ym2413_bus_calls.log";

    /* Parse arguments simply: accept positional csv_path and options anywhere */
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--vcd") == 0) {
            enable_vcd = true;
            if (i + 1 < argc && argv[i+1][0] != '-') {
                /* next token is filename */
                strncpy(vcd_filename, argv[i+1], sizeof(vcd_filename)-1);
                vcd_filename[sizeof(vcd_filename)-1] = '\0';
                ++i;
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
    bool input_is_vgm = has_vgm_extension_or_none(csv_path);

    printf("IKAOPLL-verilator: YM2413 bus + VGM CSV player\n");
    printf("  INPUT: %s\n", csv_path);
    printf("  Input type: %s\n", input_is_vgm ? "VGM" : "CSV");
    if (enable_vcd) {
        printf("  VCD: enabled -> %s\n", vcd_filename);
    } else {
        printf("  VCD: disabled\n");
    }
    printf("  CSV logging: %s\n", enable_csv ? "enabled (MO value changes only)" : "disabled");
    printf("  Debug log: %s\n", enable_debug ? debug_filename : "disabled");

    /* Verilated IKAOPLL インスタンス初期化 */
    ikaopll_init();

    /* VCD トレースは runtime フラグに従って初期化する */
    if (enable_vcd) {
        ikaopll_trace_init(vcd_filename);
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

    /* ACC / Mo ログ開始（runtime で制御可能）
       NOTE: We only open the MO signal-change log (ikaopll_mo_change_log) here.
       The legacy ym2413_bus_mo_log / acc_log are NOT opened when enable_csv is true.
     */
    if (enable_csv) {
        /* MO 差分ログをオープン（信号変化ログのみ） */
        ikaopll_mo_change_log_open("mo_value_changes.csv");
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
            /* MO 差分ログをクローズ */
            ikaopll_mo_change_log_close();
        }
        if (enable_debug) {
            ym2413_bus_debug_close();
        }
        if (enable_vcd) {
            ikaopll_trace_close();
        }
        ikaopll_release();
        return 1;
    }

    /* ログ終了 */
    if (enable_csv) {
        /* only close the MO change log (we didn't open other logs) */
        ikaopll_mo_change_log_close();
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

    if (enable_vcd) {
        ikaopll_trace_close();
    }
	
    ikaopll_release();

    return 0;
}