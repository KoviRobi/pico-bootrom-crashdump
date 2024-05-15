/**
 * Copyright (c) 2020 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <stdio.h>
#include "pico/stdlib.h"

#include "hardware/structs/psm.h"
#include "hardware/watchdog.h"

extern uint32_t __crash____vectors;
extern void __crash___entry_point;

int main() {
    void (*cep)(void) = &__crash___entry_point;
    cep();
    stdio_init_all();
    int i = 0;
    while (true) {
        printf("Hello, world!\n");
        sleep_ms(1000);
        if (i++ == 2) {
            // TODO: Watchdog normally would reset XIP, for now we just bypass
            // it. We could perhaps copy/fixup the relevant boot bits into RAM
            // and jump to that, but for now we don't
            psm_hw->wdsel =
              PSM_WDSEL_PROC0_BITS | PSM_WDSEL_PROC1_BITS;
            watchdog_reboot((uint32_t)&__crash___entry_point, __crash____vectors, 1);
        }
    }
}
