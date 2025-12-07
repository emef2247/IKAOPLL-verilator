// tb/tb_ikaopll_vgm_verilator.cpp
// Verilator harness: always-use BUSY/WRITE_DONE handshake (no compile-time macro gating)
// Clean v52-based testbench: robust CSV parsing, two-write addr->data sequence, DBG_BUS logs,
// and a safe SIGINT handler. This file is a standalone C++ source (no prose embedded).

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

#define TB_DEBUG_CPP

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

static const uint64_t PROGRESS_TICK_INTERVAL = 0x3FFFFULL;

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
static inline uint32_t parse_hex_or_dec(const std::string &s) {
    std::string t = trim(s);
    if (t.empty()) return 0;
    try {
        if (t.size()>2 && t[0]=='0' && (t[1]=='x'||t[1]=='X')) return std::stoul(t,nullptr,16);
        for(char c: t) if ((c>='A'&&c<='F')||(c>='a'&&c<='f')) return std::stoul(t,nullptr,16);
        return std::stoul(t,nullptr,0);
    } catch(...) { return 0; }
}

// Parse numeric token from CSV, accept explicit 0x, accept bare hex (contains A-F),
// otherwise treat as decimal. Returns unsigned long.
static unsigned long parse_csv_number(const std::string &tok_raw) {
    if (tok_raw.empty()) return 0;
    // trim whitespace
    size_t s = 0, e = tok_raw.size();
    while (s < e && isspace((unsigned char)tok_raw[s])) s++;
    while (e > s && isspace((unsigned char)tok_raw[e-1])) e--;
    if (e <= s) return 0;
    std::string tok = tok_raw.substr(s, e - s);

    // explicit 0x/0X
    if (tok.size() > 2 && tok[0] == '0' && (tok[1] == 'x' || tok[1] == 'X')) {
        return std::strtoul(tok.c_str(), nullptr, 0);
    }
    // contains hex letters -> parse as hex
    if (tok.find_first_of("ABCDEFabcdef") != std::string::npos) {
        return std::strtoul(tok.c_str(), nullptr, 16);
    }
    // otherwise decimal
    return std::strtoul(tok.c_str(), nullptr, 10);
}

