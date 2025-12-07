// tb/tb_ikaopll_vgm_verilator.cpp
// Mode A testbench (modified): after issuing data WR on the bus, use a mixed
// "accept then short delay" strategy:
//  - Wait a short window for queued (o_D9REG_WRDATA_QUEUED_N 1->0) OR WRITE_DONE (0->1).
//  - If either seen -> accept and proceed after a short post-accept gap.
//  - If not seen in the short window, fall back to waiting for WRITE_DONE up to a longer timeout.
// This trades speed and safety and is suitable for comparing against wrapper/core timing.
//
// Use with wrapper restored to v56 for best compatibility.
// Note: TB debug flag is defined as 1 to allow conditional debug prints.

#include "verilated.h"
#ifdef VM_TRACE
# include "verilated_vcd_c.h"
#else
class VerilatedVcdC;
#endif
#include "VIKAOPLL_vltb.h"

#include <vector>
#include <fstream>
#include <sstream>
#include <string>
#include <cstdint>
#include <iostream>
#include <cmath>
#include <limits>
#include <algorithm>
#include <iomanip>
#include <functional>
#include <cstdlib>
#include <cstring>
#include <csignal>
#include <unistd.h>
#include <cstdio>

#define TB_DEBUG_CPP 1

static uint64_t sim_cycles = 52000000ULL;
static double emuclk_hz = 3579545.0;
static uint32_t audio_sample_rate = 44100;
static uint64_t vcd_time_ps_per_tick = 0;

static uint64_t reset_half_cycles = 1024ULL;
static int WR_HOLD_POS_EDGES = 3;
static bool PHIMREF_INVERT = true;
static int TRACE_LEVEL = 99;
static bool A0_ACTIVE_HIGH = true;

static uint64_t PENDING_WAIT = 5000000ULL;
static uint64_t COMMIT_WAIT = 70000000ULL;
static uint64_t GLOBAL_SAFETY_WAIT = 100000000ULL;
static bool VERBOSE = false;

static bool DUMP_BUS = false;

// Tunable timeouts
static const uint64_t DONE_TIMEOUT = 200000ULL;       // Mode A: wait for WRITE_DONE (unused in mixed mode fallback)
static const uint64_t PROGRESS_TICK_INTERVAL = 0x3FFFFULL;

// Mixed accept parameters (half-steps, same unit as step_half increments)
static const uint64_t T_ACCEPT_HALF_STEPS = 40ULL;     // short window to accept queued or WRITE_DONE (~200 ns at HALF=5ns)
static const uint64_t POST_ACCEPT_GAP = 4ULL;          // short extra half-steps (~20 ns)
static const uint64_t T_FALLBACK_HALF_STEPS = 5000ULL; // longer fallback wait for WRITE_DONE if accept not seen

static volatile sig_atomic_t g_terminate_requested = 0;
static void sigint_handler(int) { g_terminate_requested = 1; }

static vluint64_t main_time = 0;
double sc_time_stamp() { return (double)main_time; }

struct Event { uint64_t sample_tick; uint8_t reg; uint8_t data; };

static inline std::string trim(const std::string &s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}
static unsigned long parse_csv_number(const std::string &tok_raw) {
    if (tok_raw.empty()) return 0;
    size_t s = 0, e = tok_raw.size();
    while (s < e && isspace((unsigned char)tok_raw[s])) s++;
    while (e > s && isspace((unsigned char)tok_raw[e-1])) e--;
    if (e <= s) return 0;
    std::string tok = tok_raw.substr(s, e - s);
    if (tok.size() > 2 && tok[0] == '0' && (tok[1] == 'x' || tok[1] == 'X')) {
        return std::strtoul(tok.c_str(), nullptr, 0);
    }
    if (tok.find_first_of("ABCDEFabcdef") != std::string::npos) {
        return std::strtoul(tok.c_str(), nullptr, 16);
    }
    return std::strtoul(tok.c_str(), nullptr, 10);
}

