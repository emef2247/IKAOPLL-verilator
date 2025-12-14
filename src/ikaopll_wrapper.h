#ifndef IKAOPLL_WRAPPER_H
#define IKAOPLL_WRAPPER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Verilator / IKAOPLL 全体の初期化 */
void ikaopll_init(void);

/* リセットシーケンスを実行（実装は後で詳細化） */
void ikaopll_reset(void);

/* EMUCLK を指定サイクル数だけ進める（仮のインターフェース） */
void ikaopll_step(uint32_t emuclk_cycles);

/* 今後追加予定の API のプロトタイプ（ダミー） */
/* 実装に合わせて後で flesh up する */

#ifdef __cplusplus
}
#endif

#endif /* IKAOPLL_WRAPPER_H */

