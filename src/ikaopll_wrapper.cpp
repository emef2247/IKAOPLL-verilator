#include "ikaopll_wrapper.h"

#include <cstdio>
#include <cstdlib>

#include "verilated.h"
#include "VIKAOPLL.h"

/* グローバルな Verilated モデルインスタンス */
static VIKAOPLL* g_top = nullptr;

/* シミュレーション時間（とりあえずダミーで保持） */
static vluint64_t g_main_time = 0;

void ikaopll_init(void)
{
    if (g_top != nullptr) {
        return; /* すでに初期化済み */
    }

    Verilated::traceEverOn(false); /* トレースは当面オフ */

    g_top = new VIKAOPLL();

    /* 必要ならここでポート初期値を設定 */
    /* 例:
        g_top->EMUCLK = 0;
       など。ポート名は IKAOPLL.v を見て後で合わせる。
    */

    g_main_time = 0;
}

void ikaopll_reset(void)
{
    /* TODO: IKAOPLL のポート構成を見て、
       RESET 信号や初期化シーケンスをここに実装する。

       仮の実装としては何もせず、のちほど IKAOPLL_vgm_tb.sv の
       リセットシーケンスを参考に flesh up する。
    */
}

void ikaopll_step(uint32_t emuclk_cycles)
{
    if (g_top == nullptr) {
        fprintf(stderr, "ikaopll_step: not initialized. Call ikaopll_init() first.\n");
        return;
    }

    /* 仮実装:
       EMUCLK という名前のクロックポートがある前提で、
       emuclk_cycles 回 トグルしながら eval() を呼ぶ。
       実際のポート名に応じて後で修正する。
    */

    for (uint32_t i = 0; i < emuclk_cycles; ++i) {
        // 立ち上がり
        // g_top->EMUCLK = 1;
        g_top->eval();
        ++g_main_time;

        // 立ち下がり
        // g_top->EMUCLK = 0;
        g_top->eval();
        ++g_main_time;
    }
}