static std::vector<Event> read_events_from_csv(const char* path) {
    std::vector<Event> events;
    std::ifstream ifs(path);
    if (!ifs) {
        std::cerr << "[ERR] cannot open CSV: " << path << "\n";
        return events;
    }
    std::vector<std::string> all_lines;
    std::string line;
    while (std::getline(ifs, line)) all_lines.push_back(line);
    ifs.close();
    if (all_lines.empty()) return events;

    int start_idx = 0;
    for (size_t i=0;i<all_lines.size();++i) {
        std::string s = trim(all_lines[i]);
        if (s.empty()) continue;
        std::istringstream ss(s);
        std::string col0;
        if (std::getline(ss, col0, ',')) {
            std::string t = trim(col0);
            bool has_digit = false;
            for (char c: t) if (isdigit((unsigned char)c)) { has_digit = true; break; }
            if (!has_digit) start_idx = i+1;
        }
        break;
    }

    int count0 = 0, count1 = 0;
    int score_addr0 = 0, score_addr1 = 0;
    const int SCAN_LINES = 80;
    int scanned = 0;
    for (int i = start_idx; i < (int)all_lines.size() && scanned < SCAN_LINES; ++i) {
        std::string s = trim(all_lines[i]);
        if (s.empty()) continue;
        scanned++;
        std::istringstream ss(s);
        std::string col;
        if (!std::getline(ss, col, ',')) continue;
        if (!std::getline(ss, col, ',')) continue;
        unsigned long m = parse_csv_number(trim(col)) & 0xFFUL;
        if (m == 0) ++count0;
        else if (m == 1) ++count1;
        std::string val;
        if (!std::getline(ss, val, ',')) continue;
        unsigned long p = parse_csv_number(trim(val)) & 0xFFUL;
        bool addr_like = (p <= 0x3F);
        if (m == 0 && addr_like) ++score_addr0;
        if (m == 1 && addr_like) ++score_addr1;
    }

    unsigned long ADDR_MARKER = 0x01;
    unsigned long DATA_MARKER = 0x00;
    if (count0 == 0 && count1 == 0) {
        ADDR_MARKER = 0x01; DATA_MARKER = 0x00;
        std::cerr << "[TB_CSV] WARN: could not detect marker usage, assuming ADDR=0x01 DATA=0x00\n";
    } else if (score_addr0 != score_addr1) {
        if (score_addr0 > score_addr1) { ADDR_MARKER = 0x00; DATA_MARKER = 0x01; }
        else { ADDR_MARKER = 0x01; DATA_MARKER = 0x00; }
    } else {
        if (count0 > count1) { ADDR_MARKER = 0x00; DATA_MARKER = 0x01; }
        else { ADDR_MARKER = 0x01; DATA_MARKER = 0x00; }
    }

    uint64_t cum = 0;
    bool have_pending_addr = false;
    uint8_t pending_addr = 0;
    uint64_t pending_addr_tick = 0;
    bool have_pending_data = false;
    uint8_t pending_data = 0;
    uint64_t pending_data_tick = 0;

    int lineno = 0;
    for (int idx = start_idx; idx < (int)all_lines.size(); ++idx) {
        lineno++;
        std::string ln = all_lines[idx];
        if (trim(ln).empty()) continue;
        std::istringstream ss(ln);
        std::string col;
        if (!std::getline(ss, col, ',')) continue;
        uint64_t delay = 0;
        std::string col_delay = trim(col);
        if (!col_delay.empty()) delay = (uint64_t)parse_csv_number(col_delay);
        cum += delay;
        if (!std::getline(ss, col, ',')) continue;
        std::string col_marker = trim(col);
        unsigned long marker = parse_csv_number(col_marker) & 0xFFUL;
        std::string col_value;
        if (!std::getline(ss, col_value, ',')) col_value = "0";
        unsigned long payload = parse_csv_number(trim(col_value)) & 0xFFUL;

        if (marker == ADDR_MARKER) {
            if (have_pending_data) {
                Event e; e.sample_tick = cum; e.reg = static_cast<uint8_t>(payload); e.data = pending_data;
                events.push_back(e); have_pending_data = false;
            } else {
                have_pending_addr = true; pending_addr = static_cast<uint8_t>(payload); pending_addr_tick = cum;
            }
        } else if (marker == DATA_MARKER) {
            if (have_pending_addr) {
                Event e; e.sample_tick = pending_addr_tick; e.reg = pending_addr; e.data = static_cast<uint8_t>(payload);
                events.push_back(e); have_pending_addr = false;
            } else {
                have_pending_data = true; pending_data = static_cast<uint8_t>(payload); pending_data_tick = cum;
            }
        } else {
            if (((payload >> 6) & 0x3) == 0) {
                if (have_pending_data) { Event e; e.sample_tick = cum; e.reg = static_cast<uint8_t>(payload); e.data = pending_data; events.push_back(e); have_pending_data = false; }
                else { have_pending_addr = true; pending_addr = static_cast<uint8_t>(payload); pending_addr_tick = cum; }
            } else {
                if (have_pending_addr) { Event e; e.sample_tick = pending_addr_tick; e.reg = pending_addr; e.data = static_cast<uint8_t>(payload); events.push_back(e); have_pending_addr = false; }
                else { have_pending_data = true; pending_data = static_cast<uint8_t>(payload); pending_data_tick = cum; }
            }
        }
    }

    if (have_pending_addr && have_pending_data) {
        Event e; e.sample_tick = pending_addr_tick; e.reg = pending_addr; e.data = pending_data; events.push_back(e);
    } else {
        if (have_pending_addr) std::cerr << "[TB_CSV] WARN: EOF with pending addr\n";
        if (have_pending_data) std::cerr << "[TB_CSV] WARN: EOF with pending data\n";
    }

    return events;
}

