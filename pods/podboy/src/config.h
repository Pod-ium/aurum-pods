// PodBoy - app configuration. Hardware (pins, SPI/I2C speeds, audio pins)
// now lives in the PicoPods PicoCalc terminal: override it with PD_PICOCALC_*
// compile definitions in CMakeLists.txt (see docs/PORTING.md).
#pragma once
#include "pd/keys.h"

// ---------------------------------------------------------------- clocks
#define SYS_CLOCK_KHZ        200000   // RP2350 overclock; 150000 is stock. Drop if unstable.

// ---------------------------------------------------------------- emulator behaviour
#define ROM_DIR              "/roms"      // browser starts here if it exists, else "/"
#define ROMCACHE_MAX_BYTES   (2u * 1024 * 1024 + 4096)   // flash reserved for the running ROM
#define CART_RAM_MAX         (32 * 1024)     // largest DMG save RAM; 128 KB carts are GBC titles
#define AUTOSAVE_FRAMES      180          // save .sav ~3 s after the game last wrote to cart RAM
#define DEFAULT_VOLUME       60           // percent

// Key bindings (PicoPods / PicoCalc key codes; letters are matched case-insensitively)
#define BIND_A1              'x'
#define BIND_A2              'k'
#define BIND_B1              'z'
#define BIND_B2              'j'
#define BIND_START           pd::key::Enter
#define BIND_SELECT1         pd::key::Space
#define BIND_SELECT2         pd::key::Backspace
#define BIND_PAUSE1          pd::key::Esc
#define BIND_PAUSE2          pd::key::F1
