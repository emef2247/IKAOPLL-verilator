#ifndef IKAOPLL_WRAPPER_H
#define IKAOPLL_WRAPPER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*-------------------------------------------------------------------------
 * Verilator / IKAOPLL 初期化・終了
 *------------------------------------------------------------------------*/

void ikaopll_init(void);
void ikaopll_release(void);

void ikaopll_trace_init(const char* vcd_filename);
void ikaopll_trace_close(void);

/* シミュレーション時刻 (ps 単位) を取得する */
uint64_t ikaopll_get_sim_time(void);

/*-------------------------------------------------------------------------
 * クロック／リセット制御
 *------------------------------------------------------------------------*/

void ikaopll_reset(void);
void ikaopll_step_emuclk_posedge(void);
void ikaopll_step_emuclk_negedge(void);

static inline void ikaopll_step_emuclk_1cycle(void) {
    ikaopll_step_emuclk_posedge();
    ikaopll_step_emuclk_negedge();
}

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

int16_t ikaopll_get_acc_signed(void);
int16_t ikaopll_get_mo_signed(void);
uint8_t ikaopll_get_dac_en_mo(void);
uint8_t ikaopll_get_acc_strb(void);

/*-------------------------------------------------------------------------
 * MO 変化差分ログ（CSV）
 *------------------------------------------------------------------------*/
void ikaopll_mo_change_log_open(const char* path);
void ikaopll_mo_change_log_close(void);

/*-------------------------------------------------------------------------
 * Signal dump (change-point CSV)
 *
 * Format (CSV): timestamp_ps,signal,value,duration_ps
 * - timestamp_ps : current sim time in ps (ikaopll_get_sim_time())
 * - signal       : textual signal name (e.g., "o_IMP_FLUC_SIGNED_MO")
 * - value        : integer representation of the signal value
 * - duration_ps  : difference in ps from the previous change for the same signal
 *
 * Usage:
 *  - Call ikaopll_signal_dump_open("signal_dump.csv") to start logging.
 *  - During simulation each EMUCLK half-step will check for changes and append rows.
 *  - Call ikaopll_signal_dump_close() at the end to flush/close the file.
 *
 * Note: This is implemented in C and does not modify Verilog RTL.
 *------------------------------------------------------------------------*/
void ikaopll_signal_dump_open(const char* path);
void ikaopll_signal_dump_close(void);

#ifdef __cplusplus
}
#endif

#endif /* IKAOPLL_WRAPPER_H */