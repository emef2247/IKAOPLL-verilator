#include "ym2413_bus.h"
#include <stdio.h>
#include <inttypes.h>
#include <string.h>
static FILE* g_debug = NULL;
static void step_to(uint64_t deadline) {
    while (ikaopll_get_clock_count() < deadline) ikaopll_step_emuclk_1cycle();
}
void ym2413_bus_init(ym2413_bus_t* bus) {
    memset(bus, 0, sizeof(*bus));
    bus->origin_cycle = ikaopll_get_clock_count();
}
void ym2413_bus_wait_until_sample(ym2413_bus_t* bus, uint64_t sample) {
    // VGM time is absolute: bus service time is already part of this interval.
    const uint64_t hz = ikaopll_get_clock_hz();
    const uint64_t cycle = (sample / 44100) * hz + ((sample % 44100) * hz + 44099) / 44100;
    step_to(bus->origin_cycle + cycle);
    bus->scheduled_sample = sample;
}
static void write_port(ym2413_bus_t* bus, uint8_t a0, uint8_t value) {
    // Master clock = XIN = phiM (PCEN_n is tied low). No virtual /4 clock.
    if (bus->last_wait_cycles) step_to(bus->last_write_cycle + bus->last_wait_cycles);
    ikaopll_set_A0(a0); ikaopll_set_D(value);
    ikaopll_step_emuclk_1cycle();
    ikaopll_set_CS_n(0); ikaopll_set_WR_n(0);
    // Cover the synchronous bus-input stages with a four-clock write pulse.
    for (int i = 0; i < 4; ++i) ikaopll_step_emuclk_1cycle();
    ikaopll_set_WR_n(1); ikaopll_set_CS_n(1);
    bus->last_write_cycle = ikaopll_get_clock_count();
    bus->last_wait_cycles = a0 ? 84 : 12;
    const uint64_t actual = bus->last_write_cycle - bus->origin_cycle;
    const uint64_t hz = ikaopll_get_clock_hz();
    const uint64_t target = (bus->scheduled_sample / 44100) * hz +
                          ((bus->scheduled_sample % 44100) * hz + 44099) / 44100;
    const uint64_t late = actual > target ? actual - target : 0;
    if (late) ++bus->late_events;
    if (late > bus->max_late_cycles) bus->max_late_cycles = late;
    ++bus->accesses;
    if (g_debug) fprintf(g_debug, "%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%s,0x%02X\n",
                         bus->accesses, bus->scheduled_sample, actual, late, a0 ? "DATA" : "ADDR", value);
    ikaopll_step_emuclk_1cycle();
}
void ym2413_bus_write_addr(ym2413_bus_t* bus, uint8_t addr) { write_port(bus, 0, addr); }
void ym2413_bus_write_data(ym2413_bus_t* bus, uint8_t data) { write_port(bus, 1, data); }
int ym2413_bus_debug_open(const char* path) {
    g_debug = fopen(path, "w");
    if (!g_debug) return -1;
    fprintf(g_debug, "access_idx,vgm_sample,actual_clock,late_clocks,op,value_hex\n"); return 0;
}
void ym2413_bus_debug_close(void) { if (g_debug) fclose(g_debug); g_debug = NULL; }
void ym2413_bus_report(const ym2413_bus_t* bus) {
    printf("Bus writes=%" PRIu64 ", delayed writes=%" PRIu64 ", max lateness=%" PRIu64 " clocks (%.3f ms)\n",
            bus->accesses, bus->late_events, bus->max_late_cycles,
            bus->max_late_cycles * 1000.0 / ikaopll_get_clock_hz());
}
