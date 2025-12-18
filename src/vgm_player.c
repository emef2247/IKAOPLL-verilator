/*
 * src/vgm_player.c
 *
 * VGM / CSV player adapter.
 *
 * - vgm_player_run_csv: read CSV (relative-delay format) and drive ym2413_bus.
 * - vgm_player_run_vgm: parse .vgm using vgm_parser and drive ym2413_bus with
 *   the same algorithm as vgm2csv (current_sample -> event delay -> phiM step ->
 *   addr/data writes -> post-wait).
 *
 * Notes:
 * - CSV format expected: delay,reg,data with delay as RELATIVE samples by default.
 * - vgm_player_run_vgm writes an inspection CSV (path.vgm.csv) when possible.
 * - Adapter uses ym2413_bus_step_phiM_cycles_adapter so the bus can attribute
 *   phiM increments to the adapter for debugging.
 */

#include "vgm_player.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>  /* PRIu64 用 */

#include "vgm_parser.h" /* parser for direct VGM parsing */

/* VGM とクロックの関係（CSV と同様の前提） */
static const double EMUCLK_HZ      = 3579545.0;
static const double VGM_RATE       = 44100.0;
static const double EMU_PER_SAMPLE = 3579545.0 / 44100.0;  /* ≒81.2 */
static const double PHIM_PER_SAMPLE_D = EMU_PER_SAMPLE / 4.0; /* ≒20.3 */

/* Convert delay in VGM samples to φM cycles (approx) */
static uint32_t samples_to_phiM(uint32_t delay_samples)
{
    double phiM = (double)delay_samples * PHIM_PER_SAMPLE_D;
    if (phiM <= 0.0) return 0;
    uint32_t r = (uint32_t)(phiM + 0.5); /* round */
    return r;
}

