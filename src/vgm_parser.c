/* VGM command decoder. Unsupported/truncated commands are errors: advancing a
 * byte at a time can interpret command payloads as YM2413 register writes. */
#include "vgm_parser.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

static int load_gzip(const char *path, uint8_t **out, size_t *size)
{
    gzFile fp = gzopen(path, "rb");
    if (!fp) return VGM_ERROR_IO;
    uint8_t chunk[65536];
    uint8_t *buf = NULL;
    size_t used = 0, capacity = 0;
    int count;
    while ((count = gzread(fp, chunk, sizeof(chunk))) > 0) {
        if ((size_t)count > SIZE_MAX - used) { free(buf); gzclose(fp); return VGM_ERROR_IO; }
        const size_t needed = used + (size_t)count;
        if (needed > capacity) {
            size_t next = capacity ? capacity : sizeof(chunk);
            while (next < needed) {
                if (next > SIZE_MAX / 2) { next = needed; break; }
                next *= 2;
            }
            uint8_t *grown = (uint8_t *)realloc(buf, next);
            if (!grown) { free(buf); gzclose(fp); return VGM_ERROR_IO; }
            buf = grown; capacity = next;
        }
        memcpy(buf + used, chunk, (size_t)count);
        used = needed;
    }
    int error = Z_OK;
    (void)gzerror(fp, &error);
    const int closed = gzclose(fp);
    if (count < 0 || (error != Z_OK && error != Z_STREAM_END) || closed != Z_OK) {
        free(buf); return VGM_ERROR_IO;
    }
    if (used < 0x40) { free(buf); return VGM_ERROR_HEADER; }
    *out = buf; *size = used;
    return 0;
}

static uint32_t read_le_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint32_t read_le_u16(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8);
}

static int data_start(const uint8_t *buf, size_t size, size_t *start)
{
    if (!buf || size < 0x40 || !start) return VGM_ERROR_ARGUMENT;
    if (memcmp(buf, "Vgm ", 4) != 0) return VGM_ERROR_HEADER;
    uint32_t version = read_le_u32(buf + 8);
    if (version < 0x100 || version > 0x171) return VGM_ERROR_HEADER;
    /* Before 1.50, 0x34 is reserved, not a data offset. */
    uint32_t offset = version >= 0x150 ? read_le_u32(buf + 0x34) : 0;
    if (!offset) offset = 0x0c;
    uint64_t absolute = 0x34ULL + offset;
    if (absolute < 0x40 || absolute >= size) return VGM_ERROR_HEADER;
    *start = (size_t)absolute;
    return 0;
}

static int load_file(const char *path, uint8_t **out, size_t *size)
{
    if (!path || !out || !size) return VGM_ERROR_ARGUMENT;
    FILE *fp = fopen(path, "rb");
    if (!fp) return VGM_ERROR_IO;
    const int first = fgetc(fp), second = fgetc(fp);
    if (first == 0x1f && second == 0x8b) {
        fclose(fp);
        return load_gzip(path, out, size);
    }
    if (fseek(fp, 0, SEEK_END) != 0) { fclose(fp); return VGM_ERROR_IO; }
    long length = ftell(fp);
    if (length < 0 || fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp); return VGM_ERROR_IO;
    }
    if (length < 0x40) { fclose(fp); return VGM_ERROR_HEADER; }
    uint8_t *buf = (uint8_t *)malloc((size_t)length);
    if (!buf) { fclose(fp); return VGM_ERROR_IO; }
    size_t count = fread(buf, 1, (size_t)length, fp);
    int close_result = fclose(fp);
    if (count != (size_t)length || close_result != 0) {
        free(buf); return VGM_ERROR_IO;
    }
    *out = buf;
    *size = count;
    return 0;
}

int vgm_read_ym2413_clock(const char *path, uint32_t *clock_hz)
{
    if (!clock_hz) return VGM_ERROR_ARGUMENT;
    uint8_t *buf = NULL;
    size_t size = 0, start = 0;
    int rc = load_file(path, &buf, &size);
    if (rc != 0) return rc;
    rc = data_start(buf, size, &start);
    if (rc == 0) {
        uint32_t clock = read_le_u32(buf + 0x10);
        /* This harness implements one YM2413, not dual OPLL or VRC7. */
        if (!clock || (clock & 0xc0000000U)) rc = VGM_ERROR_CHIP;
        else *clock_hz = clock;
    }
    free(buf);
    return rc;
}

static VGMChipId opcode_to_chip(uint8_t cmd)
{
    switch (cmd) {
    case 0x51: return VGM_CHIP_YM2413;
    case 0x5a: return VGM_CHIP_YM3812;
    case 0x5b: return VGM_CHIP_YM3526;
    case 0x5c: return VGM_CHIP_Y8950;
    case 0x52: case 0x53: case 0x54: case 0x55: case 0x56: case 0x57:
        return VGM_CHIP_OPN;
    default: return VGM_CHIP_UNKNOWN;
    }
}

