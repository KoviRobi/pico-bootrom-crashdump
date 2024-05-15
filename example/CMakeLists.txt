cmake_minimum_required(VERSION 3.12)
#set(CMAKE_C_STANDARD 11)

# Pull in PICO SDK (must be before project)
include(../../pico-sdk/pico_sdk_init.cmake)

project(pico_crash_core_example C CXX ASM)

pico_sdk_init()

add_executable(hello_usb
        hello_usb.c
        )

add_library(core_crash OBJECT IMPORTED)

set_property(TARGET core_crash PROPERTY IMPORTED_OBJECTS
        "${CMAKE_CURRENT_SOURCE_DIR}/../build/bootrom.o"
        )

if (NOT TARGET tinyusb_device)
  message(ERROR "not building hello_usb because TinyUSB submodule is not initialized in the SDK")
endif()

# pull in common dependencies
target_link_libraries(hello_usb
        pico_stdlib
        core_crash
        )

# enable usb output, disable uart output
pico_enable_stdio_usb(hello_usb 1)
pico_enable_stdio_uart(hello_usb 0)

# create map/bin/hex/uf2 file etc.
pico_add_extra_outputs(hello_usb)
