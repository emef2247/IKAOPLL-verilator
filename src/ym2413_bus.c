#include "ym2413_bus.h"

#include <stdio.h>

/*-------------------------------------------------------------------------
 * 内部ヘルパ: EMUCLK 1周期進めつつ、clkdiv/phiMref/phiM_cnt を TB と同じように更新
 *
 * TB では:
 *   always @(posedge EMUCLK) begin
 *       if(clkdiv == 3) begin clkdiv <= 0; phiMref <= 1; end
 *       else begin
 *           clkdiv <= clkdiv + 1;
 *           if(clkdiv == 1) phiMref <= 0;
 *       end
 *   end
 *
 *   always @(posedge EMUCLK or negedge IC_n) begin
 *       if(!IC_n) phiM_cnt <= 0;
 *       else if(phiMref) phiM_cnt <= phiM_cnt + 1;
 *   end
 *
 * なので、ここでは EMUCLK 立ち上がり側で同じ処理を行う。
 *------------------------------------------------------------------------*/

static void ym2413_bus_step_emuclk_1cycle(ym2413_bus_t* bus)
{
    if (!bus) return;

    /* posedge */
    ikaopll_step_emuclk_posedge();

    /* clkdiv / phiMref 更新 (TB と同じロジック) */
    if (bus->clkdiv == 3) {
        bus->clkdiv = 0;
        bus->phiMref = 1;
    } else {
        bus->clkdiv = (uint8_t)(bus->clkdiv + 1u);
        if (bus->clkdiv == 1) {
            bus->phiMref = 0;
        }
    }

    /* phiM_cnt 更新: phiMref==1 の posedge ごとに +1 */
    if (bus->phiMref) {
        bus->phiM_cnt += 1;
    }

    /* negedge */
    ikaopll_step_emuclk_negedge();
}

/* φM posedge を1回待つ (TB: @(posedge phiMref)) */
static void ym2413_bus_wait_phiM_posedge(ym2413_bus_t* bus)
{
    if (!bus) return;
    uint8_t last;
    do {
        last = bus->phiMref;
        ym2413_bus_step_emuclk_1cycle(bus);
    } while (!(last == 0 && bus->phiMref == 1));
}

/* φM negedge を1回待つ (TB: @(negedge phiMref)) */
static void ym2413_bus_wait_phiM_negedge(ym2413_bus_t* bus)
{
    if (!bus) return;
    uint8_t last;
    do {
        last = bus->phiMref;
        ym2413_bus_step_emuclk_1cycle(bus);
    } while (!(last == 1 && bus->phiMref == 0));
}

/* φM posedge を n 回待つ (TB: wait_phiM_cycles) */
static void ym2413_bus_wait_phiM_cycles(ym2413_bus_t* bus, int32_t n)
{
    if (!bus) return;
    if (n <= 0) return;

    for (int32_t i = 0; i < n; ++i) {
        ym2413_bus_wait_phiM_posedge(bus);
    }
}

/*-------------------------------------------------------------------------
 * 公開 API
 *------------------------------------------------------------------------*/

void ym2413_bus_init(ym2413_bus_t* bus)
{
    if (!bus) return;

    bus->phiM_cnt      = 0;
    bus->clkdiv        = 0;
    bus->phiMref       = 0;

    bus->last_op_kind  = YM2413_LAST_NONE;
    bus->last_op_phiM  = 0;

    bus->min_wait_addr = 12;  /* MIN_WAIT_ADDR */
    bus->min_wait_data = 84;  /* MIN_WAIT_DATA */
}

/* φM カウンタを n_phiM 増やす */
void ym2413_bus_step_phiM_cycles(ym2413_bus_t* bus, uint32_t n_phiM)
{
    if (!bus) return;

    for (uint32_t i = 0; i < n_phiM; ++i) {
        ym2413_bus_wait_phiM_posedge(bus);
    }
}

