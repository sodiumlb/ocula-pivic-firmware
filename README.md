# Oric OCULA and VIC-20 PIVIC Firmware

This is the firmware source for two related video replacement devices. It is using the Rumbledethumps' Picocomputer 6502 (RP6502) https://picocomputer.github.io/ as a starting point, framework, and some modules. 

## Oric OCULA
A modern implementation of the HCS10017 ULA, used in Oric 8-bit computers like the Oric-1, Atmos and clones.

## VIC-20 PIVIC
A modern implementation of the MOS VIC 6560 and 6561, used in Commodore VIC-20 8-bit computers.

## Dev Setup

This is only for building the LOCI firmware.

Install the C/C++ toolchain for the Raspberry Pi Pico. For more information, read [Getting started with the Raspberry Pi Pico](https://rptl.io/pico-get-started).
```
sudo apt install cmake gcc-arm-none-eabi libnewlib-arm-none-eabi build-essential gdb-multiarch
```

All dependencies are submodules. The following will download the correct version of all SDKs. It will take an extremely long time to recurse the project, so do this instead:
```
git submodule update --init
cd src/pico-sdk
git submodule update --init
cd ../..
```

To build from the command line in a separate build directory:
```
mkdir build
cd build
cmake ../
make
```