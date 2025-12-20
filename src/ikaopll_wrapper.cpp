// Complete ikaopll_wrapper.cpp with signal-dump additions and missing utilities.
//
// Save as src/ikaopll_wrapper.cpp (overwrite existing file).
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
 * Global Verilated model instance & helpers
 *------------------------------------------------------------------------*/
static VIKAOPLL*       g_top       = nullptr;
static vluint64_t      g_main_time = 0;     // simulation time in ps
static VerilatedVcdC*  g_trace     = nullptr;
static uint8_t         g_emuclk    = 0;
static const vluint64_t EMUCLK_HALF_TICKS = 139680ULL; // ps per half EMUCLK (existing project value)

/*-------------------------------------------------------------------------
 * Forward declarations (internal helpers)
 *------------------------------------------------------------------------*/
static void eval_tick(void);
static void ikaopll_maybe_log_mo_change(void);
static void ikaopll_signal_dump_step(void);

/*-------------------------------------------------------------------------
 * MO change log (existing)
 *------------------------------------------------------------------------*/
static FILE*    g_mo_change_fp = nullptr;
static int16_t  g_prev_mo_val = 0;
static int      g_mo_prev_valid = 0;

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

static void ikaopll_maybe_log_mo_change(void)
{
    if (!g_top) return;
    if (!g_mo_change_fp) return;

    int32_t cur32 = static_cast<int32_t>(g_top->o_IMP_FLUC_SIGNED_MO);
    int16_t cur16 = static_cast<int16_t>(cur32);

    if (!g_mo_prev_valid || cur16 != g_prev_mo_val) {
        uint64_t t_ps = ikaopll_get_sim_time();
        fprintf(g_mo_change_fp, "%" PRIu64 ",%d\n", t_ps, (int)cur16);
        fflush(g_mo_change_fp);

        g_prev_mo_val = cur16;
        g_mo_prev_valid = 1;
    }
}

/*-------------------------------------------------------------------------
 * Signal dump (change-point CSV) implementation
 *------------------------------------------------------------------------*/
static FILE* g_signal_dump_fp = nullptr;

typedef struct {
    int64_t prev_value;
    uint64_t prev_time_ps;
    int     have_prev;
} sig_prev_t;

/* Track previous values for each signal we will dump */
static sig_prev_t pv_dac_en_mo = {0,0,0};
static sig_prev_t pv_dac_en_ro = {0,0,0};
static sig_prev_t pv_imp_nofl_sign = {0,0,0};
static sig_prev_t pv_imp_nofl_mag = {0,0,0}; // 0..255
static sig_prev_t pv_imp_fluc_mo = {0,0,0};
static sig_prev_t pv_imp_fluc_ro = {0,0,0};
static sig_prev_t pv_acc_strb = {0,0,0};
static sig_prev_t pv_acc_signed = {0,0,0};

void ikaopll_signal_dump_open(const char* path)
{
    if (g_signal_dump_fp) return;
    const char* p = path ? path : "signal_dump.csv";
    g_signal_dump_fp = fopen(p, "w");
    if (!g_signal_dump_fp) {
        std::fprintf(stderr, "ikaopll_signal_dump_open: failed to open %s\n", p);
        g_signal_dump_fp = nullptr;
        return;
    }
    fprintf(g_signal_dump_fp, "t_ps,signal,value,duration_ps\n");
    fflush(g_signal_dump_fp);

    /* reset prev states */
    pv_dac_en_mo.have_prev = 0;
    pv_dac_en_ro.have_prev = 0;
    pv_imp_nofl_sign.have_prev = 0;
    pv_imp_nofl_mag.have_prev = 0;
    pv_imp_fluc_mo.have_prev = 0;
    pv_imp_fluc_ro.have_prev = 0;
    pv_acc_strb.have_prev = 0;
    pv_acc_signed.have_prev = 0;
}

