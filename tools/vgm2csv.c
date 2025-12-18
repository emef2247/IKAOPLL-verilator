/*
 * vgm2csv.c
 *
 * Simple adapter: parse a VGM file and emit CSV rows equivalent to the
 * repository's existing CSV format used by the test harness.
 *
 * Behavior (matching conversation / existing CSV samples):
 *  - Only produce output for YM2413 register writes (VGM opcode 0x51).
 *    Other opcodes are parsed and skipped by the parser so byte offsets
 *    remain correct.
 *  - For each YM2413 write (opcode 0x51) the parser provides reg and val.
 *    We emit two CSV lines:
 *      <delay>,01,<reg_hex_no_prefix>
 *      0,00,0x<data_hex_with_0x>
 *    where <delay> is the number of VGM samples elapsed since the last
 *    emitted event (VGM sample units, typically 44100-based).
 *  - Parser informs of any immediate post-wait after the write via
 *    post_wait_samples; the adapter advances its internal sample clock
 *    by that amount so subsequent events have correct delays.
 *
 * Usage:
 *   ./vgm2csv input.vgm [out.csv]
 *   If out.csv omitted, CSV is printed to stdout.
 *
 * Build (example):
 *   gcc -std=c11 -O2 -I. src/vgm_parser.c src/vgm2csv.c -o vgm2csv
 *
 * Note: this file depends on the parser interface in src/vgm_parser.h
 * which must be present (and compiled/linked).
 */

#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <string.h>
#include <stdint.h>

#include "vgm_parser.h"

typedef struct {
    FILE *out;
    uint64_t current_sample;      /* absolute sample position (VGM samples) */
    uint64_t last_output_sample;  /* sample position of last emitted event (write) */
    uint64_t events_emitted;
} AdapterCtx;

/* Callback: invoked for register writes */
static void cb_on_reg_write(void *user, VGMChipId chip,
                            uint8_t reg, uint8_t val,
                            uint32_t post_wait_samples)
{
    AdapterCtx *ctx = (AdapterCtx*)user;
    if (!ctx || !ctx->out) return;

    /* We only produce CSV rows for YM2413 (opcode 0x51) as requested.
     * Other chips are ignored here but parser still advances correctly.
     */
    if (chip != VGM_CHIP_YM2413) {
        /* Advance time by post_wait_samples so subsequent events remain correct */
        ctx->current_sample += post_wait_samples;
        return;
    }

    /* event_sample is the current sample time at which this write occurs.
     * This is set by parser (it has already advanced current_sample for
     * preceding wait commands). We must use this sample time as the basis
     * for the CSV 'delay'. Do NOT include post_wait_samples when updating
     * last_output_sample — post_wait affects future events but not the
     * delay associated with this write.
     */
    uint64_t event_sample = ctx->current_sample;

    uint64_t delay;
    if (event_sample >= ctx->last_output_sample) {
        delay = event_sample - ctx->last_output_sample;
    } else {
        delay = 0;
    }

    /* Emit two-line format:
     *  <delay>,01,<reg_hex_without_0x>
     *  0,00,0x<val_hex>
     */
    fprintf(ctx->out, "%" PRIu64 ",01,%02X\n", delay, reg);
    fprintf(ctx->out, "0,00,0x%02X\n", val);

    /* After emitting, advance the parser time by post_wait_samples so the
     * next event's event_sample will include this wait. However, last_output_sample
     * should remain equal to the time of this write (event_sample).
     */
    if (post_wait_samples) {
        ctx->current_sample += (uint64_t)post_wait_samples;
    }

    /* Update last output to the write time (not including post-wait) */
    ctx->last_output_sample = event_sample;

    ctx->events_emitted++;
}

static void cb_on_wait(void *user, uint32_t samples)
{
    AdapterCtx *ctx = (AdapterCtx*)user;
    if (!ctx) return;
    ctx->current_sample += (uint64_t)samples;
}

static void cb_on_end(void *user)
{
    /* nothing special to do for CSV generation */
    (void)user;
}

static void cb_on_unknown(void *user, uint8_t opcode, uint32_t offset)
{
    /* Log a terse warning to stderr for diagnostic purposes. Parsing
     * keeps running; unknown opcodes are forwarded/skip-1 by the parser.
     */
    (void)user;
    fprintf(stderr, "[vgm2csv] WARN: unknown opcode 0x%02X at offset 0x%X\n", opcode, offset);
}

static void cb_on_data_block(void *user, uint8_t type_byte, const uint8_t *data_ptr, uint32_t data_size)
{
    /* We don't need to emit data blocks to CSV for now; just ignore.
     * If desired later, we could extract instrument data here.
     */
    (void)user; (void)type_byte; (void)data_ptr; (void)data_size;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "Usage: %s input.vgm [out.csv]\n", argv[0]);
        return 1;
    }

    const char *inpath = argv[1];
    FILE *out = stdout;
    int close_out = 0;
    if (argc >= 3) {
        out = fopen(argv[2], "wb");
        if (!out) {
            perror("fopen(out)");
            return 1;
        }
        close_out = 1;
    }

    /* Print CSV header (match repo's CSV) */
    fprintf(out, "delay,reg,data\n");

    AdapterCtx ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.out = out;
    ctx.current_sample = 0;
    ctx.last_output_sample = 0;
    ctx.events_emitted = 0;

    VGMParserCallbacks cbs;
    memset(&cbs, 0, sizeof(cbs));
    cbs.on_reg_write = cb_on_reg_write;
    cbs.on_wait = cb_on_wait;
    cbs.on_end = cb_on_end;
    cbs.on_unknown = cb_on_unknown;
    cbs.on_data_block = cb_on_data_block;

    int rc = vgm_parse_file(inpath, &cbs, &ctx);
    if (rc != 0) {
        fprintf(stderr, "[vgm2csv] Parsing failed (rc=%d)\n", rc);
        if (close_out) fclose(out);
        return 2;
    }

    if (close_out) fclose(out);

    fprintf(stderr, "[vgm2csv] Done. Events emitted: %" PRIu64 "\n", ctx.events_emitted);
    return 0;
}

