# RP2040 coredump on crash

This 🚧 WORK IN PROGRESS 🚧 project is an add-on you can add to your normal
flash image, and it provides an alternative entry-point which presents itself
as an USB mass storage device with a core-dump file on it.

The expected use-case is that you use the watchdog alternate entry function of
the RP2040 (see RP2040 datasheet §2.8.1.1 Watchdog Boot) to enter this mode
(save registers in TODO first).

I do far too much RP2040 development on the train with my old laptop which only
has one working USB port, so I cannot plug in a debugger (which would be more
faff anyway to carry around). As they say, necessity is the mother of invention,
and I can't think of a better platform for this than the RP2040.

## Progress

Currently it is only a port of the RP2040 bootrom that can be written into the
flash and boot to presenting itself as a mass-storage device.

## TODO:
- [ ] Generate actual coredump file:
  - [ ] Dump memory;
  - [ ] Space for registers;
  - [ ] Check RAM usage so it can be reserved -- maybe possible to keep this
    under 256 bytes to only use the RAM which the bootrom thrashes by writing
    the boot2 loader to it.
- [ ] Link to the end of the flash, link as an ELF library and provide an
  example that uses it as a library, rather than just linking an ELF executable.
- [ ] Optimize space (can probably reuse some functions from the bootrom).
  - Probably worth providing two variants, a portable one and a B2 optimized
    one.
- [ ] Do hand-over for flashing (writing firmware to the mass storage device).
  - Currently it just resets to the actual bootrom via the `reset_usb_boot`
    function. On my desktop (Linux 6.6.23), the USB stack is forgiving enough
    that it is fine with this, but this is a hack.

## Compiling

Since this isn't burned in to the ROM, there is no need for it to be compiled
with the exact compiler that I used, nevertheless you can use the Nix flake to
do so.

You can use the usual compile steps:
```
cmake -Bbuild -GNinja -DCMAKE_EXPORT_COMPILE_COMMANDS=On
cmake --build build
```

And you can run it using elf2uf2/elf2uf2-rs, e.g.
```
elf2uf2-rs -d build/bootrom.elf
```
