/*
 * src/ym2413_bus.c
 *
 * YM2413 バス制御 / φM クロック模倣 / ACC & Mo ログ取得
 *
 * 変更点（今回）:
 * - o_DAC_EN_MO の立ち上がりを検出し、"debounce window" 内の複数立ち上がりを
 *   まとめて、そのウィンドウ内で最大振幅（peak）の時刻と値のみを出力します。
 * - これにより WAV 合成向けに「1 論理イベント = 1 行」のログが得られます。
 *
 * 動作:
 * - 立ち上がりを検出するごとに cluster を作成/更新し、最後の立ち上がりから
 *   MO_LOG_DEBOUNCE_PS を越えた時点でクラスタの peak をログに書き込みます。
 *
 * 注意:
 * - デフォルトのデバウンス時間は MO_LOG_DEBOUNCE_PS = 2500000 ps (2.5 ms) にしてあります。
 *   必要なら値を変更してください。
 */

#include "ym2413_bus.h"

#include <stdio.h>
#include <inttypes.h>   /* PRIu64 用 */
#include <stdlib.h>     /* abs */

/* Debounce: cluster の間隔 (ps 単位) --- 2.5 ms */
#ifndef MO_LOG_DEBOUNCE_PS
#define MO_LOG_DEBOUNCE_PS 2500000ULL
#endif

/*-------------------------------------------------------------------------
 * ACC / Mo ログ用 グローバル（ここで先に宣言しておく）
 *------------------------------------------------------------------------*/
static FILE*    g_acc_log_fp    = NULL;
static uint64_t g_acc_log_count = 0;

/* Mo ログ */
static FILE*    g_mo_log_fp    = NULL;
static uint64_t g_mo_log_count = 0;

/*-------------------------------------------------------------------------
 * 内部グローバル / cluster 状態（peak-selection 用）
 *------------------------------------------------------------------------*/

/* Mo の DAC_EN の前回状態を保持する (0/1) --- rising-edge 検出用 */
static int g_prev_dac_en_mo = 0;

/* cluster (debounce window) 状態:
 * - g_cluster_active: 現在 cluster 収集中か
 * - g_cluster_peak_val_signed: cluster 内の peak の signed 値
 * - g_cluster_peak_abs: peak の絶対値 (比較用)
 * - g_cluster_peak_tps: peak 時刻 (ps)
 * - g_cluster_last_edge_tps: cluster 内で最後に立ち上がりが見つかった時刻 (ps)
 */
static int      g_cluster_active = 0;
static int      g_cluster_peak_val_signed = 0;
static uint64_t g_cluster_peak_abs = 0ULL;
static uint64_t g_cluster_peak_tps = 0ULL;
static uint64_t g_cluster_last_edge_tps = 0ULL;

/*-------------------------------------------------------------------------
 * ヘルパ: cluster を出力してクリア
 *------------------------------------------------------------------------*/
static void ym2413_bus_emit_cluster_if_active(void)
{
    if (!g_cluster_active) return;
    if (g_mo_log_fp) {
        /* 出力は peak 時刻と signed 値 (CSV: t_ps,mo) */
        fprintf(g_mo_log_fp, "%" PRIu64 ",%d\n", g_cluster_peak_tps, g_cluster_peak_val_signed);
        g_mo_log_count++;
    }
    /* clear */
    g_cluster_active = 0;
    g_cluster_peak_val_signed = 0;
    g_cluster_peak_abs = 0ULL;
    g_cluster_peak_tps = 0ULL;
    g_cluster_last_edge_tps = 0ULL;
}

/*-------------------------------------------------------------------------
 * EMUCLK / phiM 周りのユーティリティ (既存のロジックを保持)
 *------------------------------------------------------------------------*/
static void ym2413_bus_step_emuclk_1cycle(ym2413_bus_t* bus)
{
    if (!bus) return;

    ikaopll_step_emuclk_posedge();

    if (bus->clkdiv == 3) {
        bus->clkdiv = 0;
        bus->phiMref = 1;
    } else {
        bus->clkdiv = (uint8_t)(bus->clkdiv + 1u);
        if (bus->clkdiv == 1) {
            bus->phiMref = 0;
        }
    }

    if (bus->phiMref) {
        bus->phiM_cnt += 1;
    }

    ikaopll_step_emuclk_negedge();
}

static void ym2413_bus_step_emuclk_posedge_only(ym2413_bus_t* bus)
{
    if (!bus) return;

    ikaopll_step_emuclk_posedge();

    if (bus->clkdiv == 3) {
        bus->clkdiv = 0;
        bus->phiMref = 1;
    } else {
        bus->clkdiv = (uint8_t)(bus->clkdiv + 1u);
        if (bus->clkdiv == 1) {
            bus->phiMref = 0;
        }
    }

    if (bus->phiMref) {
        bus->phiM_cnt += 1;
    }
}

