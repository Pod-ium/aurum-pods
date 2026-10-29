# aurum-pods - PicoPods emulators for the PicoCalc, and PicoPods Web, built on the PicoPods
# libraries (hydrogenium-pods).
#
# Device build (default: PicoCalc + Pico 2 W):
#   cmake -B build -DPICO_SDK_PATH=/path/to/pico-sdk
#   cmake --build build -j
# Other board:    -DPD_BOARD=pico2   (pico, pico_w, pico2, pico2_w...)
# PC build (simulator + tests):
#   cmake -B build-host -DPD_TERMINAL=host && cmake --build build-host -j
#   ctest --test-dir build-host
# The libraries are fetched from git unless -DPICOPODS_PATH=/path/to/hydrogenium-pods
# (or the environment variable) points at a checkout: see picopods_import.cmake.
cmake_minimum_required(VERSION 3.13)
include(picopods_import.cmake)
project(aurum_pods C CXX ASM)
include(${PICOPODS_PATH}/cmake/picopods.cmake)

# Pods
add_subdirectory(pods/podboy)

if(PD_HOST AND NOT EMSCRIPTEN)   # the browser build has pods only
    enable_testing()
    add_subdirectory(tests)
endif()
