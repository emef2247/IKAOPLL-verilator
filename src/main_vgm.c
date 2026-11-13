#include "ikaopll_wrapper.h"
#include "ym2413_bus.h"
#include "vgm_player.h"
#include "vgm_parser.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>
#include <string.h>
#include <strings.h>
#include <string>
#include <vector>
#include <limits.h>
#include <sys/stat.h>

static std::string resolved_path(const std::string& path) {
    char buffer[PATH_MAX];
    if (realpath(path.c_str(),buffer)) return buffer;
    const size_t slash = path.find_last_of('/');
    const std::string parent = slash == std::string::npos ? "." : (slash == 0 ? "/" : path.substr(0,slash));
    if (!realpath(parent.c_str(),buffer)) return path;
    return std::string(buffer)+"/"+path.substr(slash == std::string::npos ? 0 : slash+1);
}
static bool same_file(const std::string& a, const std::string& b) {
    struct stat sa, sb;
    if (stat(a.c_str(),&sa) == 0 && stat(b.c_str(),&sb) == 0 &&
        sa.st_dev == sb.st_dev && sa.st_ino == sb.st_ino) return true;
    return resolved_path(a) == resolved_path(b);
}
static void usage(const char* name) {
    printf("Usage: %s INPUT.vgm [--output FILE.wav] [--sample-rate 44100|48000]\n",name);
    printf("  --enable-csv     raw ACC, output PCM, and source event CSVs\n");
    printf("  --debug [FILE]   source timestamp / actual bus-clock log\n");
    printf("  --vcd [FILE]     full RTL trace (large; disabled by default)\n");
}
static bool is_vgm(const char* path) {
    const size_t n = strlen(path);
    return n >= 4 && (!strcasecmp(path+n-4,".vgm") || !strcasecmp(path+n-4,".vgz"));
}
int main(int argc, char** argv) {
    const char* input = NULL; const char* output = NULL;
    const char* debug = NULL; const char* vcd = NULL;
    bool csv = false; uint32_t clock = 3579545; int rate = 44100;
    for (int i = 1; i < argc; ++i) {
        const char* arg = argv[i];
        if (!strcmp(arg,"--help") || !strcmp(arg,"-h")) { usage(argv[0]); return 0; }
        if (!strcmp(arg,"--enable-csv")) csv = true;
        else if (!strcmp(arg,"--no-csv")) csv = false;
        else if (!strcmp(arg,"--vcd") || !strcmp(arg,"--debug")) {
            const char* name = !strcmp(arg,"--vcd") ? "ikaopll_dump.vcd" : "ym2413_bus_calls.log";
            if (i+1 < argc && argv[i+1][0] != '-') name = argv[++i];
            if (!strcmp(arg,"--vcd")) vcd = name; else debug = name;
        } else if (!strcmp(arg,"--output") || !strcmp(arg,"--sample-rate")) {
            if (i+1 >= argc) { fprintf(stderr,"Missing value for %s\n",arg); return 1; }
            const char* value = argv[++i];
            if (!strcmp(arg,"--output")) output = value;
            else {
                char* end = NULL; const unsigned long n = strtoul(value,&end,10);
                if (!*value || *end || n == 0 || n > 100000000) { fprintf(stderr,"Invalid %s\n",arg); return 1; }
                if (n == 44100 || n == 48000) rate = (int)n;
                else { fprintf(stderr,"Sample rate must be 44100 or 48000\n"); return 1; }
            }
        } else if (arg[0] == '-' || input) { fprintf(stderr,"Unexpected argument: %s\n",arg); return 1; }
        else input = arg;
    }
    if (!input) { usage(argv[0]); return 1; }
    if (!is_vgm(input)) {
        fprintf(stderr,"Input must be a .vgm or .vgz file (CSV event input is no longer supported)\n"); return 1;
    }
    if (vgm_read_ym2413_clock(input,&clock)) {
        fprintf(stderr,"Cannot read a supported single-YM2413 VGM header (dual-chip/VRC7 flags unsupported)\n"); return 1;
    }
    if (ikaopll_set_clock_hz(clock)) { fprintf(stderr,"Invalid master clock\n"); return 1; }
    std::string wav;
    if (output) wav = output;
    else {
        wav = input; const size_t slash = wav.find_last_of("/\\");
        if (slash != std::string::npos) wav.erase(0,slash+1);
        const size_t dot = wav.find_last_of('.');
        if (dot != std::string::npos) wav.erase(dot);
        wav += ".wav";
    }
    if (wav == input) { fprintf(stderr,"Output must differ from input\n"); return 1; }
    std::string base = wav;
    if (base.size() >= 4 && !strcasecmp(base.c_str()+base.size()-4,".wav")) base.resize(base.size()-4);
    // Validate all destinations before trace/debug/diagnostic files can open.
    std::vector<std::string> paths;
    paths.push_back(input); paths.push_back(wav);
    if (vcd) paths.push_back(vcd);
    if (debug) paths.push_back(debug);
    if (csv) {
        paths.push_back(base+".audio.csv"); paths.push_back(base+".acc.csv");
        paths.push_back(base+".events.csv");
    }
    for (size_t i = 1; i < paths.size(); ++i) {
        for (size_t j = 0; j < i; ++j) {
            if (same_file(paths[i],paths[j])) {
                fprintf(stderr,"Input/output paths must be distinct: %s and %s\n",paths[j].c_str(),paths[i].c_str());
                return 1;
            }
        }
    }
    printf("IKAOPLL RTL simulation: %s\n",input);
    printf("Master clock=%u Hz, native PCM=clock/72 (%.6f Hz), WAV=%d Hz\n",clock,clock/72.0,rate);
    printf("RTL parameters: FULLY_SYNCHRONOUS=1 FAST_RESET=1 USE_PIPELINED_MULTIPLIER=1\n");
    ikaopll_init(); if (vcd) ikaopll_trace_init(vcd);
    ikaopll_reset();
    ym2413_bus_t bus; ym2413_bus_init(&bus);
    ikaopll_audio_wav_open(wav.c_str(),rate);
    if ((csv && ikaopll_audio_csv_open((base+".audio.csv").c_str(),(base+".acc.csv").c_str())) ||
        (debug && ym2413_bus_debug_open(debug))) {
        fprintf(stderr,"Cannot open diagnostic output\n"); ikaopll_audio_wav_abort(); ikaopll_release(); return 1;
    }
    const uint64_t start_ps = ikaopll_get_sim_time();
    const std::string events = base+".events.csv";
    const int rc = vgm_player_run_vgm(input,&bus,csv ? events.c_str() : NULL);
    if (rc) {
        fprintf(stderr,"Input rendering failed (%d)\n",rc);
        ikaopll_audio_wav_abort(); ym2413_bus_debug_close(); ikaopll_release(); return 1;
    }
    ym2413_bus_report(&bus); ym2413_bus_debug_close();
    const uint64_t ticks = ikaopll_get_sim_time()-start_ps;
    printf("Simulation finished. sim_time = %" PRIu64 " ticks (%.6f s)\n",ticks,ticks*1e-12);
    const int wav_rc = ikaopll_audio_wav_close(); ikaopll_release(); return wav_rc ? 1 : 0;
}
