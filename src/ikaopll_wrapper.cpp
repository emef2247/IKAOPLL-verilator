#include "ikaopll_wrapper.h"
#include "VIKAOPLL.h"
#include "verilated.h"
#include "verilated_vcd_c.h"
#include <cstdio>
#include <cinttypes>
#include <string>
#include <vector>
extern "C" {
#include "wav_writer.h"
}
static VIKAOPLL* g_top = nullptr;
static VerilatedVcdC* g_trace = nullptr;
static uint32_t g_clock_hz = 3579545;
static uint64_t g_time_ps = 0, g_time_remainder = 0, g_cycles = 0;
static FILE* g_mo_log = nullptr;
static bool g_mo_valid = false;
static int16_t g_previous_mo = 0;
static bool g_audio_enabled = false, g_previous_strb = false;
static int g_sample_rate = 44100;
static uint64_t g_origin_cycle = 0, g_origin_ps = 0;
// Resampling time unit: one clock divided by the output sample rate.
static uint64_t g_previous_tick = 0, g_next_tick = 0;
static int16_t g_previous_acc = 0;
static uint64_t g_capture_count = 0, g_last_capture = 0;
static uint64_t g_min_interval = UINT64_MAX, g_max_interval = 0;
static std::vector<int16_t> g_samples;
static std::string g_wav_path;
static FILE* g_audio_csv = nullptr;
static FILE* g_native_csv = nullptr;
int ikaopll_set_clock_hz(uint32_t hz) {
    if (g_top || hz == 0 || hz > 100000000) return -1;
    g_clock_hz = hz; return 0;
}
uint32_t ikaopll_get_clock_hz(void) { return g_clock_hz; }
uint64_t ikaopll_get_clock_count(void) { return g_cycles; }
uint64_t ikaopll_get_sim_time(void) { return g_time_ps; }
int16_t ikaopll_get_mo_signed(void) {
    if (!g_top) return 0;
    const int v = g_top->o_IMP_FLUC_SIGNED_MO & 0x3ff;
    return static_cast<int16_t>(v >= 512 ? v - 1024 : v);
}
int16_t ikaopll_get_acc_signed(void) {
    return g_top ? static_cast<int16_t>(g_top->o_ACC_SIGNED) : 0;
}
uint8_t ikaopll_get_acc_strb(void) { return g_top && g_top->o_ACC_SIGNED_STRB; }
uint8_t ikaopll_get_dac_en_mo(void) { return g_top && g_top->o_DAC_EN_MO; }
static void emit_sample(int16_t value) {
    g_samples.push_back(value);
    if (g_audio_csv) {
        const uint64_t index = g_samples.size();
        const uint64_t t = g_origin_ps + (index / g_sample_rate) * 1000000000000ULL +
                           (index % g_sample_rate) * 1000000000000ULL / g_sample_rate;
        std::fprintf(g_audio_csv, "%" PRIu64 ",%d,%d\n", t,
                     (int)ikaopll_get_mo_signed(), (int)value);
    }
    g_next_tick += g_clock_hz;
}
static void capture_audio(void) {
    const bool strb = ikaopll_get_acc_strb();
    if (g_audio_enabled && strb && !g_previous_strb) {
        const uint64_t tick = (g_cycles - g_origin_cycle) * g_sample_rate;
        const int16_t acc = ikaopll_get_acc_signed();
        if (g_capture_count) {
            const uint64_t interval = g_cycles - g_last_capture;
            if (interval < g_min_interval) g_min_interval = interval;
            if (interval > g_max_interval) g_max_interval = interval;
        }
        ++g_capture_count; g_last_capture = g_cycles;
        if (g_native_csv) std::fprintf(g_native_csv, "%" PRIu64 ",%" PRIu64 ",%d\n",
                                      g_cycles - g_origin_cycle, g_time_ps, (int)acc);
        // STRB remains high for multiple clocks. Capture its edge once.
        while (g_next_tick <= tick && tick > g_previous_tick) {
            const int64_t span = tick - g_previous_tick;
            const int64_t numerator = (int64_t(acc) - g_previous_acc) *
                                      int64_t(g_next_tick - g_previous_tick);
            const int64_t rounded = numerator >= 0 ? numerator + span / 2 : numerator - span / 2;
            emit_sample(static_cast<int16_t>(g_previous_acc + rounded / span));
        }
        g_previous_tick = tick; g_previous_acc = acc;
    }
    g_previous_strb = strb;
}
static void half_tick(uint8_t clock) {
    if (!g_top) return;
    // Exact rational clock accumulation; ps rounding cannot change frequency.
    g_time_remainder += 1000000000000ULL;
    g_time_ps += g_time_remainder / (2ULL * g_clock_hz);
    g_time_remainder %= 2ULL * g_clock_hz;
    if (clock) ++g_cycles;
    g_top->i_XIN_EMUCLK = clock; g_top->eval();
#if VM_TRACE
    if (g_trace) g_trace->dump(g_time_ps);
#endif
    if (clock) capture_audio();
    if (g_mo_log) {
        const int16_t mo = ikaopll_get_mo_signed();
        if (!g_mo_valid || mo != g_previous_mo) {
            std::fprintf(g_mo_log, "%" PRIu64 ",%d\n", g_time_ps, (int)mo);
            g_previous_mo = mo; g_mo_valid = true;
        }
    }
}
void ikaopll_step_emuclk_posedge(void) { half_tick(1); }
void ikaopll_step_emuclk_negedge(void) { half_tick(0); }
void ikaopll_init(void) {
    if (g_top) return;
    Verilated::traceEverOn(true); g_top = new VIKAOPLL;
    g_time_ps = g_time_remainder = g_cycles = 0;
    g_top->i_XIN_EMUCLK = 0; g_top->i_phiM_PCEN_n = 0; g_top->i_IC_n = 1;
    g_top->i_ALTPATCH_EN = 0; g_top->i_CS_n = g_top->i_WR_n = 1;
    g_top->i_A0 = g_top->i_D = 0;
    g_top->i_ACC_SIGNED_MOVOL = 2; g_top->i_ACC_SIGNED_ROVOL = 3;
    g_top->eval(); g_previous_strb = false;
}
void ikaopll_reset(void) {
    if (!g_top) return;
    g_top->i_IC_n = 0;
    for (int i = 0; i < 1000; ++i) ikaopll_step_emuclk_1cycle();
    g_top->i_IC_n = 1;
    for (int i = 0; i < 144; ++i) ikaopll_step_emuclk_1cycle();
}
void ikaopll_set_phiM_pcen_n(uint8_t v) { if (g_top) g_top->i_phiM_PCEN_n = !!v; }
void ikaopll_set_CS_n(uint8_t v) { if (g_top) g_top->i_CS_n = !!v; }
void ikaopll_set_WR_n(uint8_t v) { if (g_top) g_top->i_WR_n = !!v; }
void ikaopll_set_A0(uint8_t v) { if (g_top) g_top->i_A0 = !!v; }
void ikaopll_set_D(uint8_t v) { if (g_top) g_top->i_D = v; }
void ikaopll_audio_wav_open(const char* path, int sample_rate) {
    if (g_audio_enabled) return;
    g_samples.clear(); g_wav_path = path ? path : "audio_samples.wav";
    g_sample_rate = sample_rate > 0 ? sample_rate : 44100;
    g_origin_cycle = g_cycles; g_origin_ps = g_time_ps;
    g_previous_tick = 0; g_next_tick = g_clock_hz;
    g_previous_acc = ikaopll_get_acc_signed(); g_previous_strb = ikaopll_get_acc_strb();
    g_capture_count = g_last_capture = g_max_interval = 0; g_min_interval = UINT64_MAX;
    g_audio_enabled = true;
}
int ikaopll_audio_csv_open(const char* path, const char* native_path) {
    g_audio_csv = std::fopen(path, "w");
    if (!g_audio_csv) return -1;
    std::fprintf(g_audio_csv, "t_ps,mo_signed,acc_signed\n");
    g_native_csv = std::fopen(native_path, "w");
    if (!g_native_csv) { std::fclose(g_audio_csv); g_audio_csv = nullptr; return -1; }
    std::fprintf(g_native_csv, "clock,t_ps,acc_signed\n"); return 0;
}
int ikaopll_audio_wav_close(void) {
    if (!g_audio_enabled) return 0;
    const uint64_t end_tick = (g_cycles - g_origin_cycle) * g_sample_rate;
    // Keep the final partial source interval, including silent/short inputs.
    while (g_next_tick <= end_tick) emit_sample(g_previous_acc);
    const int rc = wav_write_mono16(g_wav_path.c_str(), g_samples.data(), g_samples.size(), g_sample_rate);
    if (rc) std::fprintf(stderr, "Failed to write WAV: %s\n", g_wav_path.c_str());
    else std::printf("WAV written: %s (%zu samples @ %d Hz)\n",g_wav_path.c_str(),g_samples.size(),g_sample_rate);
    std::printf("ACC captures=%" PRIu64 ", clock interval min=%" PRIu64 ", max=%" PRIu64 "\n",
                g_capture_count, g_min_interval == UINT64_MAX ? 0 : g_min_interval, g_max_interval);
    if (g_audio_csv) std::fclose(g_audio_csv);
    if (g_native_csv) std::fclose(g_native_csv);
    g_audio_csv = g_native_csv = nullptr; g_audio_enabled = false;
    return rc;
}
void ikaopll_audio_wav_abort(void) {
    if (g_audio_csv) std::fclose(g_audio_csv);
    if (g_native_csv) std::fclose(g_native_csv);
    g_audio_csv = g_native_csv = nullptr; g_audio_enabled = false; g_samples.clear();
}
void ikaopll_mo_change_log_open(const char* path) {
    if (g_mo_log) return;
    g_mo_log = std::fopen(path ? path : "mo_value_changes.csv", "w");
    if (g_mo_log) std::fprintf(g_mo_log, "t_ps,mo_signed\n"); g_mo_valid = false;
}
void ikaopll_mo_change_log_close(void) { if (g_mo_log) std::fclose(g_mo_log); g_mo_log = nullptr; }
void ikaopll_trace_init(const char* path) {
#if VM_TRACE
    if (!g_top || g_trace) return;
    g_trace = new VerilatedVcdC; g_top->trace(g_trace, 99); g_trace->open(path);
#else
    (void)path;
#endif
}
void ikaopll_trace_close(void) {
#if VM_TRACE
    if (g_trace) { g_trace->close(); delete g_trace; g_trace = nullptr; }
#endif
}
void ikaopll_release(void) {
    ikaopll_audio_wav_close(); ikaopll_mo_change_log_close(); ikaopll_trace_close();
    if (g_top) { g_top->final(); delete g_top; g_top = nullptr; }
}
