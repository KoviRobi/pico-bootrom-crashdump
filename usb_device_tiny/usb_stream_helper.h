/**
 * Copyright (c) 2020 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _USB_STREAM_HELPER_H
#define _USB_STREAM_HELPER_H

#include "usb_device.h"

struct usb_transfer_funcs;

struct usb_stream_transfer {
    struct usb_transfer core;
    uint32_t offset; // offset within the stream
    uint32_t transfer_length;
    uint32_t chunk_size;
    uint8_t *chunk_buffer;
    struct usb_endpoint *ep;
    const struct usb_stream_transfer_funcs *funcs;
#ifndef NDEBUG
    bool packet_handler_complete_expected;
#endif
};

typedef void (*stream_on_packet_complete_function)(__removed_for_space(struct usb_stream_transfer *transfer));
typedef bool (*stream_on_chunk_function)(uint32_t chunk_len
                                         __comma_removed_for_space(struct usb_stream_transfer *transfer));

struct usb_stream_transfer_funcs {
    stream_on_packet_complete_function on_packet_complete;
    // returns whether processing async
    __rom_function_type(stream_on_chunk_function) on_chunk;
};

void usb_stream_setup_transfer(struct usb_stream_transfer *transfer, const struct usb_stream_transfer_funcs *funcs,
                               uint8_t *chunk_buffer, uint32_t chunk_size, uint32_t transfer_length,
                               usb_transfer_completed_func on_complete);

void usb_stream_chunk_done(struct usb_stream_transfer *transfer);

#ifndef USB_BOOT_EXPANDED_RUNTIME
extern void __crash___noop();
#define usb_stream_noop_on_packet_complete ((stream_on_packet_complete_function)__crash___noop)
#define usb_stream_noop_on_chunk needs_work
#else
void usb_stream_noop_on_packet_complete(__removed_for_space(struct usb_stream_transfer *transfer));
bool usb_stream_noop_on_chunk(uint32_t chunk_len __comma_removed_for_space( struct usb_stream_transfer *transfer));
#endif
#endif //SOFTWARE_USB_STREAM_HELPER_H