// Globals
static int prev_o_ADDRREG_WRRQ = -1;
static int prev_o_DATAREG_WRRQ = -1;
static int prev_o_D9REG_WRDATA_QUEUED_N = -1;
static int prev_o_D9REG_ADDR_MATCH = -1;

static inline void step_half(VIKAOPLL_vltb* top, VerilatedVcdC* tfp, uint64_t &tick,
                             std::vector<int32_t>& raw_samples, bool &prev_strb,
                             uint64_t &strobe_count, int32_t &s_min, int32_t &s_max) {
    static uint64_t last_dump_time = (uint64_t)-1;
    static int prev_phi_raw = -1;

    top->i_XIN_EMUCLK = !top->i_XIN_EMUCLK;
    top->eval();

    int cur_ADDRREG_WRRQ = (int)top->o_ADDRREG_WRRQ;
    int cur_DATAREG_WRRQ = (int)top->o_DATAREG_WRRQ;
    int cur_D9REG_WRDATA_QUEUED_N = (int)top->o_D9REG_WRDATA_QUEUED_N;
    int cur_D9REG_ADDR_MATCH = (int)top->o_D9REG_ADDR_MATCH;
    if (cur_ADDRREG_WRRQ != prev_o_ADDRREG_WRRQ || cur_DATAREG_WRRQ != prev_o_DATAREG_WRRQ ||
        cur_D9REG_WRDATA_QUEUED_N != prev_o_D9REG_WRDATA_QUEUED_N || cur_D9REG_ADDR_MATCH != prev_o_D9REG_ADDR_MATCH) {
        std::fprintf(stderr, "[CORE_SIG] tick=%llu ADDR_WRRQ=%d DATA_WRRQ=%d QUEUED_N=%d ADDR_MATCH=%d\n",
                     (unsigned long long)tick, cur_ADDRREG_WRRQ, cur_DATAREG_WRRQ, cur_D9REG_WRDATA_QUEUED_N, cur_D9REG_ADDR_MATCH);
        prev_o_ADDRREG_WRRQ = cur_ADDRREG_WRRQ;
        prev_o_DATAREG_WRRQ = cur_DATAREG_WRRQ;
        prev_o_D9REG_WRDATA_QUEUED_N = cur_D9REG_WRDATA_QUEUED_N;
        prev_o_D9REG_ADDR_MATCH = cur_D9REG_ADDR_MATCH;
    }

    main_time += vcd_time_ps_per_tick;
#if VM_TRACE
    if (tfp && main_time != last_dump_time) { tfp->dump((vluint64_t)main_time); last_dump_time = main_time; }
#endif

    if (DUMP_BUS) {
        int cur_phi_raw = (int)top->phiMref_out;
        int cur_phi_log = cur_phi_raw; if (PHIMREF_INVERT) cur_phi_log = !cur_phi_raw;
        if (prev_phi_raw == -1) prev_phi_raw = cur_phi_raw;
        if (prev_phi_raw == 0 && cur_phi_raw == 1) {
            std::fprintf(stderr,
                         "[BUS_RAW] tick=%llu phi_raw=%d phi_log=%d i_CS_n=%d i_WR_n=%d i_A0=%d i_D=0x%02x o_D9REG_WRDATA_QUEUED_N=%d o_D9REG_ADDR_MATCH=%d o_WRITE_DONE=%d o_BUSY=%d core_ADDR_WRRQ=%d core_DATA_WRRQ=%d\n",
                         (unsigned long long)tick, cur_phi_raw, cur_phi_log,
                         (int)top->i_CS_n, (int)top->i_WR_n, (int)top->i_A0, (int)top->i_D,
                         (int)top->o_D9REG_WRDATA_QUEUED_N, (int)top->o_D9REG_ADDR_MATCH,
                         (int)top->o_WRITE_DONE, (int)top->o_BUSY, (int)top->o_ADDRREG_WRRQ, (int)top->o_DATAREG_WRRQ);
        }
        prev_phi_raw = cur_phi_raw;
    }

    bool cur_strb = (top->o_ACC_SIGNED_STRB != 0);
    static bool prev_strb_local = false;
    if (!prev_strb_local && cur_strb) {
        int32_t raw_u = (int32_t)top->o_ACC_SIGNED;
        int32_t signed_raw = (int32_t)(int16_t)raw_u;
        raw_samples.push_back(signed_raw);
        ++strobe_count;
        if (signed_raw < s_min) s_min = signed_raw;
        if (signed_raw > s_max) s_max = signed_raw;
        if (VERBOSE) std::cerr << "[STROBE] tick=" << tick << " sample=" << signed_raw << " total_strobes=" << strobe_count << "\n";
    }
    prev_strb_local = cur_strb;

    ++tick;
    if ((tick & PROGRESS_TICK_INTERVAL) == 0) {
        std::cerr << "[PROGRESS] tick=" << tick << " strobe_count=" << strobe_count << "\n";
    }
}

