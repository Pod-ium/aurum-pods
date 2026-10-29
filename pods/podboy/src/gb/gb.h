// PodBoy - Game Boy (DMG) emulator core
// Portable C++: no Pico SDK dependencies, so it also builds on a PC for testing.
#pragma once
#include <stdint.h>
#include <stddef.h>

// On the Pico, hot functions are placed in RAM so the XIP flash cache is left
// to the ROM data (the running game is stored in flash).
#if defined(PODBOY_DEVICE)
#define GB_HOT __attribute__((section(".time_critical.gb")))
#else
#define GB_HOT
#endif

namespace gb {

constexpr int SCREEN_W = 160;
constexpr int SCREEN_H = 144;
constexpr uint32_t CYCLES_PER_FRAME = 70224;   // T-cycles (4.194304 MHz)
constexpr uint32_t CPU_HZ = 4194304;
constexpr uint32_t AUDIO_RATE = 32768;         // CPU_HZ / 128, exact
constexpr int AUDIO_MAX_SAMPLES = 1024;        // per frame, stereo pairs

// Joypad bits for set_buttons(): 1 = pressed
enum Button : uint8_t {
    BTN_RIGHT = 0x01, BTN_LEFT = 0x02, BTN_UP = 0x04, BTN_DOWN = 0x08,
    BTN_A = 0x10, BTN_B = 0x20, BTN_SELECT = 0x40, BTN_START = 0x80,
};

enum class LoadResult { OK, TOO_SMALL, CGB_ONLY, UNSUPPORTED_MBC };

struct CartInfo {
    char title[17];
    uint8_t type;        // header byte 0x147
    uint8_t mbc;         // 0 none, 1 MBC1, 2 MBC2, 3 MBC3, 5 MBC5
    bool battery;
    bool rtc;
    uint32_t rom_size;
    uint32_t ram_size;   // bytes of cartridge RAM (512 for MBC2)
};

// Reads the header from the first 0x150 bytes of a ROM.
LoadResult parse_header(const uint8_t* hdr, uint32_t file_size, CartInfo* out);

class APU {
public:
    void reset();
    void power_on_defaults();
    uint8_t read(uint16_t addr) const;
    void write(uint16_t addr, uint8_t v);
    void step(uint32_t cycles);
    // Stereo interleaved samples produced since last clear
    const int16_t* samples() const { return buf_; }
    int sample_count() const { return nsamples_; }
    void clear_samples() { nsamples_ = 0; }

private:
    struct Square {
        bool enabled, dac, len_en;
        uint8_t duty, pos, vol, env_init, env_period, env_timer; bool env_up;
        uint16_t freq; int32_t timer; uint16_t len;
        // sweep (channel 1 only)
        uint8_t sw_period, sw_shift, sw_timer; bool sw_neg, sw_en; uint16_t shadow;
    } sq_[2];
    struct Wave {
        bool enabled, dac, len_en; uint16_t freq; int32_t timer; uint16_t len;
        uint8_t pos, shift;
    } wave_;
    struct Noise {
        bool enabled, dac, len_en; uint8_t vol, env_init, env_period, env_timer; bool env_up;
        uint16_t lfsr; bool width7; int32_t timer; uint32_t period; uint16_t len;
    } noise_;

    uint8_t regs_[0x30];     // FF10..FF3F
    bool power_;
    uint32_t fs_counter_, sample_counter_;
    uint8_t fs_step_;
    float hp_l_, hp_r_;
    int16_t buf_[AUDIO_MAX_SAMPLES * 2];
    int nsamples_;

    void trigger_square(int i);
    void trigger_wave();
    void trigger_noise();
    uint16_t sweep_calc();
    void clock_length();
    void clock_envelope();
    void clock_sweep();
    void emit_sample();
};

class GameBoy {
public:
    // rom must stay valid while running (on the Pico it lives in flash).
    // cart_ram is caller-owned storage of at least info.ram_size bytes.
    LoadResult load(const uint8_t* rom, uint32_t rom_size, uint8_t* cart_ram, uint32_t cart_ram_cap);
    void reset();
    void run_frame();                           // one video frame (~16.74 ms of GB time)
    void set_buttons(uint8_t pressed_mask);
    void set_framebuffer(uint8_t* fb) { fb_ = fb; }   // 160*144 bytes, shades 0..3 (0 = lightest)
    const CartInfo& info() const { return cart_; }
    APU& apu() { return apu_; }

    // Save RAM tracking
    bool ram_dirty() const { return ram_dirty_; }
    void clear_ram_dirty() { ram_dirty_ = false; }

    // Serial port hook (used by the PC test harness to read test ROM output)
    void (*serial_cb)(uint8_t byte, void* user) = nullptr;
    void* serial_user = nullptr;

private:
    // ---- CPU ----
    uint8_t a_, f_, b_, c_, d_, e_, h_, l_;
    uint16_t sp_, pc_;
    bool ime_, halted_, halt_bug_;
    uint8_t ei_delay_;
    uint32_t cyc_;        // T-cycles consumed by the current instruction

    uint32_t cpu_step();
    void exec_cb();
    uint8_t fetch8();
    uint16_t fetch16();
    void push16(uint16_t v);
    uint16_t pop16();
    uint8_t get_r(int i);
    void set_r(int i, uint8_t v);
    void alu(int op, uint8_t v);

    // ---- memory ----
    GB_HOT uint8_t rd(uint16_t addr);
    GB_HOT void wr(uint16_t addr, uint8_t v);
    uint8_t io_read(uint16_t addr);
    void io_write(uint16_t addr, uint8_t v);
    void mbc_write(uint16_t addr, uint8_t v);
    void update_banks();

    const uint8_t* rom_ = nullptr;
    const uint8_t* rom0_ = nullptr;   // mapped at 0000-3FFF
    const uint8_t* romx_ = nullptr;   // mapped at 4000-7FFF
    uint8_t* cram_ = nullptr;          // cartridge RAM
    uint8_t* cram_bank_ = nullptr;
    uint32_t rom_banks_ = 0;
    CartInfo cart_{};
    bool ram_en_ = false, ram_dirty_ = false;
    uint16_t rom_bank_ = 1;
    uint8_t ram_bank_ = 0, mbc1_hi_ = 0, mbc1_mode_ = 0;
    // MBC3 RTC
    uint8_t rtc_[5] = {0}, rtc_latched_[5] = {0}, rtc_latch_prev_ = 0xFF;
    uint32_t rtc_cycles_ = 0;
    void rtc_tick(uint32_t cycles);

    uint8_t vram_[0x2000];
    uint8_t wram_[0x2000];
    uint8_t oam_[0xA0];
    uint8_t hram_[0x7F];
    uint8_t io_[0x80];
    uint8_t ie_ = 0;

    // ---- timer ----
    uint32_t div_counter_ = 0;
    void timer_step(uint32_t cycles);

    // ---- joypad ----
    uint8_t buttons_ = 0;

    // ---- PPU ----
    uint8_t* fb_ = nullptr;
    uint8_t mode_ = 2, ly_ = 0, win_line_ = 0;
    uint32_t dot_ = 0;
    bool stat_line_ = false;
    bool frame_done_ = false;
    void ppu_step(uint32_t cycles);
    void update_stat();
    void render_line();

    APU apu_;
};

} // namespace gb