void ikaopll_signal_dump_close(void)
{
    if (!g_signal_dump_fp) return;
    fflush(g_signal_dump_fp);
    fclose(g_signal_dump_fp);
    g_signal_dump_fp = nullptr;
}

static void maybe_dump_change(sig_prev_t* prev, const char* name, int64_t curval, uint64_t t_ps)
{
    if (!g_signal_dump_fp) return;
    if (!prev->have_prev) {
        /* first time: write with duration = 0 */
        fprintf(g_signal_dump_fp, "%" PRIu64 ",%s,%" PRId64 ",%" PRIu64 "\n", t_ps, name, (int64_t)curval, (uint64_t)0);
        prev->prev_value = curval;
        prev->prev_time_ps = t_ps;
        prev->have_prev = 1;
        fflush(g_signal_dump_fp);
        return;
    }
    if (curval != prev->prev_value) {
        uint64_t dur = t_ps - prev->prev_time_ps;
        fprintf(g_signal_dump_fp, "%" PRIu64 ",%s,%" PRId64 ",%" PRIu64 "\n", t_ps, name, (int64_t)curval, dur);
        prev->prev_value = curval;
        prev->prev_time_ps = t_ps;
        fflush(g_signal_dump_fp);
    }
}

static void ikaopll_signal_dump_step(void)
{
    if (!g_top) return;
    if (!g_signal_dump_fp) return;

    uint64_t t_ps = ikaopll_get_sim_time();

    /* read signals (names match top-level VCD) */
    int64_t dac_en_mo = (g_top->o_DAC_EN_MO != 0);
    int64_t dac_en_ro = (g_top->o_DAC_EN_RO != 0);
    int64_t imp_nofl_sign = (g_top->o_IMP_NOFLUC_SIGN != 0);
    int64_t imp_nofl_mag = (int64_t)(g_top->o_IMP_NOFLUC_MAG & 0xFF);
    int64_t imp_fluc_mo = (int64_t)( (int32_t)g_top->o_IMP_FLUC_SIGNED_MO );
    int64_t imp_fluc_ro = (int64_t)( (int32_t)g_top->o_IMP_FLUC_SIGNED_RO );
    int64_t acc_strb = (g_top->o_ACC_SIGNED_STRB != 0);
    int64_t acc_signed = (int64_t)( (int32_t)g_top->o_ACC_SIGNED );

    maybe_dump_change(&pv_dac_en_mo, "o_DAC_EN_MO", dac_en_mo, t_ps);
    maybe_dump_change(&pv_dac_en_ro, "o_DAC_EN_RO", dac_en_ro, t_ps);
    maybe_dump_change(&pv_imp_nofl_sign, "o_IMP_NOFLUC_SIGN", imp_nofl_sign, t_ps);
    maybe_dump_change(&pv_imp_nofl_mag, "o_IMP_NOFLUC_MAG", imp_nofl_mag, t_ps);
    maybe_dump_change(&pv_imp_fluc_mo, "o_IMP_FLUC_SIGNED_MO", imp_fluc_mo, t_ps);
    maybe_dump_change(&pv_imp_fluc_ro, "o_IMP_FLUC_SIGNED_RO", imp_fluc_ro, t_ps);
    maybe_dump_change(&pv_acc_strb, "o_ACC_SIGNED_STRB", acc_strb, t_ps);
    maybe_dump_change(&pv_acc_signed, "o_ACC_SIGNED", acc_signed, t_ps);
}

/*-------------------------------------------------------------------------
 * Core eval helper
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
 * Public API implementations
 *------------------------------------------------------------------------*/

void ikaopll_init(void)
{
    if (g_top != nullptr) return;

    Verilated::traceEverOn(true);
    g_top = new VIKAOPLL();

    /* default inputs */
    g_top->i_XIN_EMUCLK  = 0;
    g_top->i_phiM_PCEN_n = 0;
    g_top->i_IC_n        = 1;
    g_top->i_ALTPATCH_EN = 0;
    g_top->i_CS_n        = 1;
    g_top->i_WR_n        = 1;
    g_top->i_A0          = 0;
    g_top->i_D           = 0;
    /* default accumulator volumes as README suggests */
    g_top->i_ACC_SIGNED_MOVOL = 2;
    g_top->i_ACC_SIGNED_ROVOL = 3;

    g_main_time = 0;
    g_emuclk = 0;

    eval_tick();
}