static inline int read_phiMref(VIKAOPLL_vltb* top) {
    int val = (int)top->phiMref_out; if (PHIMREF_INVERT) val = !val; return val;
}

static inline void dump_core_snapshot(VIKAOPLL_vltb* top, uint64_t tick) {
    std::fprintf(stderr,
        "[CORE_SNAP] tick=%llu o_WRITE_DONE=%d o_BUSY=%d o_ADDRREG_WRRQ=%d o_DATAREG_WRRQ=%d o_D9REG_WRDATA_QUEUED_N=%d o_D9REG_ADDR_MATCH=%d\n",
        (unsigned long long)tick,
        (int)top->o_WRITE_DONE, (int)top->o_BUSY,
        (int)top->o_ADDRREG_WRRQ, (int)top->o_DATAREG_WRRQ,
        (int)top->o_D9REG_WRDATA_QUEUED_N, (int)top->o_D9REG_ADDR_MATCH);
}

// Wait-for-edge helper
static inline bool wait_for_edge_generic(VIKAOPLL_vltb* top, VerilatedVcdC* tfp, uint64_t &tick,
                                         std::vector<int32_t>& raw_samples, bool &prev_strb,
                                         uint64_t &strobe_count, int32_t &s_min, int32_t &s_max,
                                         std::function<int()> reader, int want, uint64_t max_steps = (uint64_t)-1) {
    int prev = reader();
    uint64_t steps = 0;
    while (!Verilated::gotFinish() && steps < max_steps && !g_terminate_requested) {
        step_half(top, tfp, tick, raw_samples, prev_strb, strobe_count, s_min, s_max);
        int cur = reader();
        if (prev != cur && cur == want) return true;
        prev = cur;
        ++steps;
    }
    return false;
}

// Low-level bus-only write: assert CS, set D/A0, pulse WR, release
// Replace the existing do_bus_write_only(...) function with this version to add extra debug prints.
// It logs i_A0 before/after set, WR assert/release, phi, tick, i_D, and core queued/WRITE_DONE.
// Insert this into tb/tb_ikaopll_vgm_verilator.cpp replacing the old function.

