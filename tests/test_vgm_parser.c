/* Run from a scratch/build directory, passing a temporary fixture filename.
 * cc -std=c11 -Wall -Wextra -Werror -Isrc tests/test_vgm_parser.c src/vgm_parser.c -lz -o build/test_vgm_parser
 * build/test_vgm_parser build/parser_clock_fixture.vgm
 */
#include "vgm_parser.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <zlib.h>

typedef struct {
    uint64_t samples;
    unsigned writes, ym_writes, ends, errors, blocks;
    uint64_t ym_time[4];
    uint8_t block_type, block_first;
    uint32_t block_size;
} Observed;

static void on_write(void *p, VGMChipId chip, uint8_t reg, uint8_t value, uint32_t post)
{
    Observed *o = (Observed *)p;
    (void)reg; (void)value;
    assert(post == 0);
    ++o->writes;
    if (chip == VGM_CHIP_YM2413) {
        assert(o->ym_writes < 4);
        o->ym_time[o->ym_writes++] = o->samples;
    }
}
static void on_wait(void *p, uint32_t count) { ((Observed *)p)->samples += count; }
static void on_end(void *p) { ++((Observed *)p)->ends; }
static void on_error(void *p, uint8_t cmd, uint32_t offset)
{
    (void)cmd; (void)offset; ++((Observed *)p)->errors;
}
static void on_block(void *p, uint8_t type, const uint8_t *data, uint32_t size)
{
    Observed *o = (Observed *)p;
    ++o->blocks; o->block_type = type; o->block_size = size;
    o->block_first = size ? data[0] : 0;
}
static const VGMParserCallbacks callbacks = {on_write, on_wait, on_end, on_error, on_block};

static void u32(uint8_t *p, uint32_t x)
{
    p[0] = (uint8_t)x; p[1] = (uint8_t)(x >> 8);
    p[2] = (uint8_t)(x >> 16); p[3] = (uint8_t)(x >> 24);
}
static size_t fixture(uint8_t *buf, const uint8_t *commands, size_t count)
{
    memset(buf, 0, 256); memcpy(buf, "Vgm ", 4);
    u32(buf + 8, 0x171); u32(buf + 0x10, 3579545);
    memcpy(buf + 0x40, commands, count);
    return 0x40 + count;
}
static int parse(const uint8_t *buf, size_t size, Observed *o)
{
    memset(o, 0, sizeof(*o));
    return vgm_parse_buffer(buf, size, &callbacks, o);
}
static void save(const char *path, const uint8_t *buf, size_t size)
{
    FILE *fp = fopen(path, "wb"); assert(fp);
    assert(fwrite(buf, 1, size, fp) == size); assert(fclose(fp) == 0);
}