void ikaopll_release(void)
{
    ikaopll_mo_change_log_close();
    ikaopll_signal_dump_close();

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
}

/* trace control */
void ikaopll_trace_init(const char* vcd_filename)
{
    if (!g_top) return;
    if (g_trace) return;
    g_trace = new VerilatedVcdC();
    g_top->trace(g_trace, 99);
    const char* fn = vcd_filename ? vcd_filename : "ikaopll_dump.vcd";
    g_trace->open(fn);
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

/* sim time getter */
uint64_t ikaopll_get_sim_time(void)
{
    return (uint64_t)g_main_time;
}

/* phiM PCEN control */
void ikaopll_set_phiM_pcen_n(uint8_t value)
{
    if (!g_top) return;
    g_top->i_phiM_PCEN_n = value ? 1 : 0;
}

/* EMUCLK step helpers (posedge / negedge) */
void ikaopll_step_emuclk_posedge(void)
{
    if (!g_top) {
        std::fprintf(stderr, "ikaopll_step_emuclk_posedge: not initialized\n");
        return;
    }
    g_emuclk = 1;
    g_top->i_XIN_EMUCLK = g_emuclk;
    eval_tick();
    g_main_time += EMUCLK_HALF_TICKS;
    ikaopll_maybe_log_mo_change();
    ikaopll_signal_dump_step();
}

void ikaopll_step_emuclk_negedge(void)
{
    if (!g_top) {
        std::fprintf(stderr, "ikaopll_step_emuclk_negedge: not initialized\n");
        return;
    }
    g_emuclk = 0;
    g_top->i_XIN_EMUCLK = g_emuclk;
    eval_tick();
    g_main_time += EMUCLK_HALF_TICKS;
    ikaopll_maybe_log_mo_change();
    ikaopll_signal_dump_step();
}

/* reset sequence: keep compatibility with previous behavior */
void ikaopll_reset(void)
{
    if (!g_top) {
        std::fprintf(stderr, "ikaopll_reset: not initialized\n");
        return;
    }

    /* assert IC_n low for some cycles, then release */
    g_top->i_IC_n = 0;
    for (int i = 0; i < 64; ++i) {
        ikaopll_step_emuclk_posedge();
        ikaopll_step_emuclk_negedge();
    }
    g_top->i_IC_n = 1;
    for (int i = 0; i < 64; ++i) {
        ikaopll_step_emuclk_posedge();
        ikaopll_step_emuclk_negedge();
    }
}

/*-------------------------------------------------------------------------
 * Bus setters / getters (keep names used by main and other modules)
 *------------------------------------------------------------------------*/
void ikaopll_set_CS_n(uint8_t v) { if (!g_top) return; g_top->i_CS_n = v ? 1 : 0; }
void ikaopll_set_WR_n(uint8_t v) { if (!g_top) return; g_top->i_WR_n = v ? 1 : 0; }
void ikaopll_set_A0(uint8_t v)  { if (!g_top) return; g_top->i_A0 = v ? 1 : 0; }
void ikaopll_set_D(uint8_t v)   { if (!g_top) return; g_top->i_D = v & 0xFF; }

int16_t ikaopll_get_acc_signed(void)
{
    if (!g_top) { std::fprintf(stderr, "ikaopll_get_acc_signed: not initialized\n"); return 0; }
    return static_cast<int16_t>(g_top->o_ACC_SIGNED);
}

int16_t ikaopll_get_mo_signed(void)
{
    if (!g_top) { std::fprintf(stderr, "ikaopll_get_mo_signed: not initialized\n"); return 0; }
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

/* end of file */