static inline bool do_bus_write_only(VIKAOPLL_vltb* top, VerilatedVcdC* tfp, uint64_t &tick,
                                     std::vector<int32_t>& raw_samples, bool &prev_strb,
                                     uint64_t &strobe_count, int32_t &s_min, int32_t &s_max,
                                     uint8_t addr_param, uint8_t din)
{
    auto reader_phi = [&](){ return read_phiMref(top); };

    // Idle bus for a couple steps
    top->i_CS_n = 1; top->i_WR_n = 1; top->i_D = 0;
    top->eval();
    for (int i=0;i<2;++i) step_half(top, tfp, tick, raw_samples, prev_strb, strobe_count, s_min, s_max);

    // compute desired A0 value (caller uses LSB convention)
    int wantA0 = (addr_param & 1) ? (A0_ACTIVE_HIGH ? 1 : 0) : (A0_ACTIVE_HIGH ? 0 : 1);
    int prevA0 = (int)top->i_A0;
    int phi_before = reader_phi();
    int queued_before = (int)top->o_D9REG_WRDATA_QUEUED_N;
    int write_done_before = (int)top->o_WRITE_DONE;

    // Debug: print before changing A0
    std::fprintf(stderr, "[BUS_DBG] tick=%llu phi=%d BEFORE set A0 prevA0=%d wantA0=%d i_D=0x%02x queued=%d write_done=%d\n",
                 (unsigned long long)tick, phi_before, prevA0, wantA0, (int)din, queued_before, write_done_before);

    // set A0 on posedge
    if (!wait_for_edge_generic(top,tfp,tick,raw_samples,prev_strb,strobe_count,s_min,s_max,reader_phi,1)) {
        std::fprintf(stderr, "[BUS_DBG] ERROR: phi posedge not seen before setting A0\n");
        return false;
    }
    // only change if different to reduce redundant transitions
    if ((int)top->i_A0 != wantA0) {
        top->i_A0 = wantA0;
        top->eval();
        // step to let it propagate
        step_half(top, tfp, tick, raw_samples, prev_strb, strobe_count, s_min, s_max);
    } else {
        // still step once to align with phi
        top->eval();
        step_half(top, tfp, tick, raw_samples, prev_strb, strobe_count, s_min, s_max);
    }

    // Debug: print after setting A0
    int phi_afterA0 = reader_phi();
    int queued_afterA0 = (int)top->o_D9REG_WRDATA_QUEUED_N;
    int write_done_afterA0 = (int)top->o_WRITE_DONE;
    std::fprintf(stderr, "[BUS_DBG] tick=%llu phi=%d AFTER  set A0 nowA0=%d i_D=0x%02x queued=%d write_done=%d\n",
                 (unsigned long long)tick, phi_afterA0, (int)top->i_A0, (int)din, queued_afterA0, write_done_afterA0);

    // assert CS at negedge
    if (!wait_for_edge_generic(top,tfp,tick,raw_samples,prev_strb,strobe_count,s_min,s_max,reader_phi,0)) {
        std::fprintf(stderr, "[BUS_DBG] ERROR: phi negedge not seen before CS\n");
        return false;
    }
    top->i_CS_n = 0; top->eval();

    // set DIN on posedge
    if (!wait_for_edge_generic(top,tfp,tick,raw_samples,prev_strb,strobe_count,s_min,s_max,reader_phi,1)) {
        std::fprintf(stderr, "[BUS_DBG] ERROR: phi posedge not seen before DIN\n");
        return false;
    }
    top->i_D = din; top->eval();
    step_half(top, tfp, tick, raw_samples, prev_strb, strobe_count, s_min, s_max);

    // Debug: before pulsing WR, snapshot queued / write_done
    std::fprintf(stderr, "[BUS_DBG] tick=%llu phi=%d ABOUT TO PULSE WR i_D=0x%02x i_A0=%d i_CS_n=%d queued=%d write_done=%d\n",
                 (unsigned long long)tick, reader_phi(), (int)top->i_D, (int)top->i_A0, (int)top->i_CS_n,
                 (int)top->o_D9REG_WRDATA_QUEUED_N, (int)top->o_WRITE_DONE);

    // pulse WR on negedge and hold
    if (!wait_for_edge_generic(top,tfp,tick,raw_samples,prev_strb,strobe_count,s_min,s_max,reader_phi,0)) {
        std::fprintf(stderr, "[BUS_DBG] ERROR: phi negedge not seen before WR\n");
        return false;
    }
    top->i_WR_n = 0; top->eval();

    // Debug: right after asserting WR
    std::fprintf(stderr, "[BUS_DBG] tick=%llu phi=%d WR_ASSERTED i_D=0x%02x i_A0=%d i_CS_n=%d queued=%d write_done=%d\n",
                 (unsigned long long)tick, reader_phi(), (int)top->i_D, (int)top->i_A0, (int)top->i_CS_n,
                 (int)top->o_D9REG_WRDATA_QUEUED_N, (int)top->o_WRITE_DONE);

    for (int i=0;i<WR_HOLD_POS_EDGES;++i) {
        if (!wait_for_edge_generic(top,tfp,tick,raw_samples,prev_strb,strobe_count,s_min,s_max,reader_phi,1)) {
            std::fprintf(stderr, "[BUS_DBG] ERROR: hold WR posedge not seen\n");
            return false;
        }
    }

    // release WR and CS after hold
    top->i_WR_n = 1;
    top->i_CS_n = 1;
    top->i_D = 0;
    top->eval();

    // Debug: after releasing WR
    std::fprintf(stderr, "[BUS_DBG] tick=%llu phi=%d WR_RELEASED i_A0=%d i_CS_n=%d queued=%d write_done=%d\n",
                 (unsigned long long)tick, reader_phi(), (int)top->i_A0, (int)top->i_CS_n,
                 (int)top->o_D9REG_WRDATA_QUEUED_N, (int)top->o_WRITE_DONE);

    // one step to settle
    step_half(top, tfp, tick, raw_samples, prev_strb, strobe_count, s_min, s_max);

    return true;
}


