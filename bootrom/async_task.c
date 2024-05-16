/**
 * Copyright (c) 2020 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <sys/param.h>
#include "runtime.h"
#include "async_task.h"
#include "usb_boot_device.h"
#include "usb_msc.h"
#include "boot/picoboot.h"
#include "hardware/sync.h"

//#define NO_ASYNC
//#define NO_ROM_READ

CU_REGISTER_DEBUG_PINS(flash)

static uint8_t _last_mutation_source;

bool watchdog_rebooting = false;

// NOTE for simplicity this returns error codes from PICOBOOT
static uint32_t _execute_task(struct async_task *task) {
    if (watchdog_rebooting) {
        return PICOBOOT_REBOOTING;
    }
    uint type = task->type;
    if (type & AT_EXCLUSIVE) {
        // we do this in executex task, so we know we aren't executing and virtual_disk_queue tasks at this moment
        usb_warn("SET EXCLUSIVE ACCESS %d\n", task->exclusive_param);
        async_disable_queue(&virtual_disk_queue, task->exclusive_param);
        if (task->exclusive_param == EXCLUSIVE_AND_EJECT) {
            msc_eject();
        }
    }
    if (type & AT_EXEC) {
        usb_warn("exec %08x\n", (uint) task->transfer_addr);
        // scary but true; note callee must not overflow our stack (note also we reuse existing field task->transfer_addr to save code/data space)
        (((void (*)()) (task->transfer_addr | 1u)))();
    }
    if (type & (AT_WRITE | AT_FLASH_ERASE)) {
        if (task->check_last_mutation_source && _last_mutation_source != task->source) {
            return PICOBOOT_INTERLEAVED_WRITE;
        }
        _last_mutation_source = task->source;
    }
    bool direct_access = false;
    if (type & (AT_WRITE | AT_READ)) {
        if ((is_address_ram(task->transfer_addr) && is_address_ram(task->transfer_addr + task->data_length))
            #ifndef NO_ROM_READ
            || (!(type & AT_WRITE) && is_address_rom(task->transfer_addr) &&
                is_address_rom(task->transfer_addr + task->data_length))
#endif
                ) {
            direct_access = true;
        } else {
            // bad address
            return PICOBOOT_INVALID_ADDRESS;
        }
        if (type & AT_WRITE) {
            if (direct_access) {
                usb_warn("writing %08x +%04x\n", (uint) task->transfer_addr, (uint) task->data_length);
                memcpy((void *) task->transfer_addr, task->data, task->data_length);
            }
        }
        if (type & AT_READ) {
            if (direct_access) {
                usb_warn("reading %08x +%04x\n", (uint) task->transfer_addr, (uint) task->data_length);
                memcpy(task->data, (void *) task->transfer_addr, task->data_length);
            }
        }
    }
    return PICOBOOT_OK;
}

// just put this here in case it is worth noinlining - not atm
static void _task_copy(struct async_task *to, struct async_task *from) {
    //*to = *from;
    memcpy(to, from, sizeof(struct async_task));
}

void reset_task(struct async_task *task) {
    memset0(task, sizeof(struct async_task));
}

void queue_task(struct async_task_queue *queue, struct async_task *task, async_task_callback callback) {
    task->callback = callback;
#ifdef ASYNC_TASK_REQUIRE_TASK_CALLBACK
    assert(callback);
#endif
    assert(!task->result); // convention is that it is zero, so try to catch missing rest
#ifdef NO_ASYNC
    task->result = _execute_task(task);
    _call_task_complete(task);
#else
    if (queue->full) {
        usb_warn("overwriting already queued task for queue %p\n", queue);
    }
    _task_copy(&queue->task, task);
    queue->full = true;
    __sev();
#endif
}

static inline void _call_task_complete(struct async_task *task) {
#ifdef ASYNC_TASK_REQUIRE_TASK_CALLBACK
    task->callback(task);
#else
    if (task->callback) task->callback(task);
#endif
}

bool dequeue_task(struct async_task_queue *queue, struct async_task *task_out) {
#ifdef NO_ASYNC
    return false;
#else
    bool have_task = false;
    uint32_t save = save_and_disable_interrupts();
    __mem_fence_acquire();
    if (queue->full) {
        _task_copy(task_out, &queue->task);
        queue->full = false;
        have_task = true;
    }
    restore_interrupts(save);
    return have_task;
#endif
}

void execute_task(struct async_task_queue *queue, struct async_task *task) {
    if (queue->disable)
        task->result = 1; // todo better code (this is fine for now since we only ever disable virtual_disk queue which only cares where or not result is 0
    else
        task->result = _execute_task(task);
    uint32_t save = save_and_disable_interrupts();
    _call_task_complete(task);
    restore_interrupts(save);
}

struct async_task_queue virtual_disk_queue;

#ifndef NDEBUG
static bool _worker_started;
#endif

static struct async_task _worker_task;

void __attribute__((noreturn)) async_task_worker() {
#ifndef NDEBUG
    _worker_started = true;
#endif
    do {
        if (dequeue_task(&virtual_disk_queue, &_worker_task)) {
            execute_task(&virtual_disk_queue, &_worker_task);
        }
#ifdef USE_PICOBOOT
        else if (dequeue_task(&picoboot_queue, &_worker_task)) {
            execute_task(&picoboot_queue, &_worker_task);
        }
#endif
        else {
            __wfe();
        }
    } while (true);
}
