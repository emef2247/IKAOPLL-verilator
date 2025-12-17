/*
 * src/ym2413_bus.c
 *
 * YM2413 バス制御 / φM クロック模倣 / ACC & Mo ログ取得 + 定期オーディオサンプリング出力 (CSV)
 *
 * 概要:
 * - 既存の mo ログ（立ち上がりのデバウンス + cluster 内 peak 選択）は維持します。
 * - 追加: "定期サンプリング" を行い、オーディオ用の CSV (t_ps, mo_signed, acc_signed) を出力します。
 *   → ポストプロセスで WAV 化する際に十分な情報を残します。
 *
 * 動作:
 * - デフォルトのサンプルレートは 44100 Hz (AUDIO_SAMPLE_RATE マクロ)。必要なら変更して再ビルドしてください。
 * - サンプリングは「シミュレーション時刻 (ps)」を基準に行います。サンプル時刻を越えたら ikaopll の現在の
 *   mo_signed / acc_signed を取得して CSV に出力します。
 *
 * 設計方針:
 * - VCD 出力は引き続き行う運用を想定。シミュレーションが大きい場合でも、CSV を出力しておけばポスト処理は
 *   そこから行えます。
 * - まずは CSV 出力で確実な確認を行い、必要なら C 側で直接 WAV 出力（wav_writer を使う等）に拡張可能です。
 *
 * 使い方:
 * - デフォルトで audio_samples.csv を出力します (ym2413_bus_init 時にファイルを開きます)。
 * - 出力ファイルを変更したければ ym2413_bus_audio_log_open() を呼ぶ実装を追加しても良いです。
 *
 * 注意:
 * - 出力する mo 値は ikaopll_get_mo_signed() の生の signed 値 (おおむね 10bit 方向) です。
 * - ACC 値は ikaopll_get_acc_signed() の生データ (16bit) です。
 *
 */

#include "ym2413_bus.h"

#include <stdio.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdlib.h>
#include <math.h>

/* 設定: オーディオサンプルレート (Hz) */
#ifndef AUDIO_SAMPLE_RATE
#define AUDIO_SAMPLE_RATE 44100
#endif

/* デバウンス: cluster の間隔 (ps 単位) --- ここでは 2.5 ms をデフォルト */
#ifndef MO_LOG_DEBOUNCE_PS
#define MO_LOG_DEBOUNCE_PS 2500000ULL
#endif

/*=========================================================================
 * グローバル / 状態変数 (先に宣言しておく)
 *=========================================================================*/

/* ACC / Mo ログ用グローバル (従来) */
static FILE*    g_acc_log_fp    = NULL;
static uint64_t g_acc_log_count = 0;

/* Mo ログ (立ち上がり cluster -> peak 出力) */
static FILE*    g_mo_log_fp    = NULL;
static uint64_t g_mo_log_count = 0;

/* オーディオサンプル CSV 出力用 */
static FILE*    g_audio_fp = NULL;
static uint64_t g_audio_log_count = 0;

/* Mo cluster (debounce + peak selection) 状態 */
static int      g_prev_dac_en_mo = 0;
static int      g_cluster_active = 0;
static int      g_cluster_peak_val_signed = 0;
static uint64_t g_cluster_peak_abs = 0ULL;
static uint64_t g_cluster_peak_tps = 0ULL;
static uint64_t g_cluster_last_edge_tps = 0ULL;

/* オーディオ定期サンプリング用状態 */
static uint64_t g_audio_sample_period_ps = 0ULL;   /* ps 単位のサンプル周期 */
static uint64_t g_audio_next_sample_tps = 0ULL;    /* 次サンプルのシミュ時刻 (ps) */
static int32_t  g_last_acc_signed = 0;             /* ACC の最新値（サンプリングで使うためにキャッシュ） */

/*=========================================================================
 * 内部ヘルパ: cluster を出力してクリア
 *=========================================================================*/
