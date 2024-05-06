/**
 * Copyright (c) 2020 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */


// ----------------------------------------------------------------------------
// Bootrom Runtime 0
// ----------------------------------------------------------------------------
// This is not a full crt0 -- in particular, no .bss or .data initialisation
// (use of .data/.bss is disallowed via linker script assertions).
// The bootrom is not permitted to use statically-allocated memory, as parts of
// it are called into by user code.
// The purpose of this file is:
// - Provide initial entry point for both cores
// - Provide holding pen and launch code for core 1
// - Provide direct-boot entry for core 0, mainly intended for running
//   ARM code during ATE
// - Pass core 0 control over to the main flash boot sequence

#include "hardware/regs/addressmap.h"
#include "hardware/regs/pads_bank0.h"
#include "hardware/regs/resets.h"
#include "hardware/regs/sio.h"
#include "hardware/regs/watchdog.h"
#include "hardware/regs/syscfg.h"
#include "hardware/regs/clocks.h"
#include "hardware/regs/vreg_and_chip_reset.h"
#include "hardware/regs/m0plus.h"
#include "git_info.h"

.cpu cortex-m0

.section .reset, "ax"

// This is the beginning of the image, which is entered from stage2 or bootrom USB MSD watchdog reboot

// note if we are NO_FLASH then start: below is currently identical anyway, so save 4 bytes
#if !PICO_NO_FLASH
    // We simply install our own vector table and redirect through it
    ldr r0, =__vectors
    b __vector_entry
#endif

// ELF entry point generally called when we load an ELF via debugger
.type _entry_point,%function
.thumb_func
.global _entry_point
_entry_point:

#if PICO_NO_FLASH
    // non flash
    ldr r0, =__vectors
#else
    // todo clear watchdog?
    // When using flash, we install and use the ROM vector table to go thru regular bootrom/stage2 flash sequence
    movs r0, #0
#endif

