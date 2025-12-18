/*
 * vgm_parser.h
 *
 * VGM parser that calls back into the host (simulator/player) for
 * register writes and wait events. The parser is designed to be a
 * faithful, careful reimplementation of the VGM command handling
 * approach used in eseopl3patcher (src/main.c) so that byte offsets
 * and skips match precisely and do not desynchronize streamed parsing.
 *
 * The parser itself is agnostic about how the host consumes events;
 * the host must provide callbacks to receive writes/waits/end/unknown.
 *
 * Usage:
 *   VGMParserCallbacks cb = { ... };
 *   int rc = vgm_parse_file(path, &cb, userptr);
 *
 * The parser will:
 *  - parse header and find DataOffset/data start
 *  - honor loop offset fields (reads them for completeness; loop behaviour
 *    is not enforced by the parser itself)
 *  - recognize common VGM opcodes (reg writes, waits, data blocks)
 *  - for fixed-length unknown-but-known opcodes it will skip the correct
 *    number of bytes (kKnownFixedCmds table)
 *  - for variable-length data blocks (0x67) it will read the length field
 *    and skip exactly that many bytes
 *  - for completely unknown opcodes not in the fixed table it will
 *    issue an unknown callback and advance by 1 byte (safe forward)
 *
 * This header and parser intentionally do not directly couple to a
 * particular YM/OPL bus API. Callbacks let the main program decide how to
 * map events (e.g., to the existing CSV-based player API or to direct
 * IKAOPLL calls).
 */

#ifndef VGM_PARSER_H
#define VGM_PARSER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Chip identifiers for callbacks (subset relevant here) */
typedef enum {
    VGM_CHIP_UNKNOWN = 0,
    VGM_CHIP_YM2413,
    VGM_CHIP_YM3812,
    VGM_CHIP_YM3526,
    VGM_CHIP_Y8950,
    VGM_CHIP_PSG, /* SN76489 / PSG type */
    VGM_CHIP_OPN, /* OPN family passthrough (generic) */
} VGMChipId;

/* Callback set the host must provide */
typedef struct VGMParserCallbacks {
    /* Called when a register write is parsed.
     * user: user-supplied pointer
     * chip: VGMChipId
     * reg, val: register and value bytes from VGM
     * post_wait_samples: if the command is immediately followed by an
     *                    embedded wait (VGM often places a wait opcode
     *                    right after a register write), that number of
     *                    samples is returned here. The host may choose
     *                    to handle the write+wait together or separate them.
     */
    void (*on_reg_write)(void *user, VGMChipId chip,
                         uint8_t reg, uint8_t val,
                         uint32_t post_wait_samples);

    /* Called when a wait command is parsed.
     * user: user-supplied pointer
     * samples: number of audio samples to wait (e.g., 735, 882, or 1..16)
     */
    void (*on_wait)(void *user, uint32_t samples);

    /* Called when an end-of-data (0x66) is seen */
    void (*on_end)(void *user);

    /* Called for unknown opcodes (one byte forwarded). Useful for logging.
     * user: user pointer
     * opcode: the unknown opcode
     * offset: file offset where the opcode was found
     */
    void (*on_unknown)(void *user, uint8_t opcode, uint32_t offset);

    /* Optional: called when a data-block is encountered (0x67).
     * type_byte: the data block type byte (following 0x67)
     * data_ptr: pointer to data payload (may be NULL if host doesn't need it).
     * data_size: size of the payload in bytes.
     *
     * If this callback is NULL, parser will still skip the block safely.
     */
    void (*on_data_block)(void *user, uint8_t type_byte,
                          const uint8_t *data_ptr, uint32_t data_size);
} VGMParserCallbacks;

/* Parse the given VGM file path and invoke callbacks as events are parsed.
 * Returns 0 on success, non-zero on error.
 *
 * The parser performs bounds-checking and avoids overruns. It reads
 * the VGM header to find the data offset and loop offset and then
 * iterates through the data region until EOF or 0x66 end command.
 */
int vgm_parse_file(const char *path, const VGMParserCallbacks *cbs, void *user);

/* Parse a memory buffer (already read) with given size. Same semantics. */
int vgm_parse_buffer(const uint8_t *buf, size_t bufsize, const VGMParserCallbacks *cbs, void *user);

#ifdef __cplusplus
}
#endif

#endif /* VGM_PARSER_H */

