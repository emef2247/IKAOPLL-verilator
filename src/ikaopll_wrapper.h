#ifndef IKAOPLL_WRAPPER_H
#define IKAOPLL_WRAPPER_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
int ikaopll_set_clock_hz(uint32_t hz);
uint32_t ikaopll_get_clock_hz(void);
uint64_t ikaopll_get_clock_count(void);
uint64_t ikaopll_get_sim_time(void);
void ikaopll_init(void);
void ikaopll_release(void);
void ikaopll_reset(void);
void ikaopll_step_emuclk_posedge(void);
void ikaopll_step_emuclk_negedge(void);
static inline void ikaopll_step_emuclk_1cycle(void) {
    ikaopll_step_emuclk_posedge(); ikaopll_step_emuclk_negedge();
}
void ikaopll_set_phiM_pcen_n(uint8_t v);
void ikaopll_set_CS_n(uint8_t v);
void ikaopll_set_WR_n(uint8_t v);
void ikaopll_set_A0(uint8_t v);
void ikaopll_set_D(uint8_t v);
int16_t ikaopll_get_acc_signed(void);
int16_t ikaopll_get_mo_signed(void);
uint8_t ikaopll_get_dac_en_mo(void);
uint8_t ikaopll_get_acc_strb(void);
void ikaopll_trace_init(const char* path);
void ikaopll_trace_close(void);
void ikaopll_mo_change_log_open(const char* path);
void ikaopll_mo_change_log_close(void);
/* Capture includes every evaluated clock, including bus writes/waits. */
void ikaopll_audio_wav_open(const char* path, int sample_rate);
int ikaopll_audio_csv_open(const char* path, const char* native_path);
int ikaopll_audio_wav_close(void);
void ikaopll_audio_wav_abort(void);
#ifdef __cplusplus
}
#endif
#endif
