/*
 * vgm_parser.h
 * 使用例：
 *   VGMParserCallbacks cb = { ... };
 *   int rc = vgm_parse_file(path, &cb, userptr);
 *
 * パーサの動作：
 *  - ヘッダーを解析し、DataOffset／データ開始位置を特定
 *  - ループオフセットも読み取る（パーサ自体はループ処理を行わない）
 *  - 一般的な VGM オペコード（レジスタ書き込み、ウェイト、データブロック）を認識
 *  - 固定長の「未知だが既知の」オペコードについては、正しいバイト数をスキップ（kKnownFixedCmds テーブル）
 *  - 可変長のデータブロック（0x67）は、長さフィールドを読み取り、正確なバイト数をスキップ
 *  - 完全に未知なオペコードは unknown コールバックを呼び出し、1 バイトだけ進めて安全に継続
 *
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
    /* レジスタ書き込みがパースされたときに呼び出される。
	 *
	 * user: ユーザーが指定したポインタ
	 * chip: VGMChipId（チップの識別子）
	 * reg, val: VGM から読み取ったレジスタと値のバイト
	 * post_wait_samples: このコマンドの直後にウェイト命令が埋め込まれている場合、
	 *                    そのサンプル数がここに返される。
	 *                    （VGM ではレジスタ書き込み直後にウェイトを置くことが多い）
	 *                    ホスト側は、書き込み＋ウェイトをまとめて処理しても、
	 *                    別々に扱ってもよい。
	 */
    void (*on_reg_write)(void *user, VGMChipId chip,
                         uint8_t reg, uint8_t val,
                         uint32_t post_wait_samples);

	/* ウェイトコマンドがパースされたときに呼び出される。
	 *
	 * user: ユーザーが指定したポインタ
	 * samples: 待機するオーディオサンプル数（例：735、882、または 1〜16 など）
	 */
    void (*on_wait)(void *user, uint32_t samples);

    /* データ終端（0x66）が現れたときに呼び出される */
    void (*on_end)(void *user);

	/* 未知のオペコードに対して呼び出される（1 バイトだけ進める）。
	 * ログ出力などに便利。
	 *
	 * user: ユーザー指定のポインタ
	 * opcode: 未知のオペコード
	 * offset: そのオペコードが見つかったファイル上のオフセット
	 */
    void (*on_unknown)(void *user, uint8_t opcode, uint32_t offset);

	/* オプション：データブロック（0x67）に遭遇したときに呼び出される。
	 *
	 * type_byte: 0x67 に続くデータブロックのタイプバイト
	 * data_ptr: データ本体へのポインタ（ホストが必要としない場合は NULL の可能性あり）
	 * data_size: データ本体のサイズ（バイト単位）
	 *
	 * このコールバックが NULL の場合でも、パーサは安全にブロックをスキップする。
	 */
    void (*on_data_block)(void *user, uint8_t type_byte,
                          const uint8_t *data_ptr, uint32_t data_size);
} VGMParserCallbacks;

/* オプション：データブロック（0x67）に遭遇したときに呼び出される。
 *
 * type_byte: 0x67 に続くデータブロックのタイプバイト
 * data_ptr: データ本体へのポインタ（ホストが不要なら NULL の可能性あり）
 * data_size: データ本体のバイト数
 *
 * このコールバックが NULL の場合でも、パーサは安全にブロックをスキップする。
 */
int vgm_parse_file(const char *path, const VGMParserCallbacks *cbs, void *user);

/* すでに読み込まれたメモリバッファを、指定サイズでパースする。同じセマンティクス。 */
int vgm_parse_buffer(const uint8_t *buf, size_t bufsize, const VGMParserCallbacks *cbs, void *user);

#ifdef __cplusplus
}
#endif

#endif /* VGM_PARSER_H */

