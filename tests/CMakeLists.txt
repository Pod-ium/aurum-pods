# Host tests of the emulators (cmake -DPD_TERMINAL=host): scripted sessions
# that drive each pod through its screens. Run with ctest.
set(_tmp ${CMAKE_CURRENT_BINARY_DIR}/tmp)
file(MAKE_DIRECTORY ${_tmp})

# PodBoy: starts, shows the ROM browser (no ROMs here), opens a folder.
file(MAKE_DIRECTORY ${_tmp}/podboy_sd/roms/sub)
add_test(NAME podboy_script COMMAND podboy WORKING_DIRECTORY ${_tmp})
set_tests_properties(podboy_script PROPERTIES TIMEOUT 60 ENVIRONMENT
    "PD_SDCARD=${_tmp}/podboy_sd;PD_FRAMES=${_tmp}/podboy_frames;PD_KEYS=down,enter,bksp,shot")
