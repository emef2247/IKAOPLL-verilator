#include "ikaopll_wrapper.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <inttypes.h>

#include "verilated.h"
#include "VIKAOPLL.h"
#include "verilated_vcd_c.h"

/*-------------------------------------------------------------------------
 * グローバルな Verilated モデルインスタンス
 *------------------------------------------------------------------------*/
static VIKAOPLL*       g_top       = nullptr;
/* g_main_time: ps 単位で管理します（このプロジェクト内の他コードと整合） */
static vluint64_t      g_main_time = 0;

/* VCD トレース */
static VerilatedVcdC*  g_trace     = nullptr;

/* EMUCLK の内部状態 */
static uint8_t         g_emuclk    = 0;

/* EMUCLK half-period (ps 単位) -- 既存値 139680 を使用 */
static const vluint64_t EMUCLK_HALF_TICKS = 139680ULL;

/*-------------------------------------------------------------------------
 * MO 変化差分ログ用
 *------------------------------------------------------------------------*/
static FILE*    g_mo_change_fp = nullptr;
static int16_t  g_prev_mo_val = 0;
static int      g_mo_prev_valid = 0;

static void ikaopll_maybe_log_mo_change(void)
{
    if (!g_top) return;
    if (!g_mo_change_fp) return;

    int32_t cur = static_cast<int32_t>(g_top->o_IMP_FLUC_SIGNED_MO);
    int16_t cur16 = static_cast<int16_t>(cur);

    if (!g_mo_prev_valid || cur16 != g_prev_mo_val) {
        uint64_t t_ps = ikaopll_get_sim_time();
        fprintf(g_mo_change_fp, "%" PRIu64 ",%d\n", t_ps, (int)cur16);
        fflush(g_mo_change_fp);

        g_prev_mo_val = cur16;
        g_mo_prev_valid = 1;
    }
}

void ikaopll_mo_change_log_open(const char* path)
{
    if (g_mo_change_fp) return;
    const char* p = path ? path : "mo_value_changes.csv";
    g_mo_change_fp = fopen(p, "w");
    if (!g_mo_change_fp) {
        std::fprintf(stderr, "ikaopll_mo_change_log_open: failed to open %s\n", p);
        g_mo_change_fp = nullptr;
        return;
    }
    fprintf(g_mo_change_fp, "t_ps,mo_signed\n");
    fflush(g_mo_change_fp);
    g_mo_prev_valid = 0;
}

void ikaopll_mo_change_log_close(void)
{
    if (!g_mo_change_fp) return;
    fclose(g_mo_change_fp);
    g_mo_change_fp = nullptr;
    g_mo_prev_valid = 0;
}

/*-------------------------------------------------------------------------
 * 内部ユーティリティ
 *------------------------------------------------------------------------*/

static void eval_tick()
{
    if (!g_top) return;
    g_top->eval();
#if VM_TRACE
    if (g_trace) {
        g_trace->dump(g_main_time);
    }
#else
    (void)g_trace;
#endif
}

/*-------------------------------------------------------------------------
 * 初期化・終了
 *------------------------------------------------------------------------*/

void ikaopll_init(void)
{
    if (g_top != nullptr) {
        return;
    }

    Verilated::traceEverOn(true);
    g_top = new VIKAOPLL();

    g_top->i_XIN_EMUCLK  = 0;
    g_top->i_phiM_PCEN_n = 0;
    g_top->i_IC_n        = 1;
    g_top->i_ALTPATCH_EN = 0;
    g_top->i_CS_n        = 1;
    g_top->i_WR_n        = 1;
    g_top->i_A0          = 0;
    g_top->i_D           = 0;
    g_top->i_ACC_SIGNED_MOVOL = 2;
    g_top->i_ACC_SIGNED_ROVOL = 3;

    g_main_time = 0;
    g_emuclk    = 0;

    eval_tick();
}

void ikaopll_release(void)
{
#if VM_TRACE
    if (g_trace) {
        g_trace->close();
        delete g_trace;
        g_trace = nullptr;
    }
#else
    (void)g_trace;
#endif

    if (g_top) {
        delete g_top;
        g_top = nullptr;
    }

    ikaopll_mo_change_log_close();
}

void ikaopll_trace_init(const char* vcd_filename)
{
    if (!g_top) {
        std::fprintf(stderr, "ikaopll_trace_init: call ikaopll_init() first.\n");
        return;
    }

#if VM_TRACE
    if (g_trace) {
        g_trace->close();
        delete g_trace;
        g_trace = nullptr;
    }

    Verilated::traceEverOn(true);
    g_trace = new VerilatedVcdC;
    g_top->trace(g_trace, 99);
    g_trace->open(vcd_filename);
#else
    (void)vcd_filename;
#endif
}

void ikaopll_trace_close(void)
{
#if VM_TRACE
    if (g_trace) {
        g_trace->close();
        delete g_trace;
        g_trace = nullptr;
    }
#else
    (void)g_trace;
#endif
}

/* シミュレーション時刻取得（ps 単位） */
uint64_t ikaopll_get_sim_time(void)
{
    return g_main_time;
}

/*-------------------------------------------------------------------------
 * クロック／リセット制御
 *------------------------------------------------------------------------*/

void ikaopll_step_emuclk_posedge(void)
{
    if (!g_top) {
        std::fprintf(stderr, "ikaopll_step_emuclk_posedge: not initialized. Call ikaopll_init() first.\n");
        return;
    }
    g_emuclk = 1;
    g_top->i_XIN_EMUCLK = g_emuclk;
    eval_tick();
    g_main_time += EMUCLK_HALF_TICKS;
    ikaopll_maybe_log_mo_change();
}

void ikaopll_step_emuclk_negedge(void)
{
    if (!g_top) {
        std::fprintf(stderr, "ikaopll_step_emuclk_negedge: not initialized. Call ikaopll_init() first.\n");
        return;
    }
    g_emuclk = 0;
    g_top->i_XIN_EMUCLK = g_emuclk;
    eval_tick();
    g_main_time += EMUCLK_HALF_TICKS;
    ikaopll_maybe_log_mo_change();
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
}

/*-------------------------------------------------------------------------
 * バスポート setter
 *------------------------------------------------------------------------*/

void ikaopll_set_CS_n(uint8_t v) { if (!g_top) return; g_top->i_CS_n = (v ? 1 : 0); }
void ikaopll_set_WR_n(uint8_t v) { if (!g_top) return; g_top->i_WR_n = (v ? 1 : 0); }
void ikaopll_set_A0(uint8_t v) { if (!g_top) return; g_top->i_A0 = (v ? 1 : 0); }
void ikaopll_set_D(uint8_t v)  { if (!g_top) return; g_top->i_D = v; }

/*-------------------------------------------------------------------------
 * 出力取得
 *------------------------------------------------------------------------*/

int16_t ikaopll_get_acc_signed(void)
{
    if (!g_top) {
        std::fprintf(stderr, "ikaopll_get_acc_signed: not initialized\n");
        return 0;
    }
    return static_cast<int16_t>(g_top->o_ACC_SIGNED);
}

int16_t ikaopll_get_mo_signed(void)
{
    if (!g_top) {
        std::fprintf(stderr, "ikaopll_get_mo_signed: not initialized\n");
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