int main(int argc, char **argv)
{
    uint8_t buf[256]; Observed o; size_t size;
    assert(argc == 2);
    /* Waits remain separate and include all trailing waits before END. */
    const uint8_t timeline[] = {
        0x51,0x10,0x20, 0x70, 0x61,2,0, 0x62, 0x63,
        0x51,0x20,0x19, 0x7f, 0x61,0x44,0xac, 0x66
    };
    size = fixture(buf, timeline, sizeof(timeline));
    assert(parse(buf, size, &o) == 0);
    assert(o.ym_writes == 2 && o.ym_time[0] == 0 && o.ym_time[1] == 1620);
    assert(o.samples == 45736 && o.ends == 1 && o.errors == 0);

    /* A pre-1.50 reserved field must not redirect command parsing. */
    u32(buf + 8, 0x110); u32(buf + 0x34, 0xdeadbeef);
    assert(parse(buf, size, &o) == 0 && o.samples == 45736);
    u32(buf + 8, 0x171);
    assert(parse(buf, size, &o) == VGM_ERROR_HEADER);
    u32(buf + 0x34, 0xffffffff);
    assert(parse(buf, size, &o) == VGM_ERROR_HEADER);
    u32(buf + 0x34, 1);
    assert(parse(buf, size, &o) == VGM_ERROR_HEADER);
    u32(buf + 0x34, 0);
    u32(buf + 8, 0x200);
    assert(parse(buf, size, &o) == VGM_ERROR_HEADER);

    /* The block's marker, type, uint32 size and payload are seven header bytes. */
    const uint8_t blocks[] = {
        0x67,0x66,0x81, 3,0,0,0x80, 0x51,0x66,0xff,
        0x51,0x30,0x10, 0x66
    };
    size = fixture(buf, blocks, sizeof(blocks));
    assert(parse(buf, size, &o) == 0);
    assert(o.blocks == 1 && o.block_type == 0x81 && o.block_size == 3);
    assert(o.block_first == 0x51 && o.ym_writes == 1 && o.ends == 1);
    buf[0x41] = 0;
    assert(parse(buf, size, &o) == VGM_ERROR_HEADER && o.blocks == 0);
    buf[0x41] = 0x66; u32(buf + 0x43, 0x7fffffff);
    assert(parse(buf, size, &o) == VGM_ERROR_TRUNCATED);
    assert(parse(buf, 0x46, &o) == VGM_ERROR_TRUNCATED);

    /* Foreign write payloads cannot be mistaken for OPLL opcodes. */
    const uint8_t foreign[] = {
        0x4f,0x51, 0x50,0x51, 0x53,0x51,0x51, 0x5f,0x51,0x51,
        0xa0,0x51,0x51, 0xd2,0x51,0x51,0x51,
        0xe0,0x51,0x51,0x51,0x51,
        0x80,0x8f, 0x64,0x62,4,0,0x62, 0x64,0x63,5,0,0x63,0x66
    };
    size = fixture(buf, foreign, sizeof(foreign));
    assert(parse(buf, size, &o) == 0 && o.ym_writes == 0 && o.samples == 24);
    assert(o.writes == 3);

    /* Reject unknown, second-OPLL and malformed/truncated commands. */
    const uint8_t invalid[] = {0x01,0x51,0x10,0x20,0x66};
    size = fixture(buf, invalid, sizeof(invalid));
    assert(parse(buf, size, &o) == VGM_ERROR_UNSUPPORTED && o.writes == 0 && o.errors == 1);
    buf[0x40] = 0xa1;
    assert(parse(buf, size, &o) == VGM_ERROR_UNSUPPORTED && o.writes == 0);
    buf[0x40] = 0x51;
    assert(parse(buf, 0x42, &o) == VGM_ERROR_TRUNCATED && o.writes == 0);
    buf[0x40] = 0x61;
    assert(parse(buf, 0x42, &o) == VGM_ERROR_TRUNCATED);
    buf[0x40] = 0x70;
    assert(parse(buf, 0x41, &o) == VGM_ERROR_TRUNCATED && o.samples == 1);
    buf[0x40] = 0x64; buf[0x41] = 0x61;
    assert(parse(buf, 0x44, &o) == VGM_ERROR_UNSUPPORTED);
    assert(vgm_parse_buffer(buf, size, NULL, &o) == VGM_ERROR_ARGUMENT);
    assert(parse(buf, 0x3f, &o) == VGM_ERROR_ARGUMENT);

    /* File clock API accepts single OPLL and rejects flags/zero/bad header. */
    const uint8_t end[] = {0x66};
    size = fixture(buf, end, sizeof(end));
    uint32_t clock = 0;
    save(argv[1], buf, size);
    assert(vgm_read_ym2413_clock(argv[1], &clock) == 0 && clock == 3579545);
    assert(vgm_parse_file(argv[1], &callbacks, &o) == 0);
    u32(buf + 0x10, 4000000); save(argv[1], buf, size);
    assert(vgm_read_ym2413_clock(argv[1], &clock) == 0 && clock == 4000000);
    u32(buf + 0x10, 0x40000000U | 3579545); save(argv[1], buf, size);
    assert(vgm_read_ym2413_clock(argv[1], &clock) == VGM_ERROR_CHIP);
    u32(buf + 0x10, 0x80000000U | 3579545); save(argv[1], buf, size);
    assert(vgm_read_ym2413_clock(argv[1], &clock) == VGM_ERROR_CHIP);
    u32(buf + 0x10, 0); save(argv[1], buf, size);
    assert(vgm_read_ym2413_clock(argv[1], &clock) == VGM_ERROR_CHIP);
    buf[0] = 0x1f; buf[1] = 0x8b; save(argv[1], buf, size);
    assert(vgm_read_ym2413_clock(argv[1], &clock) != 0);
    assert(vgm_read_ym2413_clock(argv[1], NULL) == VGM_ERROR_ARGUMENT);
    size = fixture(buf, end, sizeof(end));
    gzFile packed = gzopen(argv[1], "wb"); assert(packed);
    assert(gzwrite(packed, buf, (unsigned)size) == (int)size);
    assert(gzclose(packed) == Z_OK);
    assert(vgm_read_ym2413_clock(argv[1], &clock) == 0 && clock == 3579545);
    assert(vgm_parse_file(argv[1], &callbacks, &o) == 0);
    FILE *compressed_file = fopen(argv[1], "rb"); assert(compressed_file);
    uint8_t compressed[512];
    size_t compressed_size = fread(compressed, 1, sizeof(compressed), compressed_file);
    assert(fclose(compressed_file) == 0 && compressed_size > 6);
    save(argv[1], compressed, compressed_size - 6);
    assert(vgm_read_ym2413_clock(argv[1], &clock) != 0);
    assert(remove(argv[1]) == 0);
    puts("VGM parser tests passed");
    return 0;
}
