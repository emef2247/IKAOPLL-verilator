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
 *
 * - ikaopll_mo_change_log_open(path) を呼ぶと "t_ps,mo_signed" ヘッダ付きで
 *   指定パスにログを開きます。path==NULL の場合は "mo_value_changes.csv" を使用。
 * - ikaopll_mo_change_log_close() でクローズ。
 *
 * ログは EMUCLK の eval の後に差分検出して出力します。
 * 出力される時刻は ikaopll_get_sim_time()（ps 単位）です。
 *------------------------------------------------------------------------*/
void ikaopll_mo_change_log_open(const char* path);
void ikaopll_mo_change_log_close(void);

/*-------------------------------------------------------------------------
 * WAV 出力（テストベンチ／バス側から呼び出し可能）
 *
 * - ikaopll_audio_wav_open(path, sample_rate)
 *     WAV の収集／書き出しを開始する。path が NULL の場合は "audio_samples.wav" を使用。
 * - ikaopll_audio_wav_write(mo_signed, acc_signed)
 *     現在の mo_signed から導出されたモノラルサンプルを1つ追加する。
 * - ikaopll_audio_wav_close()
 *     WAV ファイルをクローズしてフラッシュする。
 *
 * これらの関数はオプションであり、ym2413_bus 側で利用可能であれば呼び出される。
 *------------------------------------------------------------------------*/
void ikaopll_audio_wav_open(const char* path, int sample_rate);
void ikaopll_audio_wav_write(int16_t mo_signed, int16_t acc_signed);
void ikaopll_audio_wav_close(void);

#ifdef __cplusplus
}
#endif

#endif /* IKAOPLL_WRAPPER_H */