/* tb/tb_ikaopll_vgm_simple.cpp
   Simplified Verilator harness for IKAOPLL_vltb
   - A robust write_reg_pulse with configurable timeouts and verbose/debug mode.
   - Decouples per-write timeouts from sim_cycles; see header docs below.
*/
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

// Defaults and configuration (see README/tb for usage)
static uint64_t sim_cycles = 52000000ULL;
static double emuclk_hz = 3579545.0;
static uint32_t audio_sample_rate = 44100;
static uint64_t vcd_time_ps_per_tick = 0;

static uint64_t reset_half_cycles = 1024ULL;
static int WR_HOLD_POS_EDGES = 3;
static bool PHIMREF_INVERT = false;
static int TRACE_LEVEL = 99;
static bool A0_ACTIVE_HIGH = true;

static uint64_t PENDING_WAIT = 5000000ULL;
static uint64_t QUEUED_ASSERT_WAIT = 500000ULL;
static uint64_t COMMIT_WAIT = 70000000ULL;
static uint64_t GLOBAL_SAFETY_WAIT = 100000000ULL;
static bool VERBOSE = false;
static bool ENABLE_VDBG_PEND_MON = false;

static const uint64_t PROGRESS_TICK_INTERVAL = 0x3FFFFULL;

static vluint64_t main_time = 0;
double sc_time_stamp() { return (double)main_time; }

typedef unsigned long long ull;
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

