#include <stdio.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdlib.h>

#include "ikaopll_wrapper.h"
#include "ym2413_bus.h"
#include "vgm_player.h"
// #include "wav_writer.h"   /* いったん WAV 生成は使わない */

/* 将来の WAV 出力用に残しておくが、今は使わない */
#if 0
#define MAX_ACC_SAMPLES  (10 * 44100)  /* 最大 10 秒分くらいのバッファ */

/* VGM 再生後に ACC_STRB を見ながら ACC_SIGNED を収集する（実験用） */
static size_t capture_acc_stream(ym2413_bus_t* bus, int16_t* out, size_t max_samples)
{
    size_t count = 0;
    int    timeout_phiM = 0;

    while (count < max_samples && timeout_phiM < 2000000) {
        /* φM を 1 カウント進める */
        ym2413_bus_step_phiM_cycles(bus, 1);
        timeout_phiM++;

        /* ACC_STRB が立っていれば ACC_SIGNED をサンプル */
        if (ikaopll_get_acc_strb()) {
            out[count++] = ikaopll_get_acc_signed();
        }
    }

    printf("[main] captured %zu ACC samples (timeout_phiM=%d)\n", count, timeout_phiM);
    return count;
}
#endif

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

    /* ★ ここで追加シミュレーションは行わない（ACC キャプチャ無し） */

    /* g_main_time は 1ps 単位の tick 数 */
    uint64_t sim_ticks = ikaopll_get_sim_time();
    double   sim_sec   = sim_ticks * 1e-12;  /* 1ps * ticks */

    printf("Simulation finished. sim_time = %" PRIu64 " ticks (%.6f s)\n",
           sim_ticks, sim_sec);

    ikaopll_trace_close();
    ikaopll_release();

    return 0;
}