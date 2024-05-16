/**
 * Copyright (c) 2020 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "hardware/structs/clocks.h"
#include "hardware/structs/pll.h"
#include "hardware/structs/rosc.h"
#include "hardware/structs/sio.h"
#include "hardware/structs/ssi.h"
#include "hardware/structs/watchdog.h"
#include "hardware/structs/xip_ctrl.h"
#include "hardware/structs/xosc.h"
#include "hardware/sync.h"
#include "hardware/resets.h"
#include "usb_boot_device.h"
#include "resets.h"

#include "async_task.h"
#include "bootrom_crc32.h"
#include "runtime.h"
#include "hardware/structs/usb.h"

// From SDF + STA, plus 20% margin each side
// CLK_SYS FREQ ON STARTUP (in MHz)
// +-----------------------
// | min    |  1.8        |
// | typ    |  6.5        |
// | max    |  11.3       |
// +----------------------+
#define ROSC_MHZ_MAX 12

// Each attempt takes around 4 ms total with a 6.5 MHz boot clock
#define FLASH_MAX_ATTEMPTS 128

#define BOOT2_SIZE_BYTES 256
#define BOOT2_FLASH_OFFS 0
#define BOOT2_MAGIC 0x12345678
#define BOOT2_BASE (SRAM_END - BOOT2_SIZE_BYTES)

uint32_t usb_activity_gpio_pin_mask;

#include "hardware/regs/m0plus.h"
#include "hardware/structs/clocks.h"
#include "hardware/structs/psm.h"
#include "hardware/structs/sio.h"
#include "hardware/structs/watchdog.h"
#include "hardware/resets.h"
#include "hardware/regs/pads_bank0.h"
#include "hardware/structs/iobank0.h"

static inline io_rw_32 *GPIO_CTRL_REG(uint x) {
    uint32_t offset = 0;

    // removed since we know it is in range
//    if (x >=  0 && x < 32)
    offset = IO_BANK0_BASE + (x * 8) + 4;

    return (io_rw_32 *) offset;
}

static inline io_rw_32 *PAD_CTRL_REG(uint x) {
    uint32_t offset = 0;

    // removed since we know it is in range
//    if (x >=  0 && x < 32)
    offset = PADS_BANK0_BASE + PADS_BANK0_GPIO0_OFFSET + (x * 4);

    return (io_rw_32 *) offset;
}

void gpio_funcsel(uint i, int fn) {
    io_rw_32 *pad_ctl = PAD_CTRL_REG(i);
    // we are only enabling output, so just clear the OD
    hw_clear_bits(pad_ctl, PADS_BANK0_GPIO0_OD_BITS);
    // Set the funcsel
    *GPIO_CTRL_REG(i) = (fn << IO_BANK0_GPIO0_CTRL_FUNCSEL_LSB);
}


void gpio_setup() {
    if (usb_activity_gpio_pin_mask) {
        unreset_block(RESETS_RESET_IO_BANK0_BITS);
        sio_hw->gpio_set = usb_activity_gpio_pin_mask;
        sio_hw->gpio_oe_set = usb_activity_gpio_pin_mask;
        // need pin number rather than mask
        gpio_funcsel(ctz32(usb_activity_gpio_pin_mask), 5);
    }
#ifndef NDEBUG
    // Set to RIO for debug
    for (int i = 19; i < 23; i++) {
        gpio_init(i);
        gpio_dir_out_mask(1 << i);
    }
#endif
}

void interrupt_enable(uint irq, bool enable) {
    assert(irq < N_IRQS);
    if (enable) {
        // Clear pending before enable
        // (if IRQ is actually asserted, it will immediately re-pend)
        *(volatile uint32_t *) (PPB_BASE + M0PLUS_NVIC_ICPR_OFFSET) = 1u << irq;
        *(volatile uint32_t *) (PPB_BASE + M0PLUS_NVIC_ISER_OFFSET) = 1u << irq;
    } else {
        *(volatile uint32_t *) (PPB_BASE + M0PLUS_NVIC_ICER_OFFSET) = 1u << irq;
    }
}

// USB bootloader requires clk_sys and clk_usb at 48 MHz. For this to work,
// xosc must be running at 12 MHz. It is possible that:
//
// - No crystal is present (and XI may not be properly grounded)
// - xosc output is much greater than 12 MHz
//
// In this case we *must* leave clk_sys in a safe state, and ideally, never
// return from this function. This is because boards which are not designed to
// use USB will still enter the USB bootcode when booted with a blank flash.

static void _usb_clock_setup() {
    // First make absolutely sure clk_ref is running: needed for resuscitate,
    // and to run clk_sys while configuring sys PLL. Assume that rosc is not
    // configured to run faster than clk_sys max (as this is officially out of
    // spec)
    // If user previously configured clk_ref to a different source (e.g.
    // GPINx), then halted that source, the glitchless mux can't switch away
    // from the dead source-- nothing we can do about this here.
    rosc_hw->ctrl = ROSC_CTRL_ENABLE_VALUE_ENABLE << ROSC_CTRL_ENABLE_LSB;
    hw_clear_bits(&clocks_hw->clk[clk_ref].ctrl, CLOCKS_CLK_REF_CTRL_SRC_BITS);

    // Resuscitate logic will switch clk_sys to clk_ref if it is inadvertently stopped
    clocks_hw->resus.ctrl =
            CLOCKS_CLK_SYS_RESUS_CTRL_ENABLE_BITS |
            (CLOCKS_CLK_SYS_RESUS_CTRL_TIMEOUT_RESET
                    << CLOCKS_CLK_SYS_RESUS_CTRL_TIMEOUT_LSB);

    // Resetting PLL regs or changing XOSC range can glitch output, so switch
    // clk_sys away before touching. Not worried about clk_usb as USB is held
    // in reset.
    hw_clear_bits(&clocks_hw->clk[clk_sys].ctrl, CLOCKS_CLK_SYS_CTRL_SRC_BITS);
    while (!(clocks_hw->clk[clk_sys].selected & 1u));
    // rosc can not (while following spec) run faster than clk_sys max, so
    // it's safe now to clear dividers in clkslices.
    clocks_hw->clk[clk_sys].div = 0x100; // int 1 frac 0
    clocks_hw->clk[clk_usb].div = 0x100;

    // Try to get the crystal running. If no crystal is present, XI should be
    // grounded, so STABLE counter will never complete. Poor designs might
    // leave XI floating, in which case we may eventually drop through... in
    // this case we rely on PLL not locking, and/or resuscitate counter.
    //
    // Don't touch range setting: user would only have changed if crystal
    // needs it, and running crystal out of range can produce glitchy output.
    // Note writing a "bad" value (non-aax) to RANGE has no effect.
    xosc_hw->ctrl = XOSC_CTRL_ENABLE_VALUE_ENABLE << XOSC_CTRL_ENABLE_LSB;
    while (!(xosc_hw->status & XOSC_STATUS_STABLE_BITS));

    // Sys PLL setup:
    // - VCO freq 1200 MHz, so feedback divisor of 100. Range is 400 MHz to 1.6 GHz
    // - Postdiv1 of 5, down to 240 MHz (appnote recommends postdiv1 >= postdiv2)
    // - Postdiv2 of 5, down to 48 MHz
    //
    // Total postdiv of 25 means that too-fast xtal will push VCO out of
    // lockable range *before* clk_sys goes out of closure (factor of 1.88)
    reset_unreset_block_wait_noinline(RESETS_RESET_PLL_SYS_BITS);
    pll_sys_hw->cs = 1u << PLL_CS_REFDIV_LSB;
    pll_sys_hw->fbdiv_int = 100;
    pll_sys_hw->prim =
            (5u << PLL_PRIM_POSTDIV1_LSB) |
            (5u << PLL_PRIM_POSTDIV2_LSB);

    // Power up VCO, wait for lock
    hw_clear_bits(&pll_sys_hw->pwr, PLL_PWR_PD_BITS | PLL_PWR_VCOPD_BITS);
    while (!(pll_sys_hw->cs & PLL_CS_LOCK_BITS));

    // Power up post-dividers, which ungates PLL final output
    hw_clear_bits(&pll_sys_hw->pwr, PLL_PWR_POSTDIVPD_BITS);

    // Glitchy switch of clk_usb, clk_sys aux to sys PLL output.
    clocks_hw->clk[clk_sys].ctrl = 0;
    clocks_hw->clk[clk_usb].ctrl =
            CLOCKS_CLK_USB_CTRL_ENABLE_BITS |
            (CLOCKS_CLK_USB_CTRL_AUXSRC_VALUE_CLKSRC_PLL_SYS
                    << CLOCKS_CLK_USB_CTRL_AUXSRC_LSB);

    // Glitchless switch of clk_sys to aux source (sys PLL)
    hw_set_bits(&clocks_hw->clk[clk_sys].ctrl, CLOCKS_CLK_SYS_CTRL_SRC_BITS);
    while (!(clocks_hw->clk[clk_sys].selected & 0x2u));
}

void __noinline __attribute__((noreturn)) async_task_worker_thunk();

static __noinline __attribute__((noreturn)) void _usb_boot(uint32_t _usb_activity_gpio_pin_mask,
                                                                  uint32_t disable_interface_mask) {
    reset_block_noinline(RESETS_RESET_USBCTRL_BITS);
    _usb_clock_setup();
    unreset_block_wait_noinline(RESETS_RESET_USBCTRL_BITS);

    // Ensure timer and watchdog are running at approximately correct speed
    // (can't switch clk_ref to xosc at this time, as we might lose ability to resus)
    watchdog_hw->tick = 12u << WATCHDOG_TICK_CYCLES_LSB;
    hw_set_bits(&watchdog_hw->tick, WATCHDOG_TICK_ENABLE_BITS);

    // turn off XIP cache since we want to use it as RAM in case the USER wants to use it for a RAM only binary
    // TODO: This is why it's so much slower than ROM
    // hw_clear_bits(&xip_ctrl_hw->ctrl, XIP_CTRL_EN_BITS);
    // Don't clear out RAM - leave it to binary download to clear anything it needs cleared; anything BSS will be done by crt0.S on reset anyway

    // this is where the BSS is so clear it
    memset0(usb_dpram, USB_DPRAM_SIZE);

    // now we can finally initialize these
#ifdef USE_BOOTROM_GPIO
    usb_activity_gpio_pin_mask = _usb_activity_gpio_pin_mask;
#endif

    usb_boot_device_init(disable_interface_mask);

    // worker to run tasks on this thread (never returns); Note: USB code is IRQ driven
    // this thunk switches stack into USB DPRAM then calls async_task_worker
    async_task_worker_thunk();
}

int __crash__main() {
    // note this never returns (and is marked as such)
    _usb_boot(0, 0);
}
