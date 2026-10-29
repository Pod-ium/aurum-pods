// PodBoy - memory bus, cartridge mappers, timer, joypad, frame loop
#include "gb.h"
#include <string.h>

namespace gb {

// IO register indices (offset from 0xFF00)
enum {
    R_P1 = 0x00, R_SB = 0x01, R_SC = 0x02, R_DIV = 0x04, R_TIMA = 0x05, R_TMA = 0x06, R_TAC = 0x07,
    R_IF = 0x0F, R_LCDC = 0x40, R_STAT = 0x41, R_SCY = 0x42, R_SCX = 0x43, R_LY = 0x44, R_LYC = 0x45,
    R_DMA = 0x46, R_BGP = 0x47, R_OBP0 = 0x48, R_OBP1 = 0x49, R_WY = 0x4A, R_WX = 0x4B,
};

LoadResult parse_header(const uint8_t* hdr, uint32_t size, CartInfo* ci) {
    memset(ci, 0, sizeof(*ci));
    if (size < 0x8000) return LoadResult::TOO_SMALL;
    for (int i = 0; i < 16; i++) {
        char ch = (char)hdr[0x134 + i];
        ci->title[i] = (ch >= 32 && ch < 127) ? ch : 0;
    }
    ci->title[16] = 0;
    if (hdr[0x143] == 0xC0) return LoadResult::CGB_ONLY;
    ci->type = hdr[0x147];
    ci->rom_size = size;
    switch (ci->type) {
        case 0x00: case 0x08: case 0x09: ci->mbc = 0; break;
        case 0x01: case 0x02: case 0x03: ci->mbc = 1; break;
        case 0x05: case 0x06: ci->mbc = 2; break;
        case 0x0F: case 0x10: case 0x11: case 0x12: case 0x13: ci->mbc = 3; break;
        case 0x19: case 0x1A: case 0x1B: case 0x1C: case 0x1D: case 0x1E: ci->mbc = 5; break;
        default: return LoadResult::UNSUPPORTED_MBC;
    }
    switch (ci->type) {
        case 0x03: case 0x06: case 0x09: case 0x0F: case 0x10: case 0x13: case 0x1B: case 0x1E:
            ci->battery = true; break;
        default: break;
    }
    ci->rtc = (ci->type == 0x0F || ci->type == 0x10);
    static const uint32_t ram_sizes[6] = {0, 2048, 8192, 32768, 131072, 65536};
    uint8_t rs = hdr[0x149];
    ci->ram_size = rs < 6 ? ram_sizes[rs] : 0;
    if (ci->mbc == 2) ci->ram_size = 512;
    return LoadResult::OK;
}

LoadResult GameBoy::load(const uint8_t* rom, uint32_t size, uint8_t* cram, uint32_t cap) {
    LoadResult r = parse_header(rom, size, &cart_);
    if (r != LoadResult::OK) return r;
    if (cart_.ram_size > cap) cart_.ram_size = cap;
    rom_ = rom;
    rom_banks_ = size / 0x4000;
    if (rom_banks_ < 2) rom_banks_ = 2;
    cram_ = cram;
    reset();
    return LoadResult::OK;
}

void GameBoy::reset() {
    // Register state right after the DMG boot ROM hands over
    a_ = 0x01; f_ = 0xB0; b_ = 0x00; c_ = 0x13; d_ = 0x00; e_ = 0xD8; h_ = 0x01; l_ = 0x4D;
    sp_ = 0xFFFE; pc_ = 0x0100;
    ime_ = false; halted_ = false; halt_bug_ = false; ei_delay_ = 0;

    memset(vram_, 0, sizeof vram_);
    memset(wram_, 0, sizeof wram_);
    memset(oam_, 0, sizeof oam_);
    memset(hram_, 0, sizeof hram_);
    memset(io_, 0, sizeof io_);
    ie_ = 0;
    io_[R_P1] = 0xCF; io_[R_SC] = 0x7E; io_[R_TAC] = 0xF8; io_[R_IF] = 0xE1;
    io_[R_LCDC] = 0x91; io_[R_STAT] = 0x85; io_[R_BGP] = 0xFC; io_[R_OBP0] = 0xFF; io_[R_OBP1] = 0xFF;
    div_counter_ = 0xABCC;

    ram_en_ = false; rom_bank_ = 1; ram_bank_ = 0; mbc1_hi_ = 0; mbc1_mode_ = 0;
    memset(rtc_, 0, sizeof rtc_); memset(rtc_latched_, 0, sizeof rtc_latched_);
    rtc_latch_prev_ = 0xFF; rtc_cycles_ = 0;
    update_banks();

    mode_ = 2; ly_ = 0; win_line_ = 0; dot_ = 0; stat_line_ = false; frame_done_ = false;
    buttons_ = 0;
    apu_.reset();
    apu_.power_on_defaults();
}

void GameBoy::set_buttons(uint8_t mask) {
    uint8_t newly = mask & ~buttons_;
    buttons_ = mask;
    if (newly) io_[R_IF] |= 0x10;   // joypad interrupt
}

// ---------------------------------------------------------------- banking
void GameBoy::update_banks() {
    uint32_t bank0 = 0, bankx = 1, rbank = 0;
    switch (cart_.mbc) {
        case 0: bankx = 1; break;
        case 1: {
            uint32_t lo = rom_bank_ & 0x1F; if (lo == 0) lo = 1;
            bankx = (mbc1_hi_ << 5) | lo;
            if (mbc1_mode_) { bank0 = mbc1_hi_ << 5; rbank = mbc1_hi_; }
            break;
        }
        case 2: bankx = rom_bank_ & 0x0F; if (bankx == 0) bankx = 1; break;
        case 3: bankx = rom_bank_ & 0x7F; if (bankx == 0) bankx = 1; rbank = ram_bank_ & 0x03; break;
        case 5: bankx = rom_bank_ & 0x1FF; rbank = ram_bank_ & 0x0F; break;
    }
    bank0 %= rom_banks_;
    bankx %= rom_banks_;
    rom0_ = rom_ + bank0 * 0x4000;
    romx_ = rom_ + bankx * 0x4000 - 0x4000;   // pre-offset so romx_[addr] works for 4000-7FFF
    uint32_t nbanks = cart_.ram_size / 0x2000;
    if (nbanks == 0) rbank = 0; else rbank %= nbanks;
    cram_bank_ = cram_ ? cram_ + rbank * 0x2000 - 0xA000 : nullptr;
}

void GameBoy::mbc_write(uint16_t addr, uint8_t v) {
    switch (cart_.mbc) {
        case 0: return;
        case 1:
            if (addr < 0x2000) ram_en_ = (v & 0x0F) == 0x0A;
            else if (addr < 0x4000) rom_bank_ = v & 0x1F;
            else if (addr < 0x6000) mbc1_hi_ = v & 0x03;
            else mbc1_mode_ = v & 1;
            break;
        case 2:
            if (addr < 0x4000) {
                if (addr & 0x100) rom_bank_ = v & 0x0F;
                else ram_en_ = (v & 0x0F) == 0x0A;
            }
            break;
        case 3:
            if (addr < 0x2000) ram_en_ = (v & 0x0F) == 0x0A;
            else if (addr < 0x4000) rom_bank_ = v & 0x7F;
            else if (addr < 0x6000) ram_bank_ = v;
            else {
                if (rtc_latch_prev_ == 0 && v == 1) memcpy(rtc_latched_, rtc_, 5);
                rtc_latch_prev_ = v;
            }
            break;
        case 5:
            if (addr < 0x2000) ram_en_ = (v & 0x0F) == 0x0A;
            else if (addr < 0x3000) rom_bank_ = (rom_bank_ & 0x100) | v;
            else if (addr < 0x4000) rom_bank_ = (rom_bank_ & 0xFF) | ((v & 1) << 8);
            else if (addr < 0x6000) ram_bank_ = v & 0x0F;
            break;
    }
    update_banks();
}

GB_HOT void GameBoy::rtc_tick(uint32_t cycles) {
    if (rtc_[4] & 0x40) return;   // halted
    rtc_cycles_ += cycles;
    while (rtc_cycles_ >= CPU_HZ) {
        rtc_cycles_ -= CPU_HZ;
        if (++rtc_[0] < 60) continue;
        rtc_[0] = 0;
        if (++rtc_[1] < 60) continue;
        rtc_[1] = 0;
        if (++rtc_[2] < 24) continue;
        rtc_[2] = 0;
        uint16_t days = rtc_[3] | ((rtc_[4] & 1) << 8);
        days++;
        if (days > 511) { days = 0; rtc_[4] |= 0x80; }
        rtc_[3] = days & 0xFF;
        rtc_[4] = (rtc_[4] & 0xFE) | (days >> 8);
    }
}

// ---------------------------------------------------------------- memory
GB_HOT uint8_t GameBoy::rd(uint16_t a) {
    switch (a >> 12) {
        case 0x0: case 0x1: case 0x2: case 0x3: return rom0_[a];
        case 0x4: case 0x5: case 0x6: case 0x7: return romx_[a];
        case 0x8: case 0x9: return vram_[a - 0x8000];
        case 0xA: case 0xB:
            if (!ram_en_) return 0xFF;
            if (cart_.mbc == 2) return cram_ ? (cram_[a & 0x1FF] | 0xF0) : 0xFF;
            if (cart_.mbc == 3 && ram_bank_ >= 0x08 && ram_bank_ <= 0x0C) return rtc_latched_[ram_bank_ - 8];
            if (!cram_bank_ || cart_.ram_size == 0) return 0xFF;
            if (cart_.ram_size < 0x2000) return cram_[(a - 0xA000) % cart_.ram_size];
            return cram_bank_[a];
        case 0xC: return wram_[a - 0xC000];
        case 0xD: return wram_[a - 0xC000];
        case 0xE: return wram_[a - 0xE000];
        default:
            if (a < 0xFE00) return wram_[a - 0xE000];
            if (a < 0xFEA0) return oam_[a - 0xFE00];
            if (a < 0xFF00) return 0xFF;
            if (a < 0xFF80) return io_read(a);
            if (a < 0xFFFF) return hram_[a - 0xFF80];
            return ie_;
    }
}

GB_HOT void GameBoy::wr(uint16_t a, uint8_t v) {
    switch (a >> 12) {
        case 0x0: case 0x1: case 0x2: case 0x3: case 0x4: case 0x5: case 0x6: case 0x7:
            mbc_write(a, v); return;
        case 0x8: case 0x9: vram_[a - 0x8000] = v; return;
        case 0xA: case 0xB:
            if (!ram_en_) return;
            if (cart_.mbc == 2) { if (cram_) { cram_[a & 0x1FF] = v & 0x0F; ram_dirty_ = true; } return; }
            if (cart_.mbc == 3 && ram_bank_ >= 0x08 && ram_bank_ <= 0x0C) {
                rtc_[ram_bank_ - 8] = v; rtc_latched_[ram_bank_ - 8] = v; return;
            }
            if (!cram_bank_ || cart_.ram_size == 0) return;
            if (cart_.ram_size < 0x2000) cram_[(a - 0xA000) % cart_.ram_size] = v;
            else cram_bank_[a] = v;
            ram_dirty_ = true;
            return;
        case 0xC: case 0xD: wram_[a - 0xC000] = v; return;
        case 0xE: wram_[a - 0xE000] = v; return;
        default:
            if (a < 0xFE00) { wram_[a - 0xE000] = v; return; }
            if (a < 0xFEA0) { oam_[a - 0xFE00] = v; return; }
            if (a < 0xFF00) return;
            if (a < 0xFF80) { io_write(a, v); return; }
            if (a < 0xFFFF) { hram_[a - 0xFF80] = v; return; }
            ie_ = v;
    }
}

GB_HOT uint8_t GameBoy::io_read(uint16_t a) {
    uint8_t r = a & 0x7F;
    switch (r) {
        case R_P1: {
            uint8_t v = 0xCF | (io_[R_P1] & 0x30);
            if (!(io_[R_P1] & 0x10)) v &= ~(buttons_ & 0x0F);
            if (!(io_[R_P1] & 0x20)) v &= ~(buttons_ >> 4);
            return v;
        }
        case R_SC: return io_[R_SC] | 0x7E;
        case R_DIV: return (div_counter_ >> 8) & 0xFF;
        case R_TAC: return io_[R_TAC] | 0xF8;
        case R_IF: return io_[R_IF] | 0xE0;
        case R_STAT: {
            uint8_t v = (io_[R_STAT] & 0x78) | 0x80;
            if (!(io_[R_LCDC] & 0x80)) return v;
            return v | mode_ | (ly_ == io_[R_LYC] ? 0x04 : 0);
        }
        case R_LY: return (io_[R_LCDC] & 0x80) ? ly_ : 0;
        default:
            if (r >= 0x10 && r < 0x40) return apu_.read(a);
            if (r >= 0x4C) return 0xFF;
            if (r == 0x03 || (r >= 0x08 && r < 0x0F)) return 0xFF;
            return io_[r];
    }
}

GB_HOT void GameBoy::io_write(uint16_t a, uint8_t v) {
    uint8_t r = a & 0x7F;
    switch (r) {
        case R_P1: io_[R_P1] = v & 0x30; return;
        case R_SB: io_[R_SB] = v; return;
        case R_SC:
            io_[R_SC] = v;
            if ((v & 0x81) == 0x81) {
                // No link cable: transfer completes immediately, receiving 0xFF
                if (serial_cb) serial_cb(io_[R_SB], serial_user);
                io_[R_SB] = 0xFF;
                io_[R_SC] &= 0x7F;
                io_[R_IF] |= 0x08;
            }
            return;
        case R_DIV: {
            // Resetting DIV can clock TIMA if the selected bit was high
            static const uint32_t bitmask[4] = {512, 8, 32, 128};
            if ((io_[R_TAC] & 4) && (div_counter_ & bitmask[io_[R_TAC] & 3])) {
                if (++io_[R_TIMA] == 0) { io_[R_TIMA] = io_[R_TMA]; io_[R_IF] |= 0x04; }
            }
            div_counter_ = 0;
            return;
        }
        case R_IF: io_[R_IF] = v | 0xE0; return;
        case R_LCDC: {
            bool was_on = io_[R_LCDC] & 0x80;
            io_[R_LCDC] = v;
            if (was_on && !(v & 0x80)) { ly_ = 0; dot_ = 0; mode_ = 0; stat_line_ = false; }
            else if (!was_on && (v & 0x80)) { ly_ = 0; dot_ = 0; mode_ = 2; win_line_ = 0; update_stat(); }
            return;
        }
        case R_STAT: io_[R_STAT] = v & 0x78; update_stat(); return;
        case R_LY: return;
        case R_LYC: io_[R_LYC] = v; update_stat(); return;
        case R_DMA: {
            uint16_t src = v << 8;
            for (int i = 0; i < 0xA0; i++) oam_[i] = rd(src + i);
            io_[R_DMA] = v;
            return;
        }
        default:
            if (r >= 0x10 && r < 0x40) { apu_.write(a, v); return; }
            io_[r] = v;
    }
}

// ---------------------------------------------------------------- timer
GB_HOT void GameBoy::timer_step(uint32_t cycles) {
    uint8_t tac = io_[R_TAC];
    if (tac & 4) {
        static const uint32_t period_shift[4] = {10, 4, 6, 8};
        uint32_t sh = period_shift[tac & 3];
        uint32_t pos = div_counter_ & ((1u << sh) - 1);
        uint32_t ticks = (pos + cycles) >> sh;
        while (ticks--) {
            if (++io_[R_TIMA] == 0) { io_[R_TIMA] = io_[R_TMA]; io_[R_IF] |= 0x04; }
        }
    }
    div_counter_ += cycles;
}

// ---------------------------------------------------------------- frame
GB_HOT void GameBoy::run_frame() {
    frame_done_ = false;
    uint32_t elapsed = 0;
    // A frame ends at the start of VBlank; with the LCD off, after one frame's worth of cycles.
    while (!frame_done_ && elapsed < CYCLES_PER_FRAME + 1000) {
        uint32_t c = cpu_step();
        elapsed += c;
        timer_step(c);
        ppu_step(c);
        apu_.step(c);
        if (cart_.rtc) rtc_tick(c);
        if (!(io_[R_LCDC] & 0x80) && elapsed >= CYCLES_PER_FRAME) break;
    }
}

} // namespace gb
