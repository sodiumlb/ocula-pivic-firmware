# TinyUSB detatched from Pico SDK so we can use latest

file(GLOB_RECURSE TINY_USB_SRC_FILES ${CMAKE_CURRENT_LIST_DIR}/tinyusb/src/*.c)

add_library(tinyusb_firmware STATIC EXCLUDE_FROM_ALL)
target_include_directories(tinyusb_firmware PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/tinyusb/src
    firmware
)
target_link_libraries(tinyusb_firmware PUBLIC
    pico_stdlib
)
target_sources(tinyusb_firmware PRIVATE
    ${TINY_USB_SRC_FILES}
)


# The Oric OCULA

add_executable(ocula)
pico_add_extra_outputs(ocula)
pico_set_program_name(ocula "Oric OCULA")
pico_set_binary_type(ocula copy_to_ram)
pico_set_linker_script(ocula ${CMAKE_CURRENT_LIST_DIR}/memmap_xram_rp2350.ld)

target_include_directories(ocula PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/tinyusb/src
    firmware
)

target_sources(ocula PRIVATE
    firmware/main.c
    firmware/str.c
    firmware/modes/mode1.c
    firmware/modes/mode2.c
    firmware/modes/mode3.c
    firmware/modes/mode4.c
    firmware/modes/mode4.S
    firmware/mon/hlp.c
    firmware/mon/mon.c
    firmware/mon/ram.c
    firmware/mon/set.c
    firmware/mon/vip.c
    firmware/oric/rst.c
    firmware/oric/ula.c
    firmware/sys/cfg.c
    firmware/sys/com.c
    firmware/sys/cpu.c
    firmware/sys/dvi.c
    firmware/sys/lfs.c
    firmware/sys/mem.c
    firmware/sys/sys.c
#    firmware/sys/vga.c
    firmware/term/color.c
    firmware/term/font.c
    firmware/term/term.c
    firmware/usb/cdc.c
    firmware/usb/descriptors.c
    firmware/usb/serno.c
    littlefs/lfs.c
    littlefs/lfs_util.c
)

target_link_libraries(ocula PRIVATE
    pico_stdlib
    pico_multicore
    pico_unique_id
    pico_scanvideo_dpi
    pico_rand
    hardware_interp
    hardware_pio
    hardware_dma
    hardware_pwm
    hardware_clocks
    hardware_flash
    tinyusb_firmware
)

pico_generate_pio_header(ocula
    ${CMAKE_CURRENT_LIST_DIR}/firmware/oric/ula.pio
)

target_compile_definitions(ocula PRIVATE
    PICO_SCANVIDEO_PLANE_COUNT=3
    PICO_SCANVIDEO_MAX_SCANLINE_BUFFER_WORDS=323
    PICO_SCANVIDEO_SCANLINE_BUFFER_COUNT=10
    PICO_SCANVIDEO_COLOR_PIN_BASE=6
    PICO_SCANVIDEO_SYNC_PIN_BASE=26
    PICO_RP2040_USB_DEVICE_ENUMERATION_FIX=1
    PICO_RP2040_USB_DEVICE_UFRAME_FIX=1
    RP6502_NAME="Oric OCULA"
    OCULA=1
)

# The VIC-20 PIVIC

add_executable(pivic)
pico_add_extra_outputs(pivic)
pico_set_program_name(pivic "VIC-20 PIVIC")
pico_set_binary_type(pivic copy_to_ram)
pico_set_linker_script(pivic ${CMAKE_CURRENT_LIST_DIR}/memmap_xram_rp2350.ld)

target_include_directories(pivic PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/tinyusb/src
    firmware
)

target_sources(pivic PRIVATE
    firmware/main.c
    firmware/str.c
    firmware/modes/mode1.c
    firmware/modes/mode2.c
    firmware/modes/mode3.c
    firmware/modes/mode4.c
    firmware/modes/mode4.S
    firmware/mon/hlp.c
    firmware/mon/mon.c
    firmware/mon/ram.c
    firmware/mon/set.c
    firmware/mon/vip.c
    firmware/sys/cfg.c
    firmware/sys/com.c
    firmware/sys/cpu.c
    firmware/sys/dvi.c
    firmware/sys/lfs.c
    firmware/sys/mem.c
    firmware/sys/sys.c
#    firmware/sys/vga.c
    firmware/term/color.c
    firmware/term/font.c
    firmware/term/term.c
    firmware/usb/cdc.c
    firmware/usb/descriptors.c
    firmware/usb/serno.c
    littlefs/lfs.c
    littlefs/lfs_util.c
    #Testing PIVIC parts
    firmware/vic/cvbs.c
)

target_link_libraries(pivic PRIVATE
    pico_stdlib
    pico_multicore
    pico_unique_id
    pico_scanvideo_dpi
    pico_rand
    hardware_interp
    hardware_pio
    hardware_dma
    hardware_pwm
    hardware_clocks
    hardware_flash
    tinyusb_firmware
)

pico_generate_pio_header(pivic
    ${CMAKE_CURRENT_LIST_DIR}/firmware/vic/cvbs.pio
)

target_compile_definitions(pivic PRIVATE
    PICO_SCANVIDEO_PLANE_COUNT=3
    PICO_SCANVIDEO_MAX_SCANLINE_BUFFER_WORDS=323
    PICO_SCANVIDEO_SCANLINE_BUFFER_COUNT=10
    PICO_SCANVIDEO_COLOR_PIN_BASE=6
    PICO_SCANVIDEO_SYNC_PIN_BASE=26
    PICO_RP2040_USB_DEVICE_ENUMERATION_FIX=1
    PICO_RP2040_USB_DEVICE_UFRAME_FIX=1
    RP6502_NAME="VIC-20 PIVIC"
    PIVIC=1
)


# Project defines available to both Pi Picos.
# Please change name for hardware forks.
# For release, set version string and set code page to 0.

set_property(TARGET ocula pivic
    APPEND PROPERTY COMPILE_DEFINITIONS
    RP6502_VERSION=""
    RP6502_CODE_PAGE=850
    RP6502_KEYBOARD=EN_US
    RP6502_MIN_PHI2=500
    RP6502_MAX_PHI2=8000
)


# If no version set, touch the file that provides a dev timestamp for the version.

get_target_property(OCULA-PIVIC_COMPILE_DEFINITIONS ocula COMPILE_DEFINITIONS)
if(OCULA-PIVIC_COMPILE_DEFINITIONS MATCHES "RP6502_VERSION=\"\"")
    add_custom_command(TARGET ocula pivic
        COMMAND ${CMAKE_COMMAND} -E touch ${CMAKE_CURRENT_LIST_DIR}/firmware/main.c
    )
endif()