__vector_entry:
    ldr r1, =(PPB_BASE + M0PLUS_CPUID_OFFSET)
    str r0, [r1, #8] // Set VTOR
    ldmia r0!, {r1, r2}
    msr msp, r1
    bx r2

.thumb
.section .vectors
.balign 2

.global __vectors
__vectors:
.word _stacktop // MSP
.word _start    // Reset
.word _nmi      // NMI
.word _dead     // HardFault

.global _dead
.type _dead,%function
.thumb_func
_dead: // in place of irq4 vector
    wfi
    b _dead

// this is all a bit cheeky, but the existing code assumed that the table above could go there because nothing ever called
// some code in irq0 -4 slots

// If you reach this, something has gone wrong. Most likely, the debugger
// has done a core-only reset while the NMI mask was set, and a relevant
// IRQ was asserted. This is probably not intentional, and is confusing
// to debug, so we should just clear the NMI masks.
.align 2
.global _nmi
.type _nmi,%function
.thumb_func
_nmi:
    // we do not want to use any stack as we're called from the core 0 boot path.
    // we use r12 as this is saved/restored by the processor in an actual NMI
    mov r12, lr
    // We can take an NMI straight out of reset, so we should first ensure
    // that SYSCFG is being clocked, else we end up straight back in NMI
    bl enable_clocks
    // Then just clear the NMI mask (for both cores)
    ldr r0, =(SYSCFG_BASE + SYSCFG_PROC0_NMI_MASK_OFFSET)
    mov r1, #0
    str r1, [r0]
    str r1, [r0, #4]
    mov pc, r12

// On a cold boot, the clocks will already be enabled, because the power-on state
// machine will have reset the clock controls. However we can have trouble on a warm
// boot, that is to say:
// - The debugger has just reset the processors and started them running
// - The watchdog has fired, with WDSEL selecting a restart point after
//   clocks_bank_default.
// Assume that enough clocks are already enabled to run this code!
// Note it is NOT recommended to disable things like ROM clock if WDSEL is
// later than CLOCKS_BANK_DEFAULT.
.global enable_clocks
.type enable_clocks,%function
.thumb_func
enable_clocks:
    ldr r0, =(CLOCKS_BASE + CLOCKS_WAKE_EN0_OFFSET)
    // Set entire clock enable mask. Relying on HCGs to avoid huge current transient
    mov r1, #0
    mvn r1, r1
    str r1, [r0]
    str r1, [r0, #4]
    // we steal the return for its own function
.global _noop
.type _noop,%function
.thumb_func
_noop:
    bx lr

.align 2
.global software_git_revision
software_git_revision:
.word GIT_REV

.org 0x54
.global __irq5_vector
__irq5_vector:
.word isr_irq5

copyright:
.string "(C) 2020 Raspberry Pi Trading Ltd"

// ----------------------------------------------------------------------------
// Entry point for both cores
// ----------------------------------------------------------------------------

.global _start
.type _start,%function
.thumb_func
_start:

    // Set memory to a known value -- at this point we aren't using any stack
    ldr r0, =0x0000CDAB // abcd0000 as LE bytes
    ldr r1, =SRAM_BASE
    ldr r2, =SRAM_END
    ldr r3, =0xFFFF
1:  and r0, r3
    lsl r4, r1, #16
    bic r4, r3
    orr r0, r4
    stm r1!, {r0}
    cmp r1, r2
    bne 1b

// Make sure all the control registers we are about to access are being clocked.
// On a cold boot everything will be set up by the power-on state machine,
// but the clock setup may be dirty on a warm boot.

// note that the NMI handler does exactly what we want (enable_clocks) and also disables NMI
    bl _nmi

disable_adc_ie:
#if RESETS_RESET_PADS_BANK0_BITS != (0x80 << 1)
#error
#endif
    mov r0, #0x80
    lsl r0, #1
    bl unreset_block_wait_noinline

    ldr r1, =PADS_BANK0_BASE + REG_ALIAS_CLR_BITS
    mov r2, #PADS_BANK0_GPIO0_IE_BITS
    str r2, [r1, #PADS_BANK0_GPIO26_OFFSET]
    str r2, [r1, #PADS_BANK0_GPIO27_OFFSET]
    str r2, [r1, #PADS_BANK0_GPIO28_OFFSET]
    str r2, [r1, #PADS_BANK0_GPIO29_OFFSET]

    // main does not return
    bl main
    // b _dead

.global reset_block_noinline
.type reset_block_noinline,%function
.thumb_func
reset_block_noinline:
    ldr r1, =RESETS_BASE + REG_ALIAS_SET_BITS
    str r0, [r1]
    bx lr

.global reset_unreset_block_wait_noinline
.type reset_unreset_block_wait_noinline,%function
.thumb_func
reset_unreset_block_wait_noinline:
    ldr r1, =RESETS_BASE + REG_ALIAS_SET_BITS
    str r0, [r1]
    // fall thru

.global unreset_block_wait_noinline
.type unreset_block_wait_noinline,%function
.thumb_func
unreset_block_wait_noinline:
    ldr r1, =RESETS_BASE
    ldr r2, =RESETS_BASE + REG_ALIAS_CLR_BITS
    str r0, [r2]
1:
    ldr r2, [r1, #RESETS_RESET_DONE_OFFSET]
    bic r0, r2
    bne 1b
    bx lr

#define USB_BOOT_STACK_SIZE 300

// we clear USB SRAM (aka .bss and stack), and switch stack
.global async_task_worker_thunk
.thumb_func
async_task_worker_thunk:
    // set stack
    ldr r0, =usb_boot_stack_end
    msr MSP, r0
    bl async_task_worker
    // async_task_worker does not return

_:

.section .bss
.align 2
usb_boot_stack:
.space USB_BOOT_STACK_SIZE * 4
usb_boot_stack_end:
