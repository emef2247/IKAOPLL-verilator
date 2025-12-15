#include "ikaopll_wrapper.h"

#include <cstdio>
#include <cstdlib>

#include "verilated.h"
#include "VIKAOPLL.h"
#include "verilated_vcd_c.h"

/*-------------------------------------------------------------------------
 * グローバルな Verilated モデルインスタンス
 *------------------------------------------------------------------------*/
static VIKAOPLL*       g_top       = nullptr;
/* g_main_time: 10ps 単位のシミュレーション時刻。
   IKAOPLL_vgm_tb.sv の `timescale 10ps/10ps` と合わせる。 */
static vluint64_t      g_main_time = 0;

/* VCD トレース */
static VerilatedVcdC*  g_trace     = nullptr;

/* EMUCLK の内部状態 */
static uint8_t         g_emuclk    = 0;

/* 10ps 単位での EMUCLK half-period (#13968) */
/* → 1ps timescale に合わせて 139680 に変更 */
static const vluint64_t EMUCLK_HALF_TICKS = 139680ULL;

/*-------------------------------------------------------------------------
 * 内部ユーティリティ
 *------------------------------------------------------------------------*/

static void eval_tick()
{
    if (!g_top) return;
    g_top->eval();
    if (g_trace) {
        g_trace->dump(g_main_time);
    }
    /* g_main_time の更新は EMUCLK トグル側で行う */
}

/*-------------------------------------------------------------------------
 * 初期化・終了
 *------------------------------------------------------------------------*/

void ikaopll_init(void)
{
    if (g_top != nullptr) {
        return; /* すでに初期化済み */
    }

    Verilated::traceEverOn(true); /* トレースを有効化 */

    g_top = new VIKAOPLL();

    /* すべての入力ポートに初期値を与える */
    g_top->i_XIN_EMUCLK  = 0;
    g_top->i_phiM_PCEN_n = 0;   /* TB 同様 0 固定（有効） */

    g_top->i_IC_n        = 1;   /* 非リセット状態 */

    g_top->i_ALTPATCH_EN = 0;

    g_top->i_CS_n        = 1;   /* 非選択 */
    g_top->i_WR_n        = 1;   /* 書き込みではない（アイドル） */
    g_top->i_A0          = 0;
    g_top->i_D           = 0;

    /* ACC 用ボリュームは TB と同じ固定値 */
    g_top->i_ACC_SIGNED_MOVOL = 2;  /* 5'sd2 相当 */
    g_top->i_ACC_SIGNED_ROVOL = 3;  /* 5'sd3 相当 */

    g_main_time = 0;
    g_emuclk    = 0;

    eval_tick();
}

void ikaopll_release(void)
{
    if (g_trace) {
        g_trace->close();
        delete g_trace;
        g_trace = nullptr;
    }

    if (g_top) {
        delete g_top;
        g_top = nullptr;
    }
}

/* VCD トレースの初期化 */
void ikaopll_trace_init(const char* vcd_filename)
{
    if (!g_top) {
        std::fprintf(stderr, "ikaopll_trace_init: call ikaopll_init() first.\n");
        return;
    }
    if (g_trace) {
        g_trace->close();
        delete g_trace;
        g_trace = nullptr;
    }

    g_trace = new VerilatedVcdC;
    g_top->trace(g_trace, 99);  /* 深さ 99 (全階層トレース) を指定 */
    g_trace->open(vcd_filename);
}

/* VCD トレースの終了 */
void ikaopll_trace_close(void)
{
    if (g_trace) {
        g_trace->close();
        delete g_trace;
        g_trace = nullptr;
    }
}

/* シミュレーション時刻取得（10ps 単位のカウンタ） */
uint64_t ikaopll_get_sim_time(void)
{
    return g_main_time;
}

/*-------------------------------------------------------------------------
 * クロック／リセット制御
 *------------------------------------------------------------------------*/

/* EMUCLK 立ち上がり */
void ikaopll_step_emuclk_posedge(void)
{
    if (!g_top) {
        std::fprintf(stderr, "ikaopll_step_emuclk_posedge: not initialized. Call ikaopll_init() first.\n");
        return;
    }
    g_emuclk = 1;
    g_top->i_XIN_EMUCLK = g_emuclk;
    eval_tick();
    g_main_time += EMUCLK_HALF_TICKS;  /* #13968 進める */
}

/* EMUCLK 立ち下がり */
void ikaopll_step_emuclk_negedge(void)
{
    if (!g_top) {
        std::fprintf(stderr, "ikaopll_step_emuclk_negedge: not initialized. Call ikaopll_init() first.\n");
        return;
    }
    g_emuclk = 0;
    g_top->i_XIN_EMUCLK = g_emuclk;
    eval_tick();
    g_main_time += EMUCLK_HALF_TICKS;  /* #13968 進める */
}

/* 簡易リセットシーケンス */
void ikaopll_reset(void)
{
    if (!g_top) {
        std::fprintf(stderr, "ikaopll_reset: not initialized. Call ikaopll_init() first.\n");
        return;
    }

    g_top->i_IC_n = 0;
    for (int i = 0; i < 64; ++i) {
        ikaopll_step_emuclk_1cycle();
    }

    g_top->i_IC_n = 1;
    for (int i = 0; i < 64; ++i) {
        ikaopll_step_emuclk_1cycle();
    }
}

void ikaopll_set_phiM_pcen_n(uint8_t value)
{
    if (!g_top) {
        std::fprintf(stderr, "ikaopll_set_phiM_pcen_n: not initialized. Call ikaopll_init() first.\n");
        return;
    }
    g_top->i_phiM_PCEN_n = value ? 1 : 0;
    eval_tick();
}

/*-------------------------------------------------------------------------
 * バスポート setter
 *------------------------------------------------------------------------*/

void ikaopll_set_CS_n(uint8_t v)
{
    if (!g_top) return;
    g_top->i_CS_n = (v ? 1 : 0);
}

void ikaopll_set_WR_n(uint8_t v)
{
    if (!g_top) return;
    g_top->i_WR_n = (v ? 1 : 0);
}

void ikaopll_set_A0(uint8_t v)
{
    if (!g_top) return;
    g_top->i_A0 = (v ? 1 : 0);
}

void ikaopll_set_D(uint8_t v)
{
    if (!g_top) return;
    g_top->i_D = v;
}

/*-------------------------------------------------------------------------
 * 出力取得
 *------------------------------------------------------------------------*/

int16_t ikaopll_get_acc_signed(void)
{
    if (!g_top) {
        std::fprintf(stderr, "ikaopll_get_acc_signed: not initialized. Call ikaopll_init() first.\n");
        return 0;
    }
    return static_cast<int16_t>(g_top->o_ACC_SIGNED);
}

int16_t ikaopll_get_mo_signed(void)
{
    if (!g_top) {
        std::fprintf(stderr, "ikaopll_get_mo_signed: not initialized. Call ikaopll_init() first.\n");
        return 0;
    }
    return static_cast<int16_t>(g_top->o_IMP_FLUC_SIGNED_MO);
}

uint8_t ikaopll_get_dac_en_mo(void)
{
    if (!g_top) return 0;
    return (g_top->o_DAC_EN_MO != 0);
}

uint8_t ikaopll_get_acc_strb(void)
{
    if (!g_top) return 0;
    return (g_top->o_ACC_SIGNED_STRB != 0);
}