static std::vector<Event> read_events_from_csv(const char* path) {
    std::vector<Event> events;
    std::ifstream ifs(path);
    if (!ifs) {
        std::cerr << "[ERR] cannot open CSV: " << path << "\n";
        return events;
    }
    std::string line;
    if (!std::getline(ifs,line)) return events;
    uint64_t cum = 0;
    size_t lineno = 1;
    while (std::getline(ifs,line)) {
        ++lineno;
        if (line.empty()) continue;
        std::string line_show = line;
        for (char &c: line_show) if (c=='\r') c='\\';
        if (VERBOSE) std::cerr << "[EV_RAW] line=" << lineno << " raw=\"" << line_show << "\"\n";
        std::istringstream ss(line);
        std::string tok;
        if (!std::getline(ss,tok,',')) continue;
        uint64_t delay = 0;
        tok = trim(tok);
        if (!tok.empty()) {
            try { delay = std::stoull(tok); } catch(...) { delay = 0; }
        }
        if (!std::getline(ss,tok,',')) continue;
        uint8_t reg_marker = (uint8_t)(parse_hex_or_dec(tok) & 0xFF);
        if (!std::getline(ss,tok,',')) tok = "0";
        uint8_t payload = (uint8_t)(parse_hex_or_dec(tok) & 0xFF);
        cum += delay;
        if (reg_marker == 0x00) { events.push_back({cum, 0x01, payload}); if (VERBOSE) std::cerr << "[EV_READ] addr payload=0x"<<std::hex<<int(payload)<<"\n"<<std::dec;}
        else if (reg_marker == 0x01) { events.push_back({cum, 0x00, payload}); if (VERBOSE) std::cerr << "[EV_READ] data payload=0x"<<std::hex<<int(payload)<<"\n"<<std::dec;}
        else { events.push_back({cum, 0x01, reg_marker}); if (payload!=0) events.push_back({cum+1, 0x00, payload}); if (VERBOSE) std::cerr << "[EV_READ] reg-as-addr 0x"<<std::hex<<int(reg_marker)<<"\n"<<std::dec; }
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
        std::cerr << "[STROBE] tick=" << tick << " sample=" << signed_raw
                  << " total_strobes=" << strobe_count << " raw_samples=" << raw_samples.size() << "\n";
    }
    prev_strb = cur_strb;
    ++tick;
    if ((tick & PROGRESS_TICK_INTERVAL) == 0) {
        std::cerr << "[PROGRESS] tick=" << tick << " time_ps=" << main_time << " strobe_count=" << strobe_count << "\n";
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
                                         std::function<int()> reader, int want, uint64_t *out_steps = nullptr, uint64_t max_steps = (uint64_t)-1) {
    int prev = reader();
    uint64_t steps = 0;
    while (!Verilated::gotFinish() && steps < max_steps) {
        step_half(top, tfp, tick, raw_samples, prev_strb, strobe_count, s_min, s_max);
        int cur = reader();
        if (prev != cur && cur == want) {
            if (out_steps) *out_steps = steps;
            return true;
        }
        prev = cur;
        ++steps;
    }
    if (out_steps) *out_steps = steps;
    return false;
}

static inline bool write_reg_pulse(VIKAOPLL_vltb* top, VerilatedVcdC* tfp, uint64_t &tick,
                                   std::vector<int32_t>& raw_samples, bool &prev_strb,
                                   uint64_t &strobe_count, int32_t &s_min, int32_t &s_max,
                                   uint8_t addr_param, uint8_t din) {
    auto reader_phi = [&](){ return read_phiMref(top); };

    top->i_CS_n = 1; top->i_WR_n = 1;
#ifdef HAS_D_OE_PORT
    top->i_D_OE = 0;
#endif
    top->i_D = 0;
    top->eval();
    for (int i=0;i<2;++i) step_half(top, tfp, tick, raw_samples, prev_strb, strobe_count, s_min, s_max);

    if (!wait_for_edge_generic(top,tfp,tick,raw_samples,prev_strb,strobe_count,s_min,s_max,reader_phi,1,nullptr,(uint64_t)-1)) {
        std::cerr << "[ERR] wait_for_edge posedge phi timed out\n"; return false;
    }
    top->i_A0 = (addr_param & 1) ? (A0_ACTIVE_HIGH ? 1 : 0) : (A0_ACTIVE_HIGH ? 0 : 1);
    top->eval();
    step_half(top, tfp, tick, raw_samples, prev_strb, strobe_count, s_min, s_max);

    if (!wait_for_edge_generic(top,tfp,tick,raw_samples,prev_strb,strobe_count,s_min,s_max,reader_phi,0,nullptr,(uint64_t)-1)) {
        std::cerr << "[ERR] wait_for_edge negedge phi timed out\n"; return false;
    }
    top->i_CS_n = 0; top->eval();

    if (!wait_for_edge_generic(top,tfp,tick,raw_samples,prev_strb,strobe_count,s_min,s_max,reader_phi,1,nullptr,(uint64_t)-1)) { std::cerr << "[ERR] wait_for_edge posedge before DIN timed out\n"; return false; }
#ifdef HAS_D_OE_PORT
    top->i_D_OE = 1;
#endif
    top->i_D = din; top->eval();
    step_half(top, tfp, tick, raw_samples, prev_strb, strobe_count, s_min, s_max);

    if (!wait_for_edge_generic(top,tfp,tick,raw_samples,prev_strb,strobe_count,s_min,s_max,reader_phi,0,nullptr,(uint64_t)-1)) { std::cerr << "[ERR] wait_for_edge negedge before WR timed out\n"; return false; }
    top->i_WR_n = 0; top->eval();

    for (int i=0;i<WR_HOLD_POS_EDGES;++i) {
        if (!wait_for_edge_generic(top,tfp,tick,raw_samples,prev_strb,strobe_count,s_min,s_max,reader_phi,1,nullptr,(uint64_t)-1)) {
            std::cerr << "[ERR] waiting for posedge while holding WR\n"; return false;
        }
    }

    auto data_ack_reader = [&](){ return (int)top->o_DATAREG_WRRQ; };
    uint64_t ack_steps = 0;
    bool acked = wait_for_edge_generic(top,tfp,tick,raw_samples,prev_strb,strobe_count,s_min,s_max,data_ack_reader,1,&ack_steps, GLOBAL_SAFETY_WAIT);
    if (!acked) {
        std::cerr << "[ERR] DATA_ACK not observed within global safety during write\n";
        top->i_WR_n = 1; top->i_CS_n = 1; top->i_A0 = A0_ACTIVE_HIGH ? 0 : 1; top->i_D = 0; top->eval();
        return false;
    }
    if (VERBOSE) {
        std::cerr << "[DBG_ACK] tick=" << tick
                  << " o_DATAREG_WRRQ=" << (int)top->o_DATAREG_WRRQ
                  << " o_D9REG_WRDATA_QUEUED_N=" << (int)top->o_D9REG_WRDATA_QUEUED_N
                  << " o_D9REG_DATA=0x" << std::hex << (int)top->o_D9REG_DATA
                  << " pending_valid=" << std::dec << (int)top->o_D9REG_PENDING_VALID
                  << " pending_data=0x" << std::hex << (int)top->o_D9REG_DATA_PENDING << std::dec << "\n";
    }

    bool seen_pending_or_queued = false;
    uint64_t waited = 0;
    uint64_t deadline_steps = GLOBAL_SAFETY_WAIT;
    while (!Verilated::gotFinish() && waited < PENDING_WAIT && waited < deadline_steps) {
        step_half(top, tfp, tick, raw_samples, prev_strb, strobe_count, s_min, s_max);
        int pending_v = (int)top->o_D9REG_PENDING_VALID;
        int queued_n  = (int)top->o_D9REG_WRDATA_QUEUED_N;
        if (pending_v == 1) { if (VERBOSE) std::cerr << "[TB-WAIT] tick="<<tick<<" observed pending_valid==1 after DATA_ACK\n"; seen_pending_or_queued = true; break; }
        if (queued_n == 0) { if (VERBOSE) std::cerr << "[TB-WAIT] tick="<<tick<<" observed queued asserted (0) after DATA_ACK\n"; seen_pending_or_queued = true; break; }
        ++waited;
    }
    if (!seen_pending_or_queued && VERBOSE) std::cerr << "[TB-WAIT] warning: did not observe pending_valid==1 or queued==0 within " << PENDING_WAIT << " cycles\n";

    uint64_t commit_start_tick = tick;
    int prev_queued = (int)top->o_D9REG_WRDATA_QUEUED_N;
    uint32_t prev_d9 = (uint32_t)top->o_D9REG_DATA;
    bool committed = false;
    uint64_t commit_steps = 0;
    while (!Verilated::gotFinish() && commit_steps < COMMIT_WAIT && commit_steps < GLOBAL_SAFETY_WAIT) {
        step_half(top, tfp, tick, raw_samples, prev_strb, strobe_count, s_min, s_max);
        int cur_queued = (int)top->o_D9REG_WRDATA_QUEUED_N;
        uint32_t cur_d9 = (uint32_t)top->o_D9REG_DATA;
        if (VERBOSE && (cur_queued != prev_queued || cur_d9 != prev_d9)) {
            std::cerr << "[DBG_COMMIT] tick="<<tick<<" prev_queued="<<prev_queued<<" cur_queued="<<cur_queued<<" prev_d9=0x"<<std::hex<<prev_d9<<" cur_d9=0x"<<cur_d9<<std::dec<<"\n";
        }
        if (prev_queued == 0 && cur_queued == 1) { committed = true; if (VERBOSE) std::cerr << "[INFO] queued rising edge at tick="<<tick<<"\n"; break; }
        if (cur_d9 != prev_d9) { committed = true; if (VERBOSE) std::cerr << "[INFO] d9 changed at tick="<<tick<<"\n"; break; }
        prev_queued = cur_queued;
        prev_d9 = cur_d9;
        ++commit_steps;
    }
    if (!committed) std::cerr << "[WARN] write_reg_pulse: commit not observed before per-write timeout\n";

    if (!wait_for_edge_generic(top,tfp,tick,raw_samples,prev_strb,strobe_count,s_min,s_max,reader_phi,0,nullptr,(uint64_t)-1)) {}
    top->i_WR_n = 1; top->i_CS_n = 1; top->eval();
    top->i_A0 = A0_ACTIVE_HIGH ? 0 : 1; top->eval();
    wait_for_edge_generic(top,tfp,tick,raw_samples,prev_strb,strobe_count,s_min,s_max,reader_phi,1,nullptr,(uint64_t)-1);
#ifdef HAS_D_OE_PORT
    top->i_D_OE = 0;
#else
    top->i_D = 0;
#endif
    top->eval();
    for (int i=0;i<1;++i) step_half(top, tfp, tick, raw_samples, prev_strb, strobe_count, s_min, s_max);

    return committed;
}

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);
    if (argc > 1) sim_cycles = strtoull(argv[1], nullptr, 0);
    if (argc > 2) emuclk_hz = atof(argv[2]);
    if (argc > 3) audio_sample_rate = (uint32_t)atoi(argv[3]);

    const char* csvpath = (argc >= 5) ? argv[4] : "events.csv";
    if (argc >= 6) reset_half_cycles = strtoull(argv[5], nullptr, 0);

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i],"--pending-wait")==0 && i+1<argc) PENDING_WAIT = strtoull(argv[++i],nullptr,0);
        else if (strncmp(argv[i],"--pending-wait=",14)==0) PENDING_WAIT = strtoull(argv[i]+14,nullptr,0);
        else if (strcmp(argv[i],"--queued-assert-wait")==0 && i+1<argc) QUEUED_ASSERT_WAIT = strtoull(argv[++i],nullptr,0);
        else if (strncmp(argv[i],"--queued-assert-wait=",21)==0) QUEUED_ASSERT_WAIT = strtoull(argv[i]+21,nullptr,0);
        else if (strcmp(argv[i],"--commit-wait")==0 && i+1<argc) COMMIT_WAIT = strtoull(argv[++i],nullptr,0);
        else if (strncmp(argv[i],"--commit-wait=",14)==0) COMMIT_WAIT = strtoull(argv[i]+14,nullptr,0);
        else if (strcmp(argv[i],"--safety-wait")==0 && i+1<argc) GLOBAL_SAFETY_WAIT = strtoull(argv[++i],nullptr,0);
        else if (strncmp(argv[i],"--safety-wait=",14)==0) GLOBAL_SAFETY_WAIT = strtoull(argv[i]+14,nullptr,0);
        else if (strcmp(argv[i],"--verbose")==0) { VERBOSE = true; ENABLE_VDBG_PEND_MON = true; }
        else if (strncmp(argv[i],"--trace-level=",14)==0) TRACE_LEVEL = atoi(argv[i]+14);
        else if (strncmp(argv[i],"--a0-active-high=",17)==0) A0_ACTIVE_HIGH = atoi(argv[i]+17)!=0;
        else if (strncmp(argv[i],"--wr-hold-pos-edges=",21)==0) WR_HOLD_POS_EDGES = atoi(argv[i]+21);
        else if (strncmp(argv[i],"--phimref-invert=",17)==0) PHIMREF_INVERT = atoi(argv[i]+17)!=0;
        else if (strcmp(argv[i],"--help")==0) {
            std::cout << "Usage: " << argv[0] << " [sim_cycles] [emuclk_hz] [audio_rate] [events.csv] [reset_half_cycles] [flags]\n";
            return 0;
        }
    }

