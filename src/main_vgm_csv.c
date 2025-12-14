#include <stdio.h>
#include <stdint.h>
#include <inttypes.h>

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

    /* VGM CSV を読み込んでシーケンスを実行 */
    if (vgm_player_run_csv(csv_path, &bus) != 0) {
        fprintf(stderr, "[main] vgm_player_run_csv failed.\n");
        ikaopll_trace_close();
        ikaopll_release();
        return 1;
    }

    /* 末尾に少し φM を進めてから最終サンプルを読む */
    ym2413_bus_step_phiM_cycles(&bus, 2000);

    int16_t acc = ikaopll_get_acc_signed();
    int16_t mo  = ikaopll_get_mo_signed();

    printf("[main] ACC_SIGNED = %d, MO_SIGNED = %d (single sample)\n", acc, mo);

    printf("Simulation finished. sim_time = %" PRIu64 "\n", ikaopll_get_sim_time());

    ikaopll_trace_close();
    ikaopll_release();

    return 0;
}

