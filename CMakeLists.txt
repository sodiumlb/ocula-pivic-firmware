cmake_minimum_required(VERSION 3.13..3.27)

set(PICO_SDK_PATH ${CMAKE_CURRENT_LIST_DIR}/src/pico-sdk)
set(PICO_EXTRAS_PATH ${CMAKE_CURRENT_LIST_DIR}/src/pico-extras)
# set(PICO_TINYUSB_PATH ${CMAKE_CURRENT_LIST_DIR}/src/tinyusb)
set(PICO_BOARD_HEADER_DIRS ${CMAKE_CURRENT_LIST_DIR}/src/boards)

set(PICO_BOARD ocula-pivic)

include(${PICO_SDK_PATH}/pico_sdk_init.cmake)
add_subdirectory(${PICO_EXTRAS_PATH})

project(OCULA-PIVIC C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

add_subdirectory(src)
