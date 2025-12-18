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
  Other args preserved: first non-option argument is treated as CSV path.
*/

static void print_usage(const char *progname)
{
    printf("Usage: %s [vgm_csv_path] [--vcd [vcd_file]] [--no-csv]\n", progname);
    printf("  If vgm_csv_path is omitted, default vgm_data/tests/ym2413_scale_chromatic.vgm.csv is used.\n");
    printf("  --vcd [file]   : enable VCD output; optional filename (default: ikaopll_dump.vcd)\n");
    printf("  --no-csv       : disable ACC/Mo CSV log output\n");
}

int main(int argc, char** argv)
{
    const char* csv_path = "vgm_data/tests/ym2413_scale_chromatic.vgm.csv";
    bool enable_vcd = false;
    char vcd_filename[256] = "ikaopll_dump.vcd";
    bool enable_csv = true;

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
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else if (argv[i][0] == '-') {
            /* unknown option: ignore or extend as needed */
            fprintf(stderr, "Warning: unknown option '%s' (ignored)\n", argv[i]);
        } else {
            /* first non-option token treated as csv path */
            csv_path = argv[i];
        }
    }

    printf("IKAOPLL-verilator: YM2413 bus + VGM CSV player\n");
    printf("  CSV: %s\n", csv_path);
    if (enable_vcd) {
        printf("  VCD: enabled -> %s\n", vcd_filename);
    } else {
        printf("  VCD: disabled\n");
    }
    printf("  CSV logging: %s\n", enable_csv ? "enabled" : "disabled");

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

    /* ACC / Mo ログ開始（runtime で制御可能） */
    if (enable_csv) {
        ym2413_bus_acc_log_open("acc_log.csv");
        ym2413_bus_mo_log_open("mo_log.csv");
    }

    /* VGM CSV を読み込んでシーケンスを実行 */
    int rv = vgm_player_run_csv(csv_path, &bus);
    if (rv != 0) {
        fprintf(stderr, "[main] vgm_player_run_csv failed.\n");
        if (enable_csv) {
            ym2413_bus_mo_log_close();
            ym2413_bus_acc_log_close();
        }
        if (enable_vcd) {
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