// Bus-only write wrapper (previously used for both addr and data)
static inline bool write_reg_pulse(VIKAOPLL_vltb* top, VerilatedVcdC* tfp, uint64_t &tick,
                                   std::vector<int32_t>& raw_samples, bool &prev_strb,
                                   uint64_t &strobe_count, int32_t &s_min, int32_t &s_max,
                                   uint8_t addr_param, uint8_t din) {
#if defined(TB_DEBUG_CPP)
    std::cerr << "[DBG_CPP] START write_reg_pulse addr=0x" << std::hex << int(addr_param) << " data=0x" << int(din)
              << " tick=" << std::dec << tick << "\n";
#endif
    bool ok = do_bus_write_only(top, tfp, tick, raw_samples, prev_strb, strobe_count, s_min, s_max, addr_param, din);
    if (!ok) { std::cerr << "[ERR] do_bus_write_only failed\n"; return false; }
#if defined(TB_DEBUG_CPP)
    std::cerr << "[DBG_CPP] END write_reg_pulse (bus-only) addr=0x" << std::hex << int(addr_param) << " data=0x" << int(din)
              << " tick=" << std::dec << tick << "\n";
#endif
    return true;
}

int main(int argc, char** argv) {
    std::signal(SIGINT, sigint_handler);
    Verilated::commandArgs(argc, argv);

    if (argc > 1) sim_cycles = strtoull(argv[1], nullptr, 0);
    if (argc > 2) emuclk_hz = atof(argv[2]);
    if (argc > 3) audio_sample_rate = (uint32_t)atoi(argv[3]);

    const char* csvpath = (argc >= 5) ? argv[4] : "events.csv";
    if (argc >= 6) reset_half_cycles = strtoull(argv[5], nullptr, 0);

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i],"--pending-wait")==0 && i+1<argc) PENDING_WAIT = strtoull(argv[++i],nullptr,0);
        else if (strncmp(argv[i],"--pending-wait=",14)==0) PENDING_WAIT = strtoull(argv[i]+14,nullptr,0);
        else if (strcmp(argv[i],"--commit-wait")==0 && i+1<argc) COMMIT_WAIT = strtoull(argv[++i],nullptr,0);
        else if (strncmp(argv[i],"--commit-wait=",14)==0) COMMIT_WAIT = strtoull(argv[i]+14,nullptr,0);
        else if (strcmp(argv[i],"--safety-wait")==0 && i+1<argc) GLOBAL_SAFETY_WAIT = strtoull(argv[++i],nullptr,0);
        else if (strncmp(argv[i],"--safety-wait=",14)==0) GLOBAL_SAFETY_WAIT = strtoull(argv[i]+14,nullptr,0);
        else if (strcmp(argv[i],"--verbose")==0) VERBOSE = true;
        else if (strncmp(argv[i],"--trace-level=",14)==0) TRACE_LEVEL = atoi(argv[i]+14);
        else if (strcmp(argv[i],"--dump-bus")==0) DUMP_BUS = true;
        else if (strncmp(argv[i],"--dump-bus=",11)==0) DUMP_BUS = (atoi(argv[i]+11) != 0);
    }

#if VM_TRACE
    Verilated::traceEverOn(true);
#endif

    vcd_time_ps_per_tick = (uint64_t)std::llround(1e12 / (2.0 * emuclk_hz));
    std::cerr << "[DBG] vcd_time_ps_per_tick=" << vcd_time_ps_per_tick << " pending_wait="<<PENDING_WAIT<<" commit_wait="<<COMMIT_WAIT<<" safety="<<GLOBAL_SAFETY_WAIT<<"\n";

    auto events = read_events_from_csv(csvpath);
    std::cerr << "[DBG] events read: " << events.size() << "\n";

    double iter_per_sample = (emuclk_hz * 2.0) / (double)audio_sample_rate;

    VIKAOPLL_vltb* top = new VIKAOPLL_vltb;
#if VM_TRACE
    VerilatedVcdC* tfp = new VerilatedVcdC;
    top->trace(tfp, TRACE_LEVEL);
    tfp->open("dump.vcd");
#else
    VerilatedVcdC* tfp = nullptr;
