/*
 * vgm_parser.c
 *
 * レジスタ書き込みやウェイトに対してホスト側のコールバックを呼び出す、
 * 慎重な VGM パーサの実装。
 * このファイルには **パース処理のロジックのみ** を含む：
 * - vgm_parse_buffer()
 * - vgm_parse_file()
 *
 * アダプタやランタイム関数（vgm_player_run_vgm やコールバック登録など）は、
 * シンボルの重複定義を避けるために vgm_player.c に実装すること。
 */
#include "vgm_parser.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* バッファからリトルエンディアンの 32 ビット値を読み取るヘルパー */
static uint32_t read_le_u32_from_buf(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

 /* 既知の固定長コマンドのテーブル（コード, 全体のバイト長）。
 * eseopl3patcher の main.c にある kKnownFixedCmds の考え方を踏襲。
 * パーサが安全にスキップできるよう、必要に応じてここにエントリを追加すること。
 */

typedef struct {
    uint8_t code;
    uint8_t length; /* total length including opcode */
} KnownFixedCmd;

static const KnownFixedCmd kKnownFixedCmds[] = {
    {0xA0, 3}, /* AY8910: opcode + port + value */
    {0xD2, 4}, /* K051649 (example) */
	/* 必要に応じて、固定長の非 OPL コマンドをここに追加すること */
};

/* 指定された固定長コマンドの情報を探す。見つからなければ NULL を返す */
static const KnownFixedCmd* find_known_fixed(uint8_t code) {
    for (size_t i = 0; i < sizeof(kKnownFixedCmds)/sizeof(kKnownFixedCmds[0]); ++i) {
        if (kKnownFixedCmds[i].code == code) return &kKnownFixedCmds[i];
    }
    return NULL;
}

/* YM 系レジスタ書き込みを示すオペコードを VGMChipId にマッピングする。
 * 0x51〜0x5C は個別にマッピングされる。
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

/* バッファ上で動作する内部のコアパース処理ルーチン */
int vgm_parse_buffer(const uint8_t *buf, size_t bufsize, const VGMParserCallbacks *cbs, void *user) {
    if (!buf || bufsize < 0x40) return -1;
    if (!cbs) return -1;

	/* VGM ヘッダーのマジック値を検証する */
    if (memcmp(buf, "Vgm ", 4) != 0) {
        return -2;
    }

	/* ヘッダーからデータオフセットを計算する（0x34 の位置にあるリトルエンディアン値） */
    uint32_t data_offset_field = read_le_u32_from_buf(buf + 0x34);
	/* VGM仕様に従い、DataOffset が 0 の場合は互換性のために 0x0C を使用する */
    if (data_offset_field == 0) data_offset_field = 0x0C;
    uint32_t header_size = 0x34 + data_offset_field;
    if (header_size < 0x40) header_size = 0x40;
    uint32_t data_start = 0x34 + data_offset_field;
    if (data_start >= bufsize) return -3;

	/* コマンドのパースを開始 */
    size_t pos = data_start;
    while (pos < bufsize) {
        uint8_t cmd = buf[pos];

		/* YM2413（0x51）および他の YM 系チップへの書き込み（0x5A / 0x5B / 0x5C） */
        if (cmd == 0x51 || cmd == 0x5A || cmd == 0x5B || cmd == 0x5C) {
			/* 3バイト必要：オペコード、レジスタ、値 */
            if (pos + 2 >= bufsize) {
				/* データが途中で終わっている */
                if (cbs->on_unknown) cbs->on_unknown(user, cmd, (uint32_t)pos);
                break;
            }
            uint8_t reg = buf[pos + 1];
            uint8_t val = buf[pos + 2];
            pos += 3;

			/* 書き込み直後にエンコードされた即時ウェイトを先読みする */
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

			/* コールバックを呼び出す（ディスパッチ） */
            if (cbs->on_reg_write) {
                VGMChipId chip = opcode_to_chip(cmd);
                cbs->on_reg_write(user, chip, reg, val, post_wait_samples);
            }
            continue;
        }

		/* PSG / SN76489 系のデータ（0x50：1バイトのペイロード） */
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

		/* OPN系のパススルー命令（3バイト：オペコード、レジスタ、値） */
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

		/* 短いウェイト 0x70〜0x7F：1〜16 サンプル分の待機 */
        if (cmd >= 0x70 && cmd <= 0x7F) {
            pos += 1;
            uint32_t samples = (cmd & 0x0F) + 1;
            if (cbs->on_wait) cbs->on_wait(user, samples);
            continue;
        }

		/* ウェイト 0x61：16ビット リトルエンディアンのサンプル数で待機 */
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

		/* 60Hz / 50Hz のウェイト */
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

		/* データの終わり */
        if (cmd == 0x66) {
            pos += 1;
            if (cbs->on_end) cbs->on_end(user);
            break;
        }

		/* データブロック 0x67：タイプ + 4バイトのサイズ + ペイロードを読み込む */
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

		/* データブロックタイプ 0x68：慎重にスキップする */
        if (cmd == 0x68) {
            if (pos + 1 >= bufsize) {
                if (cbs->on_unknown) cbs->on_unknown(user, cmd, (uint32_t)pos);
                break;
            }
            if (cbs->on_unknown) cbs->on_unknown(user, cmd, (uint32_t)pos);
            pos += 1;
            continue;
        }

		/* 既知の固定長コマンド（安全にスキップ可能） */
        const KnownFixedCmd *spec = find_known_fixed(cmd);
        if (spec) {
            if (pos + spec->length > bufsize) {
                if (cbs->on_unknown) cbs->on_unknown(user, cmd, (uint32_t)pos);
                break;
            }
            pos += spec->length;
            continue;
        }

		/* 未知の命令：警告を出して1バイトだけ進める */
        if (cbs->on_unknown) cbs->on_unknown(user, cmd, (uint32_t)pos);
        pos += 1;
    }

    return 0;
}

/* ファイルをメモリに読み込んで、バッファベースのパーサーを呼び出す */
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