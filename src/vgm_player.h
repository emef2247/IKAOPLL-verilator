#ifndef VGM_PLAYER_H
#define VGM_PLAYER_H

#include <stdint.h>
#include "ym2413_bus.h"

/* VGM CSV 1 行を表すイベント */
typedef struct {
    uint32_t delay_samples; /* VGM サンプル数での「この行の前の待ち時間」 */
    uint8_t  is_addr;       /* 1 = アドレス (reg=="01"), 0 = データ (reg=="00") */
    uint8_t  data;          /* 書き込む 8bit データ */
} vgm_csv_event_t;

/* YM2413 用 VGM CSV を読み込み、その内容を ym2413_bus 経由で IKAOPLL へ流す。
   - path: CSV ファイルパス (例: "vgm_data/tests/ym2413_scale_chromatic.vgm.csv")
   - bus : すでに初期化済みの ym2413_bus_t

   戻り値: 0 = 成功, 非0 = エラー
*/
int vgm_player_run_csv(const char* path, ym2413_bus_t* bus);

#endif /* VGM_PLAYER_H */

