#include <stdio.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdlib.h>

#include "ikaopll_wrapper.h"
#include "ym2413_bus.h"
#include "vgm_player.h"

int main(int argc, char** argv)
{
    const char* csv_path = "vgm_data/tests/ym2413_scale_chromatic.vgm.csv";
    if (argc >= 2) {
        csv_path = argv[1];
    }

    printf("IKAOPLL-verilator: YM2413 bus + VGM CSV player\n");
    printf("  CSV: %s\n", csv_path);

    /* Verilated IKAOPLL インスタンス初期化 */
    ikaopll_init();

    /* VCD トレース有効化（全ノード） */
    ikaopll_trace_init("ikaopll_dump.vcd");

    /* phiM_PCEN_n は TB と同様 0 固定 */
    ikaopll_set_phiM_pcen_n(0);

    /* リセットシーケンス */
    ikaopll_reset();

    /* YM2413 バスコンテキスト初期化 */
    ym2413_bus_t bus;
    ym2413_bus_init(&bus);

    /* ACC / Mo ログ開始 */
    ym2413_bus_acc_log_open("acc_log.csv");
    ym2413_bus_mo_log_open("mo_log.csv");

    /* VGM CSV を読み込んでシーケンスを実行 */
    if (vgm_player_run_csv(csv_path, &bus) != 0) {
        fprintf(stderr, "[main] vgm_player_run_csv failed.\n");
        ym2413_bus_mo_log_close();
        ym2413_bus_acc_log_close();
        ikaopll_trace_close();
        ikaopll_release();
        return 1;
    }

    /* ログ終了 */
    ym2413_bus_mo_log_close();
    ym2413_bus_acc_log_close();

    /* g_main_time は 1ps 単位の tick 数 */
    uint64_t sim_ticks = ikaopll_get_sim_time();
    double   sim_sec   = sim_ticks * 1e-12;

    printf("Simulation finished. sim_time = %" PRIu64 " ticks (%.6f s)\n",
           sim_ticks, sim_sec);

    ikaopll_trace_close();
    ikaopll_release();

    return 0;
}