#include "vgm_player.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>  /* PRIu64 用 */

/* VGM とクロックの関係は vgm_csv_to_vh.py と同じ前提を使う。
   ただし C 側では「何 EMUCLK 進めるか」ではなく、
   「何 φM カウント進めるか」に落とす。

   元スクリプトでは:
     EMUCLK_HZ  = 3_579_545 Hz
     VGM_RATE   = 44_100 Hz
     EMU_PER_SAMPLE ≒ 81.2

   φM は EMUCLK 4 分周とすると:
     φM_PER_SAMPLE = EMU_PER_SAMPLE / 4 ≒ 20.3

   とりあえず丸めて 20 φM / sample としておく。
   精度を追い込みたくなったらここを調整する。
*/
static const double EMUCLK_HZ      = 3579545.0;
static const double VGM_RATE       = 44100.0;
static const double EMU_PER_SAMPLE = 3579545.0 / 44100.0;  /* ≒81.2 */
static const double PHIM_PER_SAMPLE_D = EMU_PER_SAMPLE / 4.0; /* ≒20.3 */

static uint32_t samples_to_phiM(uint32_t delay_samples)
{
    /* 近似的に delay_samples * 20 で φM カウントに変換 */
    double phiM = (double)delay_samples * PHIM_PER_SAMPLE_D;
    if (phiM <= 0.0) return 0;
    uint32_t r = (uint32_t)(phiM + 0.5); /* 四捨五入 */
    return r;
}

/* 行をパースして vgm_csv_event_t に落とす */
static int parse_csv_line(const char* line, vgm_csv_event_t* ev)
{
    /* 形式: delay,reg,data */
    char* tmp = strdup(line);
    if (!tmp) return -1;

    char* p_delay = strtok(tmp, ",");
    char* p_reg   = strtok(NULL, ",");
    char* p_data  = strtok(NULL, ",");
    if (!p_delay || !p_reg || !p_data) {
        free(tmp);
        return -1;
    }

    /* delay */
    uint32_t delay = 0;
    if (*p_delay != '\0') {
        delay = (uint32_t)strtoul(p_delay, NULL, 10);
    }

    /* reg: "01" = address, "00" = data */
    int is_addr = 0;
    if (strcmp(p_reg, "01") == 0 || strcmp(p_reg, "01\n") == 0) {
        is_addr = 1;
    } else {
        /* "00" or others → data フェーズとみなす */
        is_addr = 0;
    }

    /* data: hex ("0E" or "0x0E") */
    unsigned int data_val = 0;
    if (strncasecmp(p_data, "0x", 2) == 0) {
        data_val = (unsigned int)strtoul(p_data, NULL, 16);
    } else {
        data_val = (unsigned int)strtoul(p_data, NULL, 16);
    }

    ev->delay_samples = delay;
    ev->is_addr       = (uint8_t)is_addr;
    ev->data          = (uint8_t)(data_val & 0xFF);

    free(tmp);
    return 0;
}

int vgm_player_run_csv(const char* path, ym2413_bus_t* bus)
{
    if (!path || !bus) {
        fprintf(stderr, "[vgm_player] invalid arguments\n");
        return -1;
    }

    FILE* fp = fopen(path, "r");
    if (!fp) {
        fprintf(stderr, "[vgm_player] cannot open CSV: %s\n", path);
        return -1;
    }

    char line[256];
    /* 1行目はヘッダ (delay,reg,data) のはずなので読み飛ばす */
    if (!fgets(line, sizeof(line), fp)) {
        fprintf(stderr, "[vgm_player] empty CSV: %s\n", path);
        fclose(fp);
        return -1;
    }

    uint64_t total_samples = 0;
    uint64_t total_phiM    = 0;
    uint64_t event_count   = 0;

    while (fgets(line, sizeof(line), fp)) {
        /* 改行だけの行などはスキップ */
        if (line[0] == '\n' || line[0] == '\r' || line[0] == '\0') {
            continue;
        }

        vgm_csv_event_t ev;
        if (parse_csv_line(line, &ev) != 0) {
            fprintf(stderr, "[vgm_player] parse error, skipping line: %s", line);
            continue;
        }

        /* delay_samples → φM カウントに変換 */
        uint32_t phiM_delay = samples_to_phiM(ev.delay_samples);
        total_samples += ev.delay_samples;
        total_phiM    += phiM_delay;
        event_count   += 1;

        if (phiM_delay > 0) {
            ym2413_bus_step_phiM_cycles(bus, phiM_delay);
        }

        if (ev.is_addr) {
            ym2413_bus_write_addr(bus, ev.data);
        } else {
            ym2413_bus_write_data(bus, ev.data);
        }
    }

    fclose(fp);

    printf("[vgm_player] done. events=%" PRIu64 ", total_delay_samples=%" PRIu64
           " (~%.3f s), total_phiM=%" PRIu64 "\n",
           event_count,
           total_samples,
           (double)total_samples / VGM_RATE,
           total_phiM);

    return 0;
}