#if VM_TRACE
    Verilated::traceEverOn(true);
#endif

    vcd_time_ps_per_tick = (uint64_t)std::llround(1e12 / (2.0 * emuclk_hz));
    std::cerr << "[DBG] vcd_time_ps_per_tick="<<vcd_time_ps_per_tick<<" ps/tick\n";
    std::cerr << "[DBG] pending_wait="<<PENDING_WAIT<<" commit_wait="<<COMMIT_WAIT<<" safety_wait="<<GLOBAL_SAFETY_WAIT<<" verbose="<<VERBOSE<<"\n";

    auto events = read_events_from_csv(csvpath);
    std::cerr << "[DBG] events read: " << events.size() << " entries\n";

    double iter_per_sample = (emuclk_hz * 2.0) / (double)audio_sample_rate;

    VIKAOPLL_vltb* top = new VIKAOPLL_vltb;
#if VM_TRACE
    VerilatedVcdC* tfp = new VerilatedVcdC; top->trace(tfp, TRACE_LEVEL); tfp->open("dump.vcd");
#else
    VerilatedVcdC* tfp = nullptr;
#endif

    top->i_CS_n = 1; top->i_WR_n = 1;
    top->i_A0 = A0_ACTIVE_HIGH ? 0 : 1; top->i_D = 0;
    top->i_ACC_SIGNED_MOVOL = 0x1F; top->i_ACC_SIGNED_ROVOL = 0x1F;
    top->i_XIN_EMUCLK = 0;

    std::vector<int32_t> raw_samples; raw_samples.reserve(800000);
    uint64_t tick = 0; bool prev_strb = false; uint64_t strobe_count = 0;
    int32_t s_min = std::numeric_limits<int32_t>::max(); int32_t s_max = std::numeric_limits<int32_t>::min();

    std::cerr << "[DBG] applying reset for " << reset_half_cycles << " half-cycles\n";
    for (uint64_t i=0;i<reset_half_cycles;++i) step_half(top,tfp,tick,raw_samples,prev_strb,strobe_count,s_min,s_max);
    std::cerr << "[DBG] reset done tick="<<tick<<"\n";

    std::vector<uint64_t> event_ticks; event_ticks.reserve(events.size());
    for (auto &e: events) {
        uint64_t t = (uint64_t)std::llround((double)e.sample_tick * iter_per_sample);
        event_ticks.push_back(tick + t);
    }
    std::cerr << "[DBG] scheduling " << event_ticks.size() << " events\n";

    size_t next_event_idx = 0;
    const int POST_SAMPLES = 64;
    const uint64_t POST_TICKS = (uint64_t)std::llround(iter_per_sample * POST_SAMPLES);

    while ((!Verilated::gotFinish()) && (tick < sim_cycles || next_event_idx < events.size())) {
        while (next_event_idx < events.size() && event_ticks[next_event_idx] <= tick) {
            uint64_t cur_evt_tick = event_ticks[next_event_idx];
            size_t batch_start = next_event_idx, batch_end = batch_start;
            while (batch_end < events.size() && event_ticks[batch_end] == cur_evt_tick) ++batch_end;
            if (VERBOSE) std::cerr << "[SCHED] batch ev#" << batch_start << "-" << (batch_end-1) << " tick_now="<<tick<<"\n";
            for (size_t i=batch_start;i<batch_end;++i) {
                if (VERBOSE) std::cerr << "[SCHED] ev#" << i << " addr=0x"<<std::hex<<int(events[i].reg)<<" data=0x"<<int(events[i].data)<<std::dec<<"\n";
                bool ok = write_reg_pulse(top,tfp,tick,raw_samples,prev_strb,strobe_count,s_min,s_max, events[i].reg, events[i].data);
                if (!ok) std::cerr << "[WARN] write_reg_pulse timed out for addr=0x"<<std::hex<<int(events[i].reg)<<" data=0x"<<int(events[i].data)<<std::dec<<"\n";
            }
            for (uint64_t i=0;i<POST_TICKS;++i) step_half(top,tfp,tick,raw_samples,prev_strb,strobe_count,s_min,s_max);
            next_event_idx = batch_end;
        }
        if (tick >= sim_cycles && next_event_idx >= events.size()) break;
        step_half(top,tfp,tick,raw_samples,prev_strb,strobe_count,s_min,s_max);
    }

    if (!raw_samples.empty()) write_wav_scaled("out_from_vgm.wav", raw_samples, audio_sample_rate);
    else std::cerr << "[SUMMARY] no samples captured\n";

    std::cerr << "[FINAL_REGS] o_FNUM=" << std::hex << (int)top->o_FNUM
              << " o_BLOCK=" << std::dec << (int)top->o_BLOCK
              << " o_KON=" << (int)top->o_KON
              << " o_TL=" << (int)top->o_TL
              << " o_MUL=" << std::hex << (int)top->o_MUL << std::dec << "\n";

#if VM_TRACE
    if (tfp) { tfp->close(); delete tfp; tfp = nullptr; }
#endif
    top->final();
    delete top;
    return 0;
}

