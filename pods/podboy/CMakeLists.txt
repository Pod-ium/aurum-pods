# PodBoy - Game Boy (DMG) emulator on the PicoPods libraries.
# Needs an RP2350 (Pico 2 / Pico 2 W): the 2x-scaled video buffer alone is
# 276 KB, more than an RP2040 has.
if(NOT PD_HOST AND PICO_PLATFORM MATCHES "rp2040")
    message(STATUS "PodBoy skipped: it needs an RP2350 board (pico2, pico2_w)")
    return()
endif()
picopods_add_pod(podboy
    SOURCES
        src/main.cpp
        src/gb/gb.cpp
        src/gb/cpu.cpp
        src/gb/ppu.cpp
        src/gb/apu.cpp
    LIBS pd_flash_cache
    TITLE "PodBoy"
    VERSION "1.2")
target_include_directories(podboy PRIVATE src)
target_compile_options(podboy PRIVATE -O2)
if(NOT PD_HOST)
    # Hot emulator functions go to RAM: the XIP cache is left to the ROM data.
    target_compile_definitions(podboy PRIVATE PODBOY_DEVICE=1)
    target_link_libraries(podboy PRIVATE hardware_vreg)
    # A scrolling game changes every line, so each shown frame is the whole
    # 320x288 x 18-bit image (276 KB). At the terminal's default 25 MHz that is
    # 88 ms (~11 fps); at 50 MHz (clk_peri 100 MHz / 2) 44 ms, so every third
    # emulated frame reaches the screen (~20 fps). Forum reports put this panel
    # stable up to 62.5-75 MHz. Set it back to 25000000 if the image glitches.
    target_compile_definitions(podboy PRIVATE PD_PICOCALC_LCD_HZ=50000000)
endif()
# Older mainboards route the left audio channel to GP26:
#   target_compile_definitions(podboy PRIVATE PD_PICOCALC_AUDIO_L=26)