static void ym2413_bus_wait_phiM_posedge_before_negedge(ym2413_bus_t* bus)
{
    if (!bus) return;
    uint8_t last;
    for (;;) {
        last = bus->phiMref;
        ym2413_bus_step_emuclk_posedge_only(bus);
        if (last == 0 && bus->phiMref == 1) {
            break;
        }
        ikaopll_step_emuclk_negedge();
    }
}

static void ym2413_bus_wait_phiM_posedge(ym2413_bus_t* bus)
{
    if (!bus) return;
    uint8_t last;
    do {
        last = bus->phiMref;
        ym2413_bus_step_emuclk_1cycle(bus);
    } while (!(last == 0 && bus->phiMref == 1));
}

static void ym2413_bus_wait_phiM_negedge(ym2413_bus_t* bus)
{
    if (!bus) return;
    uint8_t last;
    do {
        last = bus->phiMref;
        ym2413_bus_step_emuclk_1cycle(bus);
    } while (!(last == 1 && bus->phiMref == 0));
}

static void ym2413_bus_wait_phiM_cycles(ym2413_bus_t* bus, int32_t n)
{
    if (!bus) return;
    if (n <= 0) return;
    for (int32_t i = 0; i < n; ++i) {
        ym2413_bus_wait_phiM_posedge(bus);
    }
}

/*-------------------------------------------------------------------------
 * ACC / Mo ログ用（open/close 実装）
 *------------------------------------------------------------------------*/
void ym2413_bus_acc_log_open(const char* path)
{
    if (g_acc_log_fp) return;
    g_acc_log_fp = fopen(path, "w");
    if (!g_acc_log_fp) {
        fprintf(stderr, "[ym2413_bus] failed to open ACC log file: %s\n", path);
        return;
    }
    fprintf(g_acc_log_fp, "t_ps,acc\n");
    g_acc_log_count = 0;
}

void ym2413_bus_acc_log_close(void)
{
    if (g_acc_log_fp) {
        fclose(g_acc_log_fp);
        g_acc_log_fp = NULL;
        fprintf(stderr, "[ym2413_bus] ACC log closed. total=%" PRIu64 "\n",
                g_acc_log_count);
    }
}

void ym2413_bus_mo_log_open(const char* path)
{
    if (g_mo_log_fp) return;

    g_mo_log_fp = fopen(path, "w");
    if (!g_mo_log_fp) {
        fprintf(stderr, "[ym2413_bus] failed to open Mo log file: %s\n", path);
        return;
    }

    fprintf(g_mo_log_fp, "t_ps,mo\n");
    g_mo_log_count = 0;
}

void ym2413_bus_mo_log_close(void)
{
    /* emit pending cluster if any before closing */
    ym2413_bus_emit_cluster_if_active();

    if (g_mo_log_fp) {
        fclose(g_mo_log_fp);
        fprintf(stderr, "[ym2413_bus] Mo log closed. total=%" PRIu64 "\n",
                g_mo_log_count);
        g_mo_log_fp = NULL;
    }
}

/*-------------------------------------------------------------------------
 * 公開 API
 *------------------------------------------------------------------------*/
void ym2413_bus_init(ym2413_bus_t* bus)
{
    if (!bus) return;

    bus->phiM_cnt      = 0;
    bus->clkdiv        = 0;
    bus->phiMref       = 0;

    bus->last_op_kind  = YM2413_LAST_NONE;
    bus->last_op_phiM  = 0;

    bus->min_wait_addr = 12;
    bus->min_wait_data = 84;

    g_prev_dac_en_mo = 0;

    /* cluster state reset */
    g_cluster_active = 0;
    g_cluster_peak_val_signed = 0;
    g_cluster_peak_abs = 0ULL;
    g_cluster_peak_tps = 0ULL;
    g_cluster_last_edge_tps = 0ULL;
}

/*-------------------------------------------------------------------------
 * φM カウントを進めつつ、peak-selection を行うロジック
 *------------------------------------------------------------------------*/
