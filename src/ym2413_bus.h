#ifndef YM2413_BUS_H
#define YM2413_BUS_H

#include <stdint.h>
#include "ikaopll_wrapper.h"

/* アクセス種別 */
typedef enum {
    YM2413_LAST_NONE = 0,
    YM2413_LAST_ADDR = 1,
    YM2413_LAST_DATA = 2
} ym2413_last_op_t;

/* YM2413 バスコンテキスト
   - IKAOPLL_vgm_tb.sv の phiM_cnt / last_op_kind / last_op_phiM / clkdiv / phiMref を C に写したもの */
typedef struct {
    /* φM カウンタ (TB: integer phiM_cnt) */
    int32_t          phiM_cnt;

    /* EMUCLK 分周用 (TB: reg [1:0] clkdiv) */
    uint8_t          clkdiv;    /* 0..3 */

    /* 仮想 phiMref レベル (0/1) */
    uint8_t          phiMref;

    /* 前回アクセス種別・時刻 (TB: last_op_kind, last_op_phiM) */
    ym2413_last_op_t last_op_kind;
    int32_t          last_op_phiM;

    /* 最低ウェイト（φM 単位） */
    int32_t          min_wait_addr;
    int32_t          min_wait_data;
} ym2413_bus_t;

/* 初期化（φM カウンタと WAIT パラメータの設定） */
void ym2413_bus_init(ym2413_bus_t* bus);

/* φM ステップを n 回分進める（phiMref / phiM_cnt / clkdiv を TB と同じように更新） */
void ym2413_bus_step_phiM_cycles(ym2413_bus_t* bus, uint32_t n_phiM);

 /* アダプタ用のマーク付きラッパー：バスをステップ実行する際に、プレイヤーやパーサーからこれを呼び出す。
 * この呼び出しは「アダプタによるステップ実行」としてマークされるため、
 * デバッグ用カウンタがアダプタ起因と内部処理起因のステップを区別できる。
 */

void ym2413_bus_step_phiM_cycles_adapter(ym2413_bus_t* bus, uint32_t n_phiM);

/* アドレス書き込み（WAIT 管理込み、TB の IKAOPLL_write(ADDR) 相当） */
void ym2413_bus_write_addr(ym2413_bus_t* bus, uint8_t addr);

/* データ書き込み（WAIT 管理込み、TB の IKAOPLL_write(DATA) 相当） */
void ym2413_bus_write_data(ym2413_bus_t* bus, uint8_t data);

/* ACC ログ制御（任意） */
void ym2413_bus_acc_log_open(const char* path);
void ym2413_bus_acc_log_close(void);

/* Mo ログ制御（任意） */
void ym2413_bus_mo_log_open(const char* path);
void ym2413_bus_mo_log_close(void);

/* オーディオサンプリングのログ出力 (オプション) */
void ym2413_bus_audio_log_open(const char* path);
void ym2413_bus_audio_log_close(void);

/* 出力のベース名を設定する（拡張子なしの本体部分のみ）。
 * 設定されていれば、オーディオ CSV は "<base>.csv"、WAV は "<base>.wav" に出力される。
 * 設定されていなければ、デフォルト（"audio_samples.csv" / "audio_samples.wav"）が使われる。
 */
void ym2413_bus_set_output_basename(const char* base);

 /* ---------------------------------------------------------------------
 * デバッグ／アクセスログ用 API（追加分）
 *
 * - ym2413_bus_debug_open(path): CSV デバッグログを開く（NULL または失敗時はロギング無効）
 * - ym2413_bus_debug_close(): デバッグログを閉じ、stderr にサマリを出力する
 *
 * CSV フォーマット（1 行につき 1 回の書き込み）:
 *   access_idx, phiM_cnt, delta_phiM, approx_samples, op, value_hex
 *   例: 1,12345,64,3,ADDR,0x10
 *
 * ゲッター関数は収集された統計情報を返す。
 * --------------------------------------------------------------------- */

void ym2413_bus_debug_open(const char* path);
void ym2413_bus_debug_close(void);

uint64_t ym2413_bus_debug_get_access_count(void);
uint64_t ym2413_bus_debug_get_total_bytes(void);
uint64_t ym2413_bus_debug_get_min_duration_phiM(void);
uint64_t ym2413_bus_debug_get_max_duration_phiM(void);
uint64_t ym2413_bus_debug_get_total_duration_phiM(void);

/* NEW: getters for phiM totals split by origin */
uint64_t ym2413_bus_debug_get_total_phiM(void);
uint64_t ym2413_bus_debug_get_total_phiM_adapter(void);
uint64_t ym2413_bus_debug_get_total_phiM_internal(void);

#endif /* YM2413_BUS_H */