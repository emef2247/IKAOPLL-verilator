#include <stdio.h>
#include <stdint.h>

#include "ikaopll_wrapper.h"

int main(int argc, char** argv)
{
    (void)argc;
    (void)argv;

    printf("IKAOPLL-verilator: minimal VGM CSV driver (skeleton)\n");

    ikaopll_init();
    ikaopll_reset();

    /* とりあえず数サイクルだけ進めてみる */
    ikaopll_step(10);

    printf("Simulation finished (skeleton run).\n");

    return 0;
}