static int command_error(const VGMParserCallbacks *cbs, void *user,
                         uint8_t cmd, size_t pos, int rc)
{
    if (cbs->on_unknown) cbs->on_unknown(user, cmd, (uint32_t)pos);
    return rc;
}

int vgm_parse_buffer(const uint8_t *buf, size_t size,
                     const VGMParserCallbacks *cbs, void *user)
{
    if (!cbs) return VGM_ERROR_ARGUMENT;
    size_t pos = 0;
    int rc = data_start(buf, size, &pos);
    if (rc != 0) return rc;
    uint32_t wait_62 = 735, wait_63 = 882;

    while (pos < size) {
        uint8_t cmd = buf[pos];
        size_t remaining = size - pos;
        if (cmd >= 0x51 && cmd <= 0x5f) {
            if (remaining < 3)
                return command_error(cbs, user, cmd, pos, VGM_ERROR_TRUNCATED);
            if (cbs->on_reg_write)
                cbs->on_reg_write(user, opcode_to_chip(cmd), buf[pos + 1], buf[pos + 2], 0);
            pos += 3;
        } else if (cmd == 0x50 || cmd == 0x4f) {
            if (remaining < 2)
                return command_error(cbs, user, cmd, pos, VGM_ERROR_TRUNCATED);
            if (cmd == 0x50 && cbs->on_reg_write)
                cbs->on_reg_write(user, VGM_CHIP_PSG, 0, buf[pos + 1], 0);
            pos += 2;
        } else if (cmd >= 0x70 && cmd <= 0x8f) {
            /* 0x8n is a foreign YM2612 DAC write followed by n samples. */
            uint32_t samples = (cmd & 0x0f) + (cmd < 0x80 ? 1u : 0u);
            if (cbs->on_wait) cbs->on_wait(user, samples);
            ++pos;
        } else if (cmd == 0x61) {
            if (remaining < 3)
                return command_error(cbs, user, cmd, pos, VGM_ERROR_TRUNCATED);
            if (cbs->on_wait) cbs->on_wait(user, read_le_u16(buf + pos + 1));
            pos += 3;
        } else if (cmd == 0x62 || cmd == 0x63) {
            if (cbs->on_wait) cbs->on_wait(user, cmd == 0x62 ? wait_62 : wait_63);
            ++pos;
        } else if (cmd == 0x64) {
            if (remaining < 4)
                return command_error(cbs, user, cmd, pos, VGM_ERROR_TRUNCATED);
            if (buf[pos + 1] == 0x62) wait_62 = read_le_u16(buf + pos + 2);
            else if (buf[pos + 1] == 0x63) wait_63 = read_le_u16(buf + pos + 2);
            else return command_error(cbs, user, cmd, pos, VGM_ERROR_UNSUPPORTED);
            pos += 4;
        } else if (cmd == 0x66) {
            if (cbs->on_end) cbs->on_end(user);
            return 0;
        } else if (cmd == 0x67) {
            if (remaining < 7)
                return command_error(cbs, user, cmd, pos, VGM_ERROR_TRUNCATED);
            if (buf[pos + 1] != 0x66)
                return command_error(cbs, user, cmd, pos, VGM_ERROR_HEADER);
            uint32_t length = read_le_u32(buf + pos + 3) & 0x7fffffffU;
            if ((size_t)length > remaining - 7)
                return command_error(cbs, user, cmd, pos, VGM_ERROR_TRUNCATED);
            if (cbs->on_data_block)
                cbs->on_data_block(user, buf[pos + 2], buf + pos + 7, length);
            pos += 7 + (size_t)length;
        } else if (cmd == 0xa0 || cmd == 0xd2 || cmd == 0xe0) {
            size_t length = cmd == 0xa0 ? 3 : (cmd == 0xd2 ? 4 : 5);
            if (remaining < length)
                return command_error(cbs, user, cmd, pos, VGM_ERROR_TRUNCATED);
            pos += length;
        } else {
            /* Includes 0xa1: a second OPLL cannot be rendered by this harness. */
            return command_error(cbs, user, cmd, pos, VGM_ERROR_UNSUPPORTED);
        }
    }
    /* A valid stream terminates with 0x66, not with an incomplete file. */
    return VGM_ERROR_TRUNCATED;
}

int vgm_parse_file(const char *path, const VGMParserCallbacks *cbs, void *user)
{
    uint8_t *buf = NULL;
    size_t size = 0;
    int rc = load_file(path, &buf, &size);
    if (rc != 0) return rc;
    rc = vgm_parse_buffer(buf, size, cbs, user);
    free(buf);
    return rc;
}
