/*
 * vgm_parser.c
 *
 * Implementation of a careful VGM parser that invokes host callbacks for
 * register writes and waits. This file contains ONLY the parsing logic:
 * - vgm_parse_buffer()
 * - vgm_parse_file()
 *
 * Adapter / runtime functions (vgm_player_run_vgm, callback registration,
 * etc.) must live in vgm_player.c to avoid duplicate symbol definitions.
 */

#include "vgm_parser.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Helper for reading little-endian 32-bit from buffer */
static uint32_t read_le_u32_from_buf(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* Table of known fixed-length commands (code, total_length_in_bytes).
 * Mirrors the idea in eseopl3patcher main.c's kKnownFixedCmds. Add entries
 * here as needed so the parser can skip them safely.
 */
typedef struct {
    uint8_t code;
    uint8_t length; /* total length including opcode */
} KnownFixedCmd;

static const KnownFixedCmd kKnownFixedCmds[] = {
    {0xA0, 3}, /* AY8910: opcode + port + value */
    {0xD2, 4}, /* K051649 (example) */
    /* Add more fixed-length non-OPL commands here if needed */
};

/* Find known fixed command spec, or NULL */
static const KnownFixedCmd* find_known_fixed(uint8_t code) {
    for (size_t i = 0; i < sizeof(kKnownFixedCmds)/sizeof(kKnownFixedCmds[0]); ++i) {
        if (kKnownFixedCmds[i].code == code) return &kKnownFixedCmds[i];
    }
    return NULL;
}

/* Map an opcode that indicates a YM-family register write to VGMChipId.
 * For 0x51..0x5C we map specifically.
 */
static VGMChipId opcode_to_chip(uint8_t opcode) {
    switch (opcode) {
    case 0x50: return VGM_CHIP_PSG;
    case 0x51: return VGM_CHIP_YM2413;
    case 0x5A: return VGM_CHIP_YM3812;
    case 0x5B: return VGM_CHIP_YM3526;
    case 0x5C: return VGM_CHIP_Y8950;
    case 0x52: /* OPN-like passthrough */ return VGM_CHIP_OPN;
    case 0x54: case 0x55: case 0x56: case 0x57:
        return VGM_CHIP_OPN;
    default:
        return VGM_CHIP_UNKNOWN;
    }
}

/* Internal core parse routine operating on a buffer. */
int vgm_parse_buffer(const uint8_t *buf, size_t bufsize, const VGMParserCallbacks *cbs, void *user) {
    if (!buf || bufsize < 0x40) return -1;
    if (!cbs) return -1;

    /* Validate VGM header magic */
    if (memcmp(buf, "Vgm ", 4) != 0) {
        return -2;
    }

    /* Compute data offset from header (offset at 0x34, little endian) */
    uint32_t data_offset_field = read_le_u32_from_buf(buf + 0x34);
    /* Per VGM spec: if DataOffset == 0 then use 0x0C (compat) */
    if (data_offset_field == 0) data_offset_field = 0x0C;
    uint32_t header_size = 0x34 + data_offset_field;
    if (header_size < 0x40) header_size = 0x40;
    uint32_t data_start = 0x34 + data_offset_field;
    if (data_start >= bufsize) return -3;

    /* Start parsing commands */
    size_t pos = data_start;
    while (pos < bufsize) {
        uint8_t cmd = buf[pos];

        /* YM2413 (0x51) and other YM-family writes (0x5A/0x5B/0x5C) */
        if (cmd == 0x51 || cmd == 0x5A || cmd == 0x5B || cmd == 0x5C) {
            /* needs 3 bytes: opcode, reg, val */
            if (pos + 2 >= bufsize) {
                /* truncated */
                if (cbs->on_unknown) cbs->on_unknown(user, cmd, (uint32_t)pos);
                break;
            }
            uint8_t reg = buf[pos + 1];
            uint8_t val = buf[pos + 2];
            pos += 3;

            /* peek for immediate post-wait encoded right after a write */
            uint32_t post_wait_samples = 0;
            if (pos < bufsize) {
                uint8_t peek = buf[pos];
                if (peek >= 0x70 && peek <= 0x7F) {
                    post_wait_samples = (peek & 0x0F) + 1;
                    pos += 1;
                } else if (peek == 0x61 && pos + 2 < bufsize) {
                    uint16_t lo = buf[pos + 1];
                    uint16_t hi = buf[pos + 2];
                    post_wait_samples = (uint32_t)(lo | (hi << 8));
                    pos += 3;
                } else if (peek == 0x62) {
                    post_wait_samples = 735; pos += 1;
                } else if (peek == 0x63) {
                    post_wait_samples = 882; pos += 1;
                }
            }

            /* dispatch callback */
            if (cbs->on_reg_write) {
                VGMChipId chip = opcode_to_chip(cmd);
                cbs->on_reg_write(user, chip, reg, val, post_wait_samples);
            }
            continue;
        }

        /* PSG / SN76489 style data (0x50 : single byte payload) */
        if (cmd == 0x50) {
            if (pos + 1 >= bufsize) {
                if (cbs->on_unknown) cbs->on_unknown(user, cmd, (uint32_t)pos);
                break;
            }
            uint8_t data = buf[pos + 1];
            pos += 2;
            if (cbs->on_reg_write) {
                cbs->on_reg_write(user, VGM_CHIP_PSG, 0, data, 0);
            }
            continue;
        }

        /* OPN-like passthroughs that use 3 bytes (opcode, reg, val) */
        if (cmd == 0x52 || (cmd >= 0x54 && cmd <= 0x57)) {
            if (pos + 2 >= bufsize) {
                if (cbs->on_unknown) cbs->on_unknown(user, cmd, (uint32_t)pos);
                break;
            }
            uint8_t reg = buf[pos + 1];
            uint8_t val = buf[pos + 2];
            pos += 3;
            if (cbs->on_reg_write) {
                cbs->on_reg_write(user, VGM_CHIP_OPN, reg, val, 0);
            }
            continue;
        }

        /* Wait short 0x70 - 0x7F: 1..16 samples */
        if (cmd >= 0x70 && cmd <= 0x7F) {
            pos += 1;
            uint32_t samples = (cmd & 0x0F) + 1;
            if (cbs->on_wait) cbs->on_wait(user, samples);
            continue;
        }

        /* Wait 0x61: 16-bit little-endian sample count */
        if (cmd == 0x61) {
            if (pos + 2 >= bufsize) {
                if (cbs->on_unknown) cbs->on_unknown(user, cmd, (uint32_t)pos);
                break;
            }
            uint16_t ws = (uint16_t)(buf[pos + 1] | (buf[pos + 2] << 8));
            pos += 3;
            if (cbs->on_wait) cbs->on_wait(user, (uint32_t)ws);
            continue;
        }

        /* Wait 60Hz / 50Hz */
        if (cmd == 0x62) {
            pos += 1;
            if (cbs->on_wait) cbs->on_wait(user, 735);
            continue;
        }
        if (cmd == 0x63) {
            pos += 1;
            if (cbs->on_wait) cbs->on_wait(user, 882);
            continue;
        }

        /* End of data */
        if (cmd == 0x66) {
            pos += 1;
            if (cbs->on_end) cbs->on_end(user);
            break;
        }

        /* Data block 0x67: read type + 4-byte size + payload */
        if (cmd == 0x67) {
            if (pos + 6 > bufsize) {
                if (cbs->on_unknown) cbs->on_unknown(user, cmd, (uint32_t)pos);
                break;
            }
            uint8_t typeb = buf[pos + 1];
            uint32_t dsize = read_le_u32_from_buf(buf + pos + 2);
            if (pos + 6 + (size_t)dsize > bufsize) {
                if (cbs->on_unknown) cbs->on_unknown(user, cmd, (uint32_t)pos);
                break;
            }
            if (cbs->on_data_block) {
                const uint8_t *data_ptr = buf + pos + 6;
                cbs->on_data_block(user, typeb, data_ptr, dsize);
            }
            pos += 6 + dsize;
            continue;
        }

        /* Data block type 0x68 - skip conservatively */
        if (cmd == 0x68) {
            if (pos + 1 >= bufsize) {
                if (cbs->on_unknown) cbs->on_unknown(user, cmd, (uint32_t)pos);
                break;
            }
            if (cbs->on_unknown) cbs->on_unknown(user, cmd, (uint32_t)pos);
            pos += 1;
            continue;
        }

        /* Known fixed-length commands (safe skip) */
        const KnownFixedCmd *spec = find_known_fixed(cmd);
        if (spec) {
            if (pos + spec->length > bufsize) {
                if (cbs->on_unknown) cbs->on_unknown(user, cmd, (uint32_t)pos);
                break;
            }
            pos += spec->length;
            continue;
        }

        /* Unknown fallback: warn and advance by one byte */
        if (cbs->on_unknown) cbs->on_unknown(user, cmd, (uint32_t)pos);
        pos += 1;
    }

    return 0;
}

/* Read file into memory and call buffer-based parser */
int vgm_parse_file(const char *path, const VGMParserCallbacks *cbs, void *user) {
    if (!path) return -1;
    FILE *f = fopen(path, "rb");
    if (!f) return -2;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return -3; }
    long sz = ftell(f);
    if (sz < 0) { fclose(f); return -4; }
    if (fseek(f, 0, SEEK_SET) != 0) { fclose(f); return -5; }

    uint8_t *buf = (uint8_t*)malloc((size_t)sz);
    if (!buf) { fclose(f); return -6; }
    size_t n = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    if (n != (size_t)sz) {
        free(buf);
        return -7;
    }
    int rc = vgm_parse_buffer(buf, (size_t)sz, cbs, user);
    free(buf);
    return rc;
}