/* WAIT ルール適用 (TB: last_op_kind / last_op_phiM / MIN_WAIT_*) */
static void ym2413_bus_enforce_wait(ym2413_bus_t* bus, ym2413_last_op_t next_kind)
{
    if (!bus) return;

    int32_t need_wait = 0;

    switch (bus->last_op_kind) {
    case YM2413_LAST_ADDR:
        need_wait = bus->min_wait_addr;
        break;
    case YM2413_LAST_DATA:
        need_wait = bus->min_wait_data;
        break;
    default:
        need_wait = 0;
        break;
    }

    if (need_wait <= 0) {
        /* 新しい種別だけ記録しておく (phiM_cnt の更新は呼び出し元で行う) */
        bus->last_op_kind = next_kind;
        bus->last_op_phiM = bus->phiM_cnt;
        return;
    }

    int32_t now_phiM = bus->phiM_cnt;
    int32_t diff     = now_phiM - bus->last_op_phiM;

    if (diff < need_wait) {
        int32_t remain = need_wait - diff;
        /* デバッグしたければ以下を有効化
        printf("[ym2413_bus] wait: last=%d diff=%d need=%d -> wait %d phiM (cnt=%d)\n",
               bus->last_op_kind, diff, need_wait, remain, bus->phiM_cnt);
        */
        ym2413_bus_wait_phiM_cycles(bus, remain);
    }

    /* 新しい種別・時刻を記録 */
    bus->last_op_kind = next_kind;
    bus->last_op_phiM = bus->phiM_cnt;
}

/* アドレス書き込み (TB: IKAOPLL_write で i_TARGET_ADDR=0 のケース) */
void ym2413_bus_write_addr(ym2413_bus_t* bus, uint8_t addr)
{
    if (!bus) return;

    /* アドレスアクセス前に WAIT を強制 */
    ym2413_bus_enforce_wait(bus, YM2413_LAST_ADDR);

    /* 以下、TB の IKAOPLL_write と同じシーケンスを φM エッジに沿って実装 */

    /* @(posedge phiMref) A0 = 0; */
    ym2413_bus_wait_phiM_posedge(bus);
    ikaopll_set_A0(0);

    /* @(negedge phiMref) CS_n = 0; */
    ym2413_bus_wait_phiM_negedge(bus);
    ikaopll_set_CS_n(0);

    /* @(posedge phiMref) DIN  = addr; */
    ym2413_bus_wait_phiM_posedge(bus);
    ikaopll_set_D(addr);

    /* @(negedge phiMref) WR_n = 0; */
    ym2413_bus_wait_phiM_negedge(bus);
    ikaopll_set_WR_n(0);

    /* @(posedge phiMref); */
    ym2413_bus_wait_phiM_posedge(bus);

    /* @(negedge phiMref) begin WR_n = 1; CS_n = 1; end */
    ym2413_bus_wait_phiM_negedge(bus);
    ikaopll_set_WR_n(1);
    ikaopll_set_CS_n(1);

    /* @(posedge phiMref) DIN  = 8'h00; */
    ym2413_bus_wait_phiM_posedge(bus);
    ikaopll_set_D(0x00);
}

/* データ書き込み (TB: IKAOPLL_write で i_TARGET_ADDR=1 のケース) */
void ym2413_bus_write_data(ym2413_bus_t* bus, uint8_t data)
{
    if (!bus) return;

    /* データアクセス前に WAIT を強制 */
    ym2413_bus_enforce_wait(bus, YM2413_LAST_DATA);

    /* IKAOPLL_write の i_TARGET_ADDR=1 の場合も、バスシーケンス自体は同じで、
       A0 に 1 を書き込むだけ違う。 */

    /* @(posedge phiMref) A0 = 1; */
    ym2413_bus_wait_phiM_posedge(bus);
    ikaopll_set_A0(1);

    /* @(negedge phiMref) CS_n = 0; */
    ym2413_bus_wait_phiM_negedge(bus);
    ikaopll_set_CS_n(0);

    /* @(posedge phiMref) DIN  = data; */
    ym2413_bus_wait_phiM_posedge(bus);
    ikaopll_set_D(data);

    /* @(negedge phiMref) WR_n = 0; */
    ym2413_bus_wait_phiM_negedge(bus);
    ikaopll_set_WR_n(0);

    /* @(posedge phiMref); */
    ym2413_bus_wait_phiM_posedge(bus);

    /* @(negedge phiMref) begin WR_n = 1; CS_n = 1; end */
    ym2413_bus_wait_phiM_negedge(bus);
    ikaopll_set_WR_n(1);
    ikaopll_set_CS_n(1);

    /* @(posedge phiMref) DIN  = 8'h00; */
    ym2413_bus_wait_phiM_posedge(bus);
    ikaopll_set_D(0x00);
}

