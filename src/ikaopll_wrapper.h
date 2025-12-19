#ifndef IKAOPLL_WRAPPER_H
#define IKAOPLL_WRAPPER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*-------------------------------------------------------------------------
 * Verilator / IKAOPLL 初期化・終了
 *------------------------------------------------------------------------*/

/* Verilated IKAOPLL インスタンスを確保し、内部状態を初期化する */
void ikaopll_init(void);

/* リソース解放（現状は new/delete だけ） */
void ikaopll_release(void);

/* VCD トレースの初期化・終了 */
/* Trace API
 * - ikaopll_trace_init(filename, fmt):
 *     fmt: "vcd" or "fst" (case-insensitive). If "fst" is requested but the build environment
 *          does not provide VerilatedFstC, the implementation will fall back to VCD and
 *          print a warning.
 *     Returns 0 on success, negative on error.
 */
int ikaopll_trace_init(const char* filename, const char* fmt);
void ikaopll_trace_close(void);

/* シミュレーション時刻 (g_main_time) を取得する */
uint64_t ikaopll_get_sim_time(void);

/*-------------------------------------------------------------------------
 * クロック／リセット制御
 *------------------------------------------------------------------------*/

/* リセットシーケンスを実行する。 */
void ikaopll_reset(void);

/* EMUCLK 立ち上がりだけ進める */
void ikaopll_step_emuclk_posedge(void);

/* EMUCLK 立ち下がりだけ進める */
void ikaopll_step_emuclk_negedge(void);

/* EMUCLK 1周期 (posedge + negedge) を進めるヘルパ */
static inline void ikaopll_step_emuclk_1cycle(void) {
    ikaopll_step_emuclk_posedge();
    ikaopll_step_emuclk_negedge();
}

/* phiM_PCEN_n を設定（TB では常に 0 固定） */
void ikaopll_set_phiM_pcen_n(uint8_t value);

/*-------------------------------------------------------------------------
 * YM2413 バスポート setter
 *------------------------------------------------------------------------*/

void ikaopll_set_CS_n(uint8_t v);
void ikaopll_set_WR_n(uint8_t v);
void ikaopll_set_A0(uint8_t v);
void ikaopll_set_D(uint8_t v);

/*-------------------------------------------------------------------------
 * 出力取得（ACC / MO / STRB）
 *------------------------------------------------------------------------*/

/* o_ACC_SIGNED を取得する（16bit signed） */
int16_t ikaopll_get_acc_signed(void);

/* o_IMP_FLUC_SIGNED_MO を取得する（10bit signed 相当だが 16bit に拡張） */
int16_t ikaopll_get_mo_signed(void);

/* o_DAC_EN_MO を取得する（Mo サンプル有効時に 1） */
uint8_t ikaopll_get_dac_en_mo(void);

/* o_ACC_SIGNED_STRB を取得する（ACC 出力更新ストローブ） */
uint8_t ikaopll_get_acc_strb(void);

#ifdef __cplusplus
}
#endif

#endif /* IKAOPLL_WRAPPER_H */