void ym2413_bus_step_phiM_cycles(ym2413_bus_t* bus, uint32_t n_phiM)
{
    if (!bus) return;

    for (uint32_t i = 0; i < n_phiM; ++i) {
        /* posedge の評価だけで phiM を検出（negedge はまだ呼ばない） */
        ym2413_bus_wait_phiM_posedge_before_negedge(bus);

        /* 現在時刻を取得 */
        uint64_t now_tps = ikaopll_get_sim_time();

        /* cluster のタイムアウト判定: 最後の立ち上がりから debounce を越えていたら出力 */
        if (g_cluster_active && (now_tps - g_cluster_last_edge_tps > MO_LOG_DEBOUNCE_PS)) {
            ym2413_bus_emit_cluster_if_active();
        }

        /* posedge 直後の DAC_EN(MO) を読む */
        int cur_dac_en = ikaopll_get_dac_en_mo() ? 1 : 0;

        /* 立ち上がり検出 */
        if (g_prev_dac_en_mo == 0 && cur_dac_en == 1) {
            int mo_signed = ikaopll_get_mo_signed();
            uint64_t mo_abs = (uint64_t)( (mo_signed < 0) ? -(int64_t)mo_signed : mo_signed );

            if (!g_cluster_active) {
                /* 新規 cluster を開始 */
                g_cluster_active = 1;
                g_cluster_peak_val_signed = mo_signed;
                g_cluster_peak_abs = mo_abs;
                g_cluster_peak_tps = now_tps;
                g_cluster_last_edge_tps = now_tps;
            } else {
                /* 既存 cluster の peak 更新判定 (絶対値比較) */
                if (mo_abs > g_cluster_peak_abs) {
                    g_cluster_peak_abs = mo_abs;
                    g_cluster_peak_val_signed = mo_signed;
                    g_cluster_peak_tps = now_tps;
                }
                /* update last edge time to extend cluster */
                g_cluster_last_edge_tps = now_tps;
            }
        }

        /* prev を更新 */
        g_prev_dac_en_mo = cur_dac_en;

        /* ACC_STRB: posedge 直後でも ACC_STRB を拾う (従来どおり) */
        if (g_acc_log_fp && ikaopll_get_acc_strb()) {
            int16_t  acc  = ikaopll_get_acc_signed();
            uint64_t t_ps = ikaopll_get_sim_time();
            fprintf(g_acc_log_fp, "%" PRIu64 ",%d\n", t_ps, (int)acc);
            g_acc_log_count++;
        }

        /* posedge の後に negedge を実行してその EMUCLK サイクルを完了する */
        ikaopll_step_emuclk_negedge();
    }
}

/*-------------------------------------------------------------------------
 * WAIT ルール適用
 *------------------------------------------------------------------------*/
static void ym2413_bus_enforce_wait(ym2413_bus_t* bus, ym2413_last_op_t next_kind)
{
    if (!bus) return;

    int32_t need_wait = 0;

    switch (bus->last_op_kind) {
    case YM2413_LAST_ADDR:
        need_wait = bus->min_wait_addr;
        break;
    case YM2413_LAST_DATA:
        need_wait = bus->min_wait_data;
        break;
    default:
        need_wait = 0;
        break;
    }

    if (need_wait <= 0) {
        bus->last_op_kind = next_kind;
        bus->last_op_phiM = bus->phiM_cnt;
        return;
    }

    int32_t now_phiM = bus->phiM_cnt;
    int32_t diff     = now_phiM - bus->last_op_phiM;

    if (diff < need_wait) {
        int32_t remain = need_wait - diff;
        ym2413_bus_wait_phiM_cycles(bus, remain);
    }

    bus->last_op_kind = next_kind;
    bus->last_op_phiM = bus->phiM_cnt;
}

/*-------------------------------------------------------------------------
 * I/O シーケンス (既存のまま)
 *------------------------------------------------------------------------*/
void ym2413_bus_write_addr(ym2413_bus_t* bus, uint8_t addr)
{
    if (!bus) return;

    ym2413_bus_enforce_wait(bus, YM2413_LAST_ADDR);

    ym2413_bus_wait_phiM_posedge(bus);
    ikaopll_set_A0(0);

    ym2413_bus_wait_phiM_negedge(bus);
    ikaopll_set_CS_n(0);

    ym2413_bus_wait_phiM_posedge(bus);
    ikaopll_set_D(addr);

    ym2413_bus_wait_phiM_negedge(bus);
    ikaopll_set_WR_n(0);

    ym2413_bus_wait_phiM_posedge(bus);

    ym2413_bus_wait_phiM_negedge(bus);
    ikaopll_set_WR_n(1);
    ikaopll_set_CS_n(1);

    ym2413_bus_wait_phiM_posedge(bus);
    ikaopll_set_D(0x00);
}

void ym2413_bus_write_data(ym2413_bus_t* bus, uint8_t data)
{
    if (!bus) return;

    ym2413_bus_enforce_wait(bus, YM2413_LAST_DATA);

    ym2413_bus_wait_phiM_posedge(bus);
    ikaopll_set_A0(1);

    ym2413_bus_wait_phiM_negedge(bus);
    ikaopll_set_CS_n(0);

    ym2413_bus_wait_phiM_posedge(bus);
    ikaopll_set_D(data);

    ym2413_bus_wait_phiM_negedge(bus);
    ikaopll_set_WR_n(0);

    ym2413_bus_wait_phiM_posedge(bus);

    ym2413_bus_wait_phiM_negedge(bus);
    ikaopll_set_WR_n(1);
    ikaopll_set_CS_n(1);

    ym2413_bus_wait_phiM_posedge(bus);
    ikaopll_set_D(0x00);
}