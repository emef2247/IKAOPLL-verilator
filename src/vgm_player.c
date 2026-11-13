#include "vgm_player.h"
#include "vgm_parser.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

typedef struct {
    ym2413_bus_t* bus;
    uint64_t sample;
    uint64_t last_output_sample;
    uint64_t events;
    FILE* csv;
} player_t;
static void on_wait(void* user, uint32_t samples) {
    player_t* p = (player_t*)user; p->sample += samples;
}
static void on_write(void* user, VGMChipId chip, uint8_t reg, uint8_t value, uint32_t post_wait) {
    player_t* p = (player_t*)user;
    if (chip == VGM_CHIP_YM2413) {
        ym2413_bus_wait_until_sample(p->bus, p->sample);
        ym2413_bus_write_addr(p->bus, reg); ym2413_bus_write_data(p->bus, value);
        if (p->csv) fprintf(p->csv, "%" PRIu64 ",01,%02X\n0,00,%02X\n",
                            p->sample - p->last_output_sample, reg, value);
        p->last_output_sample = p->sample; ++p->events;
    }
    p->sample += post_wait;
}
static void on_unknown(void* user, uint8_t opcode, uint32_t offset) {
    (void)user; fprintf(stderr, "Unsupported or malformed VGM command 0x%02X at 0x%X\n",opcode,offset);
}
int vgm_player_run_vgm(const char* path, ym2413_bus_t* bus, const char* event_csv_path) {
    player_t p; memset(&p, 0, sizeof(p)); p.bus = bus;
    if (event_csv_path) {
        p.csv = fopen(event_csv_path, "w");
        if (!p.csv) return -1;
        fprintf(p.csv, "delay,reg,data\n");
    }
    VGMParserCallbacks cb; memset(&cb, 0, sizeof(cb));
    cb.on_reg_write = on_write; cb.on_wait = on_wait; cb.on_unknown = on_unknown;
    const int rc = vgm_parse_file(path, &cb, &p);
    if (!rc) {
        // This includes all waits after the last write, including silence-only VGM.
        ym2413_bus_wait_until_sample(bus, p.sample);
        if (p.csv && p.sample > p.last_output_sample)
            fprintf(p.csv, "%" PRIu64 ",wait,00\n",p.sample - p.last_output_sample);
    }
    if (p.csv) fclose(p.csv);
    printf("VGM events=%" PRIu64 ", timeline=%" PRIu64 " samples (%.6f s)\n",p.events,p.sample,p.sample/44100.0);
    return rc;
}