static void ym2413_bus_emit_cluster_if_active(void)
{
    if (!g_cluster_active) return;
    if (g_mo_log_fp) {
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

/*=========================================================================
 * EMUCLK / phiM 周りのユーティリティ (従来ロジックを保持)
 *=========================================================================*/
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

/*=========================================================================
 * ACC / Mo ログ用（open/close 実装）
 *=========================================================================*/

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

/*=========================================================================
 * オーディオ CSV 出力の open/close
 * - デフォルトで audio_samples.csv を出すようにしています。
 * - フォーマット: t_ps,mo_signed,acc_signed
 *=========================================================================*/
void ym2413_bus_audio_log_open(const char* path)
{
    if (g_audio_fp) return;

    g_audio_fp = fopen(path, "w");
    if (!g_audio_fp) {
        fprintf(stderr, "[ym2413_bus] failed to open audio log file: %s\n", path);
        return;
    }

    fprintf(g_audio_fp, "t_ps,mo_signed,acc_signed\n");
    g_audio_log_count = 0;
}

void ym2413_bus_audio_log_close(void)
{
    if (g_audio_fp) {
        fclose(g_audio_fp);
        fprintf(stderr, "[ym2413_bus] Audio log closed. total=%" PRIu64 "\n",
                g_audio_log_count);
        g_audio_fp = NULL;
    }
}

/*=========================================================================
 * 公開 API: init
 *=========================================================================*/
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

    /* cluster state reset */
    g_prev_dac_en_mo = 0;
    g_cluster_active = 0;
    g_cluster_peak_val_signed = 0;
    g_cluster_peak_abs = 0ULL;
    g_cluster_peak_tps = 0ULL;
    g_cluster_last_edge_tps = 0ULL;

    /* audio sampling initialisation */
    {
        double period_ps_d = 1000000000000.0 / (double)AUDIO_SAMPLE_RATE; /* ps */
        g_audio_sample_period_ps = (uint64_t)(period_ps_d + 0.5);
        /* 初期 next sample: 現在の sim time + 1 sample */
        uint64_t now = ikaopll_get_sim_time();
        g_audio_next_sample_tps = now + g_audio_sample_period_ps;
        g_last_acc_signed = ikaopll_get_acc_signed();
    }

    /* open default audio CSV (audio_samples.csv) so postprocessing はすぐ可能 */
    ym2413_bus_audio_log_open("audio_samples.csv");
}

/*=========================================================================
 * φM カウントを進めつつ、peak-selection と定期サンプリングを行う
 *=========================================================================*/
void ym2413_bus_step_phiM_cycles(ym2413_bus_t* bus, uint32_t n_phiM)
{
    if (!bus) return;

    for (uint32_t i = 0; i < n_phiM; ++i) {
        /* posedge の評価だけで phiM を検出（negedge はまだ呼ばない） */
        ym2413_bus_wait_phiM_posedge_before_negedge(bus);

        /* 現在時刻を取得 (ps) */
        uint64_t now_tps = ikaopll_get_sim_time();

        /* ===== audio sampling: now >= next_sample_tps ならサンプルを出す ===== */
        if (g_audio_fp) {
            while (now_tps >= g_audio_next_sample_tps) {
                /* mo (DAC) の現在値を取得。これがオーディオキャリアに相当 */
                int16_t mo_signed = ikaopll_get_mo_signed();    /* 例: -512..+511 */
                /* ACC の最新値も取得 (ストローブで更新されるが最新値を常に読んでおく) */
                int16_t acc_signed = ikaopll_get_acc_signed();

                /* CSV 出力: 生の整数値を吐く (ポスト処理でスケールや mix を行う想定) */
                fprintf(g_audio_fp, "%" PRIu64 ",%d,%d\n", g_audio_next_sample_tps, (int)mo_signed, (int)acc_signed);
                g_audio_log_count++;

                /* 次サンプル時刻へ */
                g_audio_next_sample_tps += g_audio_sample_period_ps;
            }
        }

        /* ===== mo cluster (debounce + peak selection) ロジック ===== */

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
            /* キャッシュも更新 */
            g_last_acc_signed = acc;
        } else {
            /* 常に最新 ACC をキャッシュしておく（サンプリング時に使えるように） */
            g_last_acc_signed = ikaopll_get_acc_signed();
        }

        /* posedge の後に negedge を実行してその EMUCLK サイクルを完了する */
        ikaopll_step_emuclk_negedge();
    }
}

/*=========================================================================
 * WAIT ルール適用 (従来)
 *=========================================================================*/
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

/*=========================================================================
 * I/O シーケンス (既存のまま)
 *=========================================================================*/
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