static std::vector<Event> read_events_from_csv(const char* path) {
    std::vector<Event> events;
    std::ifstream ifs(path);
    if (!ifs) {
        std::cerr << "[ERR] cannot open CSV: " << path << "\n";
        return events;
    }

    // Read all lines first so we can detect marker convention
    std::vector<std::string> all_lines;
    std::string line;
    while (std::getline(ifs, line)) {
        all_lines.push_back(line);
    }
    ifs.close();
    if (all_lines.empty()) return events;

    // detect header / first data line index
    int start_idx = 0;
    for (size_t i=0;i<all_lines.size();++i) {
        std::string s = trim(all_lines[i]);
        if (s.empty()) continue;
        // split by comma and examine first column
        std::istringstream ss(s);
        std::string col0;
        if (std::getline(ss, col0, ',')) {
            std::string t = trim(col0);
            bool has_digit = false;
            for (char c: t) if (isdigit((unsigned char)c)) { has_digit = true; break; }
            if (!has_digit) start_idx = i+1; // skip header line
        }
        break;
    }

    // Heuristic: detect whether CSV uses 0x01 as ADDR marker or 0x00 as ADDR marker.
    // Count occurrences of second-column values (0 vs 1) in the first N data lines.
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
        // skip delay
        if (!std::getline(ss, col, ',')) continue;
        // marker
        if (!std::getline(ss, col, ',')) continue;
        unsigned long m = parse_csv_number(trim(col)) & 0xFFUL;
        if (m == 0) ++count0;
        else if (m == 1) ++count1;
        // payload
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
        // fallback to previous defaults (compat)
        ADDR_MARKER = 0x01;
        DATA_MARKER = 0x00;
        std::cerr << "[TB_CSV] WARN: could not detect marker usage, assuming ADDR=0x01 DATA=0x00\n";
    } else if (score_addr0 != score_addr1) {
        if (score_addr0 > score_addr1) {
            ADDR_MARKER = 0x00;
            DATA_MARKER = 0x01;
        } else {
            ADDR_MARKER = 0x01;
            DATA_MARKER = 0x00;
        }
        std::cerr << "[TB_CSV] INFO: autodetect by addr-like score: ADDR_MARKER=0x"
                  << std::hex << ADDR_MARKER << " DATA_MARKER=0x" << DATA_MARKER
                  << " (score0=" << std::dec << score_addr0 << " score1=" << score_addr1 << ")\n";
    } else {
        if (count0 > count1) {
            ADDR_MARKER = 0x00;
            DATA_MARKER = 0x01;
            std::cerr << "[TB_CSV] INFO: autodetected CSV style: ADDR_MARKER=0x00 DATA_MARKER=0x01 (count0=" << count0 << " count1=" << count1 << ")\n";
        } else {
            ADDR_MARKER = 0x01;
            DATA_MARKER = 0x00;
            std::cerr << "[TB_CSV] INFO: autodetected CSV style: ADDR_MARKER=0x01 DATA_MARKER=0x00 (count0=" << count0 << " count1=" << count1 << ")\n";
        }
    }

    // Now parse lines from start_idx using detected mapping.
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

        // delay
        if (!std::getline(ss, col, ',')) continue;
        uint64_t delay = 0;
        std::string col_delay = trim(col);
        if (!col_delay.empty()) delay = (uint64_t)parse_csv_number(col_delay);
        cum += delay;

        // marker/reg column
        if (!std::getline(ss, col, ',')) continue;
        std::string col_marker = trim(col);
        unsigned long marker = parse_csv_number(col_marker) & 0xFFUL;

        // value / data column
        std::string col_value;
        if (!std::getline(ss, col_value, ',')) {
            col_value = "0";
        }
        std::string col_val_trim = trim(col_value);
        unsigned long payload = parse_csv_number(col_val_trim) & 0xFFUL;

        // Normalized handling: marker==ADDR_MARKER => payload is addr
        // marker==DATA_MARKER => payload is data
        if (marker == ADDR_MARKER) {
            // If we already saw data before (data-first), pair it now
            if (have_pending_data) {
                Event e;
                e.sample_tick = cum;            // use current cum (addr time) for scheduling
                e.reg = static_cast<uint8_t>(payload);
                e.data = pending_data;
                events.push_back(e);
                have_pending_data = false;
            } else {
                // store pending addr until data arrives
                have_pending_addr = true;
                pending_addr = static_cast<uint8_t>(payload);
                pending_addr_tick = cum;
            }
        }
        else if (marker == DATA_MARKER) {
            if (have_pending_addr) {
                // we have addr waiting, pair now
                Event e;
                e.sample_tick = pending_addr_tick; // schedule at addr time
                e.reg = pending_addr;
                e.data = static_cast<uint8_t>(payload);
                events.push_back(e);
                have_pending_addr = false;
            } else {
                // store data waiting for addr (data-first case)
                have_pending_data = true;
                pending_data = static_cast<uint8_t>(payload);
                pending_data_tick = cum;
            }
        }
        else {
            // Unknown marker: attempt heuristic and warn
            std::cerr << "[TB_CSV] WARN: line " << lineno << " unknown marker 0x"
                      << std::hex << marker << std::dec << " payload=0x" << std::hex << payload << std::dec
                      << " (t=" << cum << ")\n";
            // Heuristic: if payload's top2bits==0 treat as addr register for D9; otherwise treat as data
            if (((payload >> 6) & 0x3) == 0) {
                // treat as addr: same as ADDR_MARKER
                if (have_pending_data) {
                    Event e;
                    e.sample_tick = cum;
                    e.reg = static_cast<uint8_t>(payload);
                    e.data = pending_data;
                    events.push_back(e);
                    have_pending_data = false;
                } else {
                    have_pending_addr = true;
                    pending_addr = static_cast<uint8_t>(payload);
                    pending_addr_tick = cum;
                }
            } else {
                // treat as data
                if (have_pending_addr) {
                    Event e;
                    e.sample_tick = pending_addr_tick;
                    e.reg = pending_addr;
                    e.data = static_cast<uint8_t>(payload);
                    events.push_back(e);
                    have_pending_addr = false;
                } else {
                    have_pending_data = true;
                    pending_data = static_cast<uint8_t>(payload);
                    pending_data_tick = cum;
                }
            }
        }
    }

    // If at EOF we still have pending addr+data in some order, attempt to flush
    if (have_pending_addr && have_pending_data) {
        Event e;
        // favor addr time for scheduling
        e.sample_tick = pending_addr_tick;
        e.reg = pending_addr;
        e.data = pending_data;
        events.push_back(e);
        have_pending_addr = have_pending_data = false;
    } else {
        if (have_pending_addr) {
            std::cerr << "[TB_CSV] WARN: EOF with pending addr 0x" << std::hex << int(pending_addr) << std::dec << " (no data)\n";
        }
        if (have_pending_data) {
            std::cerr << "[TB_CSV] WARN: EOF with pending data 0x" << std::hex << int(pending_data) << std::dec << " (no addr)\n";
        }
    }

    return events;
}

