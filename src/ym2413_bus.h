#ifndef YM2413_BUS_H
#define YM2413_BUS_H
#include <stdint.h>
#include "ikaopll_wrapper.h"
typedef struct {
    uint64_t origin_cycle;
    uint64_t last_write_cycle;
    uint32_t last_wait_cycles;
    uint64_t accesses;
    uint64_t late_events;
    uint64_t max_late_cycles;
    uint64_t scheduled_sample;
} ym2413_bus_t;
void ym2413_bus_init(ym2413_bus_t* bus);
void ym2413_bus_wait_until_sample(ym2413_bus_t* bus, uint64_t sample);
void ym2413_bus_write_addr(ym2413_bus_t* bus, uint8_t addr);
void ym2413_bus_write_data(ym2413_bus_t* bus, uint8_t data);
int ym2413_bus_debug_open(const char* path);
void ym2413_bus_debug_close(void);
void ym2413_bus_report(const ym2413_bus_t* bus);
#endif