/* parse CSV line helper */
static int parse_csv_line(const char* line, vgm_csv_event_t* ev)
{
    char* tmp = strdup(line);
    if (!tmp) return -1;

    char* p_delay = strtok(tmp, ",");
    char* p_reg   = strtok(NULL, ",");
    char* p_data  = strtok(NULL, ",");
    if (!p_delay || !p_reg || !p_data) {
        free(tmp);
        return -1;
    }

    uint32_t delay = 0;
    if (*p_delay != '\0') {
        delay = (uint32_t)strtoul(p_delay, NULL, 10);
    }

    int is_addr = 0;
    if (strcmp(p_reg, "01") == 0 || strcmp(p_reg, "01\n") == 0) {
        is_addr = 1;
    } else {
        is_addr = 0;
    }

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

/* -------------------------------------------------------------------------
 * CSV player: treat CSV delay as RELATIVE by default (compat with existing tests)
 * ------------------------------------------------------------------------- */
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
    /* skip header */
    if (!fgets(line, sizeof(line), fp)) {
        fprintf(stderr, "[vgm_player] empty CSV: %s\n", path);
        fclose(fp);
        return -1;
    }

    uint64_t total_samples = 0;
    uint64_t total_phiM    = 0;
    uint64_t event_count   = 0;

    /* NOTE: CSV files in this project are relative-delay format by default.
     * We treat ev.delay_samples AS a relative delay (delta) and do not
     * reinterpret it as an absolute timestamp. This keeps behavior
     * compatible with the existing test vectors.
     */
    while (fgets(line, sizeof(line), fp)) {
        if (line[0] == '\n' || line[0] == '\r' || line[0] == '\0') continue;

        vgm_csv_event_t ev;
        if (parse_csv_line(line, &ev) != 0) {
            fprintf(stderr, "[vgm_player] parse error, skipping line: %s", line);
            continue;
        }

        uint32_t delta_samples = (uint32_t)ev.delay_samples;

        uint32_t phiM_delay = samples_to_phiM(delta_samples);
        total_samples += delta_samples;
        total_phiM    += phiM_delay;
        event_count   += 1;

        if (phiM_delay > 0) {
            /* use adapter wrapper so bus can attribute these steps */
            ym2413_bus_step_phiM_cycles_adapter(bus, phiM_delay);
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

/* -------------------------------------------------------------------------
 * Direct VGM parsing adapter: mirrors vgm2csv behavior and also writes a CSV
 * file containing the same delay,reg,data rows for inspection.
 * ------------------------------------------------------------------------- */

/* Adapter context visible to parser callbacks (file scope) */
typedef struct {
    ym2413_bus_t *bus;
    uint64_t current_sample;      /* absolute samples (VGM unit) */
    uint64_t last_output_sample;  /* sample time of last emitted write */
    uint64_t total_samples;
    uint64_t total_phiM;
    uint64_t event_count;
    FILE *csv_fp;                 /* optional CSV output file (nullable) */
} vgmrun_ctx_t;

static vgmrun_ctx_t *g_vgmrun_ctx = NULL;

void vgm_run_vgm_set_global_ctx(void *p) { g_vgmrun_ctx = (vgmrun_ctx_t*)p; }
void vgm_run_vgm_clear_global_ctx(void) { g_vgmrun_ctx = NULL; }

/* step φM cycles for 'samples' VGM-samples */
static void vgmrun_step_phiM(uint32_t samples)
{
    if (!g_vgmrun_ctx || samples == 0) return;
    vgmrun_ctx_t *c = g_vgmrun_ctx;
    uint32_t phiM = samples_to_phiM(samples);
    if (phiM > 0) {
        /* use adapter wrapper so bus attributes these steps to adapter */
        ym2413_bus_step_phiM_cycles_adapter(c->bus, phiM);
        c->total_phiM += phiM;
    }
    c->total_samples += samples;
}

/* parser callbacks */
static void vgmrun_on_wait(void *user, uint32_t samples)
{
    (void)user;
    if (!g_vgmrun_ctx) return;
    vgmrun_ctx_t *c = g_vgmrun_ctx;
    c->current_sample += (uint64_t)samples;
}

static void vgmrun_on_reg_write(void *user, VGMChipId chip,
                                uint8_t reg, uint8_t val,
                                uint32_t post_wait_samples)
{
    (void)user;
    if (!g_vgmrun_ctx) return;
    vgmrun_ctx_t *c = g_vgmrun_ctx;

    /* event_sample is the time (in VGM samples) when the write occurs */
    uint64_t event_sample = c->current_sample;

    if (chip != VGM_CHIP_YM2413) {
        /* For non-YM2413 writes we don't emit addr/data, but must account post-wait */
        if (post_wait_samples) {
            vgmrun_step_phiM(post_wait_samples);
            c->current_sample += (uint64_t)post_wait_samples;
        }
        return;
    }

    /* compute delay in samples since last output */
    uint64_t delay_samples = 0;
    if (event_sample >= c->last_output_sample) {
        delay_samples = event_sample - c->last_output_sample;
    } else {
        delay_samples = 0;
    }

    /* step the bus by delay (converted to φM) */
    if (delay_samples > 0) {
        vgmrun_step_phiM((uint32_t)delay_samples);
    }

    /* perform address and data writes (same order as CSV-based player) */
    ym2413_bus_write_addr(c->bus, reg);
    ym2413_bus_write_data(c->bus, val);
    c->event_count++;

    /* Also write CSV lines (if CSV file open) matching vgm2csv format */
    if (c->csv_fp) {
        /* use PRIu64 for delay */
        fprintf(c->csv_fp, "%" PRIu64 ",01,%02X\n", (uint64_t)delay_samples, reg);
        fprintf(c->csv_fp, "0,00,0x%02X\n", val);
    }

    /* apply post-wait samples (if any) */
    if (post_wait_samples) {
        vgmrun_step_phiM(post_wait_samples);
        c->current_sample += (uint64_t)post_wait_samples;
    }

    /* last_output_sample stays as the write time (event_sample) */
    c->last_output_sample = event_sample;
}

static void vgmrun_on_end(void *user)
{
    (void)user;
    /* nothing special */
}

static void vgmrun_on_unknown(void *user, uint8_t opcode, uint32_t offset)
{
    (void)user;
    if (!g_vgmrun_ctx) return;
    fprintf(stderr, "[vgm_player] WARN: unknown opcode 0x%02X at offset 0x%X\n", opcode, offset);
}

void vgm_run_vgm_register_callbacks(VGMParserCallbacks *out_cbs)
{
    if (!out_cbs) return;
    memset(out_cbs, 0, sizeof(*out_cbs));
    out_cbs->on_reg_write = vgmrun_on_reg_write;
    out_cbs->on_wait = vgmrun_on_wait;
    out_cbs->on_end = vgmrun_on_end;
    out_cbs->on_unknown = vgmrun_on_unknown;
    out_cbs->on_data_block = NULL; /* ignore data blocks for now */
}

/* Helper: build output CSV path by appending ".csv" to input path */
static char *make_vgm_csv_path(const char *vgm_path)
{
    if (!vgm_path) return NULL;
    size_t len = strlen(vgm_path);
    const char *suffix = ".csv";
    size_t outlen = len + strlen(suffix) + 1;
    char *out = (char*)malloc(outlen);
    if (!out) return NULL;
    snprintf(out, outlen, "%s%s", vgm_path, suffix);
    return out;
}

int vgm_player_run_vgm(const char* path, ym2413_bus_t* bus)
{
    if (!path || !bus) {
        fprintf(stderr, "[vgm_player] invalid arguments to vgm_player_run_vgm\n");
        return -1;
    }

    vgmrun_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.bus = bus;
    ctx.current_sample = 0;
    ctx.last_output_sample = 0;
    ctx.total_samples = 0;
    ctx.total_phiM = 0;
    ctx.event_count = 0;
    ctx.csv_fp = NULL;

    /* Attempt to open output CSV path for inspection */
    char *out_csv = make_vgm_csv_path(path);
    if (out_csv) {
        FILE *f = fopen(out_csv, "wb");
        if (f) {
            ctx.csv_fp = f;
            /* write CSV header */
            fprintf(ctx.csv_fp, "delay,reg,data\n");
        } else {
            fprintf(stderr, "[vgm_player] unable to open CSV output: %s (continuing without csv file)\n", out_csv);
        }
        free(out_csv);
    }

    VGMParserCallbacks cbs;
    memset(&cbs, 0, sizeof(cbs));

    /* set global context for callbacks and register them */
    vgm_run_vgm_set_global_ctx(&ctx);
    vgm_run_vgm_register_callbacks(&cbs);

    /* parse file (parser will call our callbacks) */
    int rc = vgm_parse_file(path, &cbs, &ctx);

    /* clear global context */
    vgm_run_vgm_clear_global_ctx();

    /* close CSV if open */
    if (ctx.csv_fp) {
        fclose(ctx.csv_fp);
        ctx.csv_fp = NULL;
    }

    printf("[vgm_player] vgm parse done. events=%" PRIu64 ", total_samples=%" PRIu64 ", total_phiM=%" PRIu64 "\n",
           ctx.event_count, ctx.total_samples, ctx.total_phiM);

    return rc;
}