void write_wav_scaled(const char* filename, const std::vector<int32_t>& raw_samples, uint32_t samplerate) {
    int32_t max_abs = 0;
    for (auto v : raw_samples) max_abs = std::max<int32_t>(max_abs, std::abs(v));
    double scale = (max_abs>0) ? (32767.0 / (double)max_abs) : 1.0;
    std::ofstream f(filename, std::ios::binary);
    f.write("RIFF",4);
    uint32_t filesize_minus8 = 36 + static_cast<uint32_t>(raw_samples.size()*2);
    f.write(reinterpret_cast<const char*>(&filesize_minus8),4);
    f.write("WAVE",4);
    f.write("fmt ",4);
    uint32_t fmtlen = 16; f.write(reinterpret_cast<const char*>(&fmtlen),4);
    uint16_t audioformat = 1; f.write(reinterpret_cast<const char*>(&audioformat),2);
    uint16_t nchannels = 1; f.write(reinterpret_cast<const char*>(&nchannels),2);
    f.write(reinterpret_cast<const char*>(&samplerate),4);
    uint32_t byterate = samplerate * nchannels * 2; f.write(reinterpret_cast<const char*>(&byterate),4);
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
    std::cout << "WAV written: " << filename << " samples=" << raw_samples.size() << " sr=" << samplerate << "\n";
}

static inline void step_half(VIKAOPLL_vltb* top, VerilatedVcdC* tfp, uint64_t &tick,
                             std::vector<int32_t>& raw_samples, bool &prev_strb,
                             uint64_t &strobe_count, int32_t &s_min, int32_t &s_max) {
    static uint64_t last_dump_time = (uint64_t)-1;
    top->i_XIN_EMUCLK = !top->i_XIN_EMUCLK;
    top->eval();
    main_time += vcd_time_ps_per_tick;
#if VM_TRACE
    if (tfp && main_time != last_dump_time) { tfp->dump((vluint64_t)main_time); last_dump_time = main_time; }
#endif
    bool cur_strb = (top->o_ACC_SIGNED_STRB != 0);
    if (!prev_strb && cur_strb) {
        int32_t raw_u = (int32_t)top->o_ACC_SIGNED;
        int32_t signed_raw = (int32_t)(int16_t)raw_u;
        raw_samples.push_back(signed_raw);
        ++strobe_count;
        if (signed_raw < s_min) s_min = signed_raw;
        if (signed_raw > s_max) s_max = signed_raw;
        if (VERBOSE) std::cerr << "[STROBE] tick=" << tick << " sample=" << signed_raw << " total_strobes=" << strobe_count << "\n";
    }
    prev_strb = cur_strb;
    ++tick;
    if ((tick & PROGRESS_TICK_INTERVAL) == 0) {
        std::cerr << "[PROGRESS] tick=" << tick << " strobe_count=" << strobe_count << "\n";
    }
}