#endif

    // init
    top->i_CS_n = 1; top->i_WR_n = 1; top->i_A0 = A0_ACTIVE_HIGH?0:1; top->i_D = 0;
    top->i_ACC_SIGNED_MOVOL = 0x1F; top->i_ACC_SIGNED_ROVOL = 0x1F;
    top->i_XIN_EMUCLK = 0;

    prev_o_ADDRREG_WRRQ = -1;
    prev_o_DATAREG_WRRQ = -1;
    prev_o_D9REG_WRDATA_QUEUED_N = -1;
    prev_o_D9REG_ADDR_MATCH = -1;

    std::vector<int32_t> raw_samples; raw_samples.reserve(800000);
    uint64_t tick = 0; bool prev_strb = false; uint64_t strobe_count = 0;
    int32_t s_min = std::numeric_limits<int32_t>::max(); int32_t s_max = std::numeric_limits<int32_t>::min();

    std::cerr << "[DBG] applying reset for " << reset_half_cycles << " half-cycles\n";
    for (uint64_t i=0;i<reset_half_cycles && !g_terminate_requested;++i) step_half(top,tfp,tick,raw_samples,prev_strb,strobe_count,s_min,s_max);
    std::cerr << "[DBG] reset done tick="<<tick<<"\n";

    std::vector<uint64_t> event_ticks; event_ticks.reserve(events.size());
    for (auto &e: events) {
        uint64_t t = (uint64_t)std::llround((double)e.sample_tick * iter_per_sample);
        event_ticks.push_back(tick + t);
    }

    size_t next_event_idx = 0;
    const int POST_SAMPLES = 64;
    const uint64_t POST_TICKS = (uint64_t)std::llround(iter_per_sample * POST_SAMPLES);

    const uint8_t ADDR_MARKER_TO_SEND = 0x00;
    const uint8_t DATA_MARKER_TO_SEND = 0x01;

    while ((!Verilated::gotFinish()) && (tick < sim_cycles || next_event_idx < events.size()) && !g_terminate_requested) {
        while (next_event_idx < events.size() && event_ticks[next_event_idx] <= tick && !g_terminate_requested) {
            uint64_t cur_evt_tick = event_ticks[next_event_idx];
            size_t batch_start = next_event_idx, batch_end = batch_start;
            while (batch_end < events.size() && event_ticks[batch_end] == cur_evt_tick) ++batch_end;
            if (VERBOSE) std::cerr << "[SCHED] batch ev#" << batch_start << "-" << (batch_end-1) << " tick_now=" << tick << "\n";
            for (size_t i = batch_start; i < batch_end && !g_terminate_requested; ++i) {
                if (VERBOSE) std::cerr << "[SCHED] ev#" << i << " tick_now="<<tick<<" reg=0x"<<std::hex<<int(events[i].reg)
                                       <<" data=0x"<<int(events[i].data)<<std::dec<<"\n";

                // 1) Address write (bus-only)
                bool ok = write_reg_pulse(top, tfp, tick, raw_samples, prev_strb, strobe_count, s_min, s_max, ADDR_MARKER_TO_SEND, events[i].reg);
                if (!ok) std::cerr << "[WARN] addr write failed\n";

                // 2) Prepare data side
                // (remove pre-setting of i_A0 here — let do_bus_write_only set A0 at the correct phi)
                // top->i_A0 = (A0_ACTIVE_HIGH ? 1 : 0);
                // top->eval();
                // for (int k = 0; k < 2; ++k) step_half(top, tfp, tick, raw_samples, prev_strb, strobe_count, s_min, s_max);

                // 3) Data write (bus-only)
                ok = do_bus_write_only(top, tfp, tick, raw_samples, prev_strb, strobe_count, s_min, s_max, DATA_MARKER_TO_SEND, events[i].data);
                if (!ok) std::cerr << "[WARN] data bus write failed\n";

                // === Mixed accept strategy: accept on queued (1->0) OR WRITE_DONE (0->1) within a short window.
                {
                    bool accepted = false;
                    uint64_t waited = 0;
                    int prev_queued = (int)top->o_D9REG_WRDATA_QUEUED_N;
                    int prev_done = (int)top->o_WRITE_DONE;

                    // short accept window
                    while (!Verilated::gotFinish() && waited < T_ACCEPT_HALF_STEPS && !g_terminate_requested) {
                        step_half(top, tfp, tick, raw_samples, prev_strb, strobe_count, s_min, s_max);
                        ++waited;
                        int cur_queued = (int)top->o_D9REG_WRDATA_QUEUED_N;
                        int cur_done = (int)top->o_WRITE_DONE;
                        if (prev_queued == 1 && cur_queued == 0) {
                            accepted = true;
                            std::cerr << "[ACCEPT] queued asserted after " << waited << " half-steps (tick=" << tick << ")\n";
                            break;
                        }
                        if (prev_done == 0 && cur_done == 1) {
                            accepted = true;
                            std::cerr << "[ACCEPT] WRITE_DONE observed after " << waited << " half-steps (tick=" << tick << ")\n";
                            break;
                        }
                        prev_queued = cur_queued;
                        prev_done = cur_done;
                    }

                    // fallback: wait for WRITE_DONE longer
                    if (!accepted) {
                        std::cerr << "[ACCEPT] short accept timed out (" << T_ACCEPT_HALF_STEPS << " half-steps); waiting for WRITE_DONE up to " << T_FALLBACK_HALF_STEPS << "\n";
                        uint64_t waited2 = 0;
                        while (!Verilated::gotFinish() && waited2 < T_FALLBACK_HALF_STEPS && !g_terminate_requested) {
                            step_half(top, tfp, tick, raw_samples, prev_strb, strobe_count, s_min, s_max);
                            ++waited2;
                            int cur_done = (int)top->o_WRITE_DONE;
                            if (prev_done == 0 && cur_done == 1) {
                                accepted = true;
                                std::cerr << "[ACCEPT] WRITE_DONE (fallback) after " << waited2 << " half-steps (tick=" << tick << ")\n";
                                break;
                            }
                            prev_done = cur_done;
                        }
                        if (!accepted) {
                            std::cerr << "[ACCEPT] Fallback timeout: no WRITE_DONE in " << T_FALLBACK_HALF_STEPS << " half-steps\n";
                        }
                    }

                    // short post-accept gap to avoid races (if accepted)
                    if (accepted) {
                        for (uint64_t g=0; g<POST_ACCEPT_GAP && !g_terminate_requested; ++g) step_half(top, tfp, tick, raw_samples, prev_strb, strobe_count, s_min, s_max);
                    } else {
                        // proceed anyway (risk of dropped writes)
                        std::cerr << "[ACCEPT] Proceeding without accept (risk of dropped writes)\n";
                    }
                }
                // =================================================================

            }
            for (uint64_t i=0;i<POST_TICKS && !g_terminate_requested;++i) step_half(top,tfp,tick,raw_samples,prev_strb,strobe_count,s_min,s_max);
            next_event_idx = batch_end;
        }
        if (tick >= sim_cycles && next_event_idx >= events.size()) break;
        step_half(top,tfp,tick,raw_samples,prev_strb,strobe_count,s_min,s_max);
    }

    if (!raw_samples.empty()) {
        int32_t max_abs = 0;
        for (auto v : raw_samples) max_abs = std::max<int32_t>(max_abs, std::abs(v));
        double scale = (max_abs>0) ? (32767.0 / (double)max_abs) : 1.0;
        std::ofstream f("out_from_vgm.wav", std::ios::binary);
        f.write("RIFF",4);
        uint32_t filesize_minus8 = 36 + static_cast<uint32_t>(raw_samples.size()*2);
        f.write(reinterpret_cast<const char*>(&filesize_minus8),4);
        f.write("WAVE",4);
        f.write("fmt ",4);
        uint32_t fmtlen = 16; f.write(reinterpret_cast<const char*>(&fmtlen),4);
        uint16_t audioformat = 1; f.write(reinterpret_cast<const char*>(&audioformat),2);
        uint16_t nchannels = 1; f.write(reinterpret_cast<const char*>(&nchannels),2);
        f.write(reinterpret_cast<const char*>(&audio_sample_rate),4);
        uint32_t byterate = audio_sample_rate * nchannels * 2; f.write(reinterpret_cast<const char*>(&byterate),4);
        uint16_t blockalign = nchannels * 2; f.write(reinterpret_cast<const char*>(&blockalign),2);
        uint16_t bitpersample = 16; f.write(reinterpret_cast<const char*>(&bitpersample),2);
        f.write("data",4);
        uint32_t datasz = static_cast<uint32_t>(raw_samples.size()*2); f.write(reinterpret_cast<const char*>(&datasz),4);
        for (auto v : raw_samples) {
            double scaled = v * scale;
            int32_t iv = (int32_t)std::lround(scaled);
            if (iv > 32767) iv = 32767;
            if (iv < -32768) iv = -32768;
            int16_t out = (int16_t)iv;
            f.write(reinterpret_cast<const char*>(&out),2);
        }
        f.close();
        std::cout << "WAV written: out_from_vgm.wav samples=" << raw_samples.size() << " sr=" << audio_sample_rate << "\n";
    } else {
        std::cerr << "[SUMMARY] no samples captured\n";
    }

#if VM_TRACE
    if (tfp) { tfp->close(); delete tfp; tfp = nullptr; }
#endif
    top->final();
    delete top;
    return 0;
}