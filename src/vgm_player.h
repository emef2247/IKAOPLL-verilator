#ifndef VGM_PLAYER_H
#define VGM_PLAYER_H
#include "ym2413_bus.h"
int vgm_player_run_vgm(const char* path, ym2413_bus_t* bus, const char* event_csv_path);
#endif