static inline int read_phiMref(VIKAOPLL_vltb* top) {
    int val = (int)top->phiMref_out;
    if (PHIMREF_INVERT) val = !val;
    return val;
}

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

// Primary write pulse: uses BUSY/WRITE_DONE handshake unconditionally
static inline bool write_reg_pulse(VIKAOPLL_vltb* top, VerilatedVcdC* tfp, uint64_t &tick,
                                   std::vector<int32_t>& raw_samples, bool &prev_strb,
                                   uint64_t &strobe_count, int32_t &s_min, int32_t &s_max,
                                   uint8_t addr_param, uint8_t din) {
    auto reader_phi = [&](){ return read_phiMref(top); };

#if defined(TB_DEBUG_CPP)
    std::cerr << "[DBG_CPP] START write_reg_pulse addr=0x" << std::hex << int(addr_param) << " data=0x" << int(din)
              << " tick=" << std::dec << tick << " phi=" << reader_phi() << "\n";
#endif

    // idle bus
    top->i_CS_n = 1; top->i_WR_n = 1; top->i_A0 = A0_ACTIVE_HIGH?0:1; top->i_D = 0;
    top->eval();
    for (int i=0;i<2;++i) step_half(top, tfp, tick, raw_samples, prev_strb, strobe_count, s_min, s_max);

    // set A0 on posedge
    if (!wait_for_edge_generic(top,tfp,tick,raw_samples,prev_strb,strobe_count,s_min,s_max,reader_phi,1)) { std::cerr << "[ERR] phi posedge\n"; return false; }
    top->i_A0 = (addr_param & 1) ? (A0_ACTIVE_HIGH ? 1 : 0) : (A0_ACTIVE_HIGH ? 0 : 1);
    top->eval();
    step_half(top, tfp, tick, raw_samples, prev_strb, strobe_count, s_min, s_max);

#if defined(TB_DEBUG_CPP)
    std::cerr << "[DBG_BUS] tick=" << tick << " set A0=" << int(top->i_A0)
              << " (addr_param LSB=" << int(addr_param & 1) << ")\n";
#endif

    // assert CS at negedge
    if (!wait_for_edge_generic(top,tfp,tick,raw_samples,prev_strb,strobe_count,s_min,s_max,reader_phi,0)) { std::cerr << "[ERR] phi negedge before CS\n"; return false; }
    top->i_CS_n = 0; top->eval();

    // set DIN on posedge
    if (!wait_for_edge_generic(top,tfp,tick,raw_samples,prev_strb,strobe_count,s_min,s_max,reader_phi,1)) { std::cerr << "[ERR] phi posedge before DIN\n"; return false; }
    top->i_D = din; top->eval();
    step_half(top, tfp, tick, raw_samples, prev_strb, strobe_count, s_min, s_max);

#if defined(TB_DEBUG_CPP)
    std::cerr << "[DBG_BUS] tick=" << tick << " set D=0x" << std::hex << int(din) << std::dec
              << " CS=" << int(top->i_CS_n) << " A0=" << int(top->i_A0) << "\n";
#endif

    // assert WR on negedge and hold WR
#if defined(TB_DEBUG_CPP)
    std::cerr << "[DBG_BUS] tick=" << tick << " about to pulse WR (prepare)\n";
#endif
    if (!wait_for_edge_generic(top,tfp,tick,raw_samples,prev_strb,strobe_count,s_min,s_max,reader_phi,0)) { std::cerr << "[ERR] phi negedge before WR\n"; return false; }
    top->i_WR_n = 0; top->eval();

#if defined(TB_DEBUG_CPP)
    std::cerr << "[DBG_BUS] tick=" << tick << " WR asserted (i_WR_n=0) D=0x" << std::hex << int(din) << std::dec
              << " A0=" << int(top->i_A0) << " CS=" << int(top->i_CS_n) << "\n";
#endif

    for (int i=0;i<WR_HOLD_POS_EDGES;++i) {
        if (!wait_for_edge_generic(top,tfp,tick,raw_samples,prev_strb,strobe_count,s_min,s_max,reader_phi,1)) { std::cerr << "[ERR] hold WR posedge wait\n"; return false; }
    }

    // ---- UNCONDITIONAL BUSY/WRITE_DONE HANDSHAKE ----
    // Wait for BUSY asserted (DUT acknowledges processing)
    uint64_t busy_wait = 0;
    const uint64_t debug_report_interval = 1000000ULL;
    while (!Verilated::gotFinish() && busy_wait < PENDING_WAIT && !g_terminate_requested) {
        step_half(top,tfp,tick,raw_samples,prev_strb,strobe_count,s_min,s_max);
        ++busy_wait;
        if ((int)top->o_BUSY == 1) {
#if defined(TB_DEBUG_CPP)
            std::cerr << "[DBG_CPP] BUSY asserted after steps=" << busy_wait << " tick=" << tick << "\n";
#endif
            break;
        }
        if (VERBOSE && (busy_wait % debug_report_interval) == 0) {
            std::cerr << "[DBG_CPP] BUSY wait steps=" << busy_wait << " tick=" << tick
                      << " o_BUSY=" << (int)top->o_BUSY << " o_WRITE_DONE=" << (int)top->o_WRITE_DONE << "\n";
        }
    }

    // wait for WRITE_DONE rising
    int prev_done = (int)top->o_WRITE_DONE;
    uint64_t done_steps = 0;
    bool committed = false;
    while (!Verilated::gotFinish() && done_steps < GLOBAL_SAFETY_WAIT && !g_terminate_requested) {
        step_half(top,tfp,tick,raw_samples,prev_strb,strobe_count,s_min,s_max);
        ++done_steps;
        int cur = (int)top->o_WRITE_DONE;
        if (prev_done == 0 && cur == 1) {
#if defined(TB_DEBUG_CPP)
            std::cerr << "[DBG_CPP] WRITE_DONE rising observed after steps=" << done_steps << " tick=" << tick << "\n";
#endif
            committed = true; break;
        }
        if (VERBOSE && (done_steps % 1000000ULL) == 0) {
            std::cerr << "[DBG_CPP] WRITE_DONE wait steps=" << done_steps << " tick=" << tick
                      << " o_BUSY=" << (int)top->o_BUSY << " o_WRITE_DONE=" << cur << "\n";
        }
        prev_done = cur;
    }
    if (!committed && VERBOSE) std::cerr << "[WARN] write: did not observe WRITE_DONE within safety\n";

    // release WR/CS/A0/DIN in safe sequence
    {
        const uint64_t FINAL_WAIT_MAX = 20000ULL;
#if defined(TB_DEBUG_CPP)
        std::cerr << "[DBG_CPP] waiting final phi negedge up to " << FINAL_WAIT_MAX << " steps before releasing WR (tick=" << tick << ")\n";
#endif
        bool ok_final = wait_for_edge_generic(top,tfp,tick,raw_samples,prev_strb,strobe_count,s_min,s_max,reader_phi,0, FINAL_WAIT_MAX);
        if (!ok_final) {
            std::cerr << "[WARN] final phi negedge not observed within timeout; forcing WR release (tick=" << tick << ")\n";
        }
        top->i_WR_n = 1;
        top->i_CS_n = 1;
        top->eval();
        top->i_A0 = A0_ACTIVE_HIGH?0:1;
        top->i_D = 0;
        for (int i=0;i<1;++i) step_half(top, tfp, tick, raw_samples, prev_strb, strobe_count, s_min, s_max);
    }

#if defined(TB_DEBUG_CPP)
    std::cerr << "[DBG_CPP] END write_reg_pulse addr=0x" << std::hex << int(addr_param) << " data=0x" << int(din)
              << " committed=" << (committed?1:0) << std::dec << " tick=" << tick << "\n";
#endif

    return committed;
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

    std::vector<int32_t> raw_samples; raw_samples.reserve(800000);
    uint64_t tick = 0; bool prev_strb = false; uint64_t strobe_count = 0;
    int32_t s_min = std::numeric_limits<int32_t>::max(); int32_t s_max = std::numeric_limits<int32_t>::min();

    std::cerr << "[DBG] applying reset for " << reset_half_cycles << " half-cycles\n";
    for (uint64_t i=0;i<reset_half_cycles && !g_terminate_requested;++i) step_half(top,tfp,tick,raw_samples,prev_strb,strobe_count,s_min,s_max);
    std::cerr << "[DBG] reset done tick="<<tick<<"\n";

    // schedule events
    std::vector<uint64_t> event_ticks; event_ticks.reserve(events.size());
    for (auto &e: events) {
        uint64_t t = (uint64_t)std::llround((double)e.sample_tick * iter_per_sample);
        event_ticks.push_back(tick + t);
    }

    size_t next_event_idx = 0;
    const int POST_SAMPLES = 64;
    const uint64_t POST_TICKS = (uint64_t)std::llround(iter_per_sample * POST_SAMPLES);

    // NOTE: we now perform two bus writes per Event:
    //   1) address write: marker = 0x01, D = reg  (A0=0)
    //   2) data  write: marker = 0x00, D = data (A0=1)
    const uint8_t ADDR_MARKER_TO_SEND = 0x00; // LSB=0 -> A0=0 for address
    const uint8_t DATA_MARKER_TO_SEND = 0x01; // LSB=1 -> A0=1 for data

    while ((!Verilated::gotFinish()) && (tick < sim_cycles || next_event_idx < events.size()) && !g_terminate_requested) {
        while (next_event_idx < events.size() && event_ticks[next_event_idx] <= tick && !g_terminate_requested) {
            uint64_t cur_evt_tick = event_ticks[next_event_idx];
            size_t batch_start = next_event_idx, batch_end = batch_start;
            while (batch_end < events.size() && event_ticks[batch_end] == cur_evt_tick) ++batch_end;
            if (VERBOSE) std::cerr << "[SCHED] batch ev#" << batch_start << "-" << (batch_end-1) << " tick_now=" << tick << "\n";
            for (size_t i = batch_start; i < batch_end && !g_terminate_requested; ++i) {
                if (VERBOSE) std::cerr << "[SCHED] ev#" << i << " tick_now="<<tick<<" reg=0x"<<std::hex<<int(events[i].reg)
                                       <<" data=0x"<<int(events[i].data)<<std::dec<<"\n";
                // Perform address write first (A0=0)
                bool ok = write_reg_pulse(top,tfp,tick,raw_samples,prev_strb,strobe_count,s_min,s_max, ADDR_MARKER_TO_SEND, events[i].reg);
                if (!ok) std::cerr << "[WARN] write_reg_pulse(addr) failed or timed out for reg=0x"<<std::hex<<int(events[i].reg)<<std::dec<<"\n";
                // Then perform data write (A0=1)
                ok = write_reg_pulse(top,tfp,tick,raw_samples,prev_strb,strobe_count,s_min,s_max, DATA_MARKER_TO_SEND, events[i].data);
                if (!ok) std::cerr << "[WARN] write_reg_pulse(data) failed or timed out for reg=0x"<<std::hex<<int(events[i].reg)<<" data=0x"<<int(events[i].data)<<std::dec<<"\n";
            }
            for (uint64_t i=0;i<POST_TICKS && !g_terminate_requested;++i) step_half(top,tfp,tick,raw_samples,prev_strb,strobe_count,s_min,s_max);
            next_event_idx = batch_end;
        }
        if (tick >= sim_cycles && next_event_idx >= events.size()) break;
        step_half(top,tfp,tick,raw_samples,prev_strb,strobe_count,s_min,s_max);
    }

    if (!raw_samples.empty()) write_wav_scaled("out_from_vgm.wav", raw_samples, audio_sample_rate);
    else std::cerr << "[SUMMARY] no samples captured\n";

#if VM_TRACE
    if (tfp) { tfp->close(); delete tfp; tfp = nullptr; }
#endif
    top->final();
    delete top;
    return 0;
}