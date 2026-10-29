// PodBoy - APU: two square channels (one with sweep), wave, noise.
// Produces stereo samples at 32768 Hz (one every 128 CPU cycles).
#include "gb.h"
#include <string.h>

namespace gb {

// Register offsets relative to 0xFF10
enum { NR10 = 0x00, NR11, NR12, NR13, NR14, NR21 = 0x06, NR22, NR23, NR24,
       NR30 = 0x0A, NR31, NR32, NR33, NR34, NR41 = 0x10, NR42, NR43, NR44,
       NR50 = 0x14, NR51, NR52, WAVE = 0x20 };

static const uint8_t DUTY[4] = {0x01, 0x81, 0x87, 0x7E};   // 12.5%, 25%, 50%, 75%
static const uint8_t READ_MASK[0x30] = {
    0x80, 0x3F, 0x00, 0xFF, 0xBF, 0xFF, 0x3F, 0x00, 0xFF, 0xBF, 0x7F, 0xFF, 0x9F, 0xFF, 0xBF, 0xFF,
    0xFF, 0x00, 0x00, 0xBF, 0x00, 0x00, 0x70, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};

void APU::reset() {
    memset(sq_, 0, sizeof sq_);
    memset(&wave_, 0, sizeof wave_);
    memset(&noise_, 0, sizeof noise_);
    memset(regs_, 0, sizeof regs_);
    power_ = true;
    fs_counter_ = 0; sample_counter_ = 0; fs_step_ = 0;
    hp_l_ = hp_r_ = 0;
    nsamples_ = 0;
    noise_.lfsr = 0x7FFF;
    sq_[0].timer = sq_[1].timer = wave_.timer = noise_.timer = 8192;
}

void APU::power_on_defaults() {
    static const uint8_t init[0x17] = {0x80, 0xBF, 0xF3, 0xFF, 0xBF, 0xFF, 0x3F, 0x00, 0xFF, 0xBF,
                                       0x7F, 0xFF, 0x9F, 0xFF, 0xBF, 0xFF, 0xFF, 0x00, 0x00, 0xBF,
                                       0x77, 0xF3, 0xF1};
    for (int i = 0; i < 0x16; i++) write(0xFF10 + i, init[i] & ~((i == NR14 || i == NR24 || i == NR34 || i == NR44) ? 0x80 : 0));
    power_ = true;
    // Channel 1 was left playing (silently) by the boot ROM; mark enabled with volume 0.
    sq_[0].enabled = false;
}

GB_HOT uint8_t APU::read(uint16_t addr) const {
    int r = addr - 0xFF10;
    if (r >= WAVE) return regs_[r];
    if (r == NR52) {
        return (power_ ? 0x80 : 0) | 0x70 | (sq_[0].enabled ? 1 : 0) | (sq_[1].enabled ? 2 : 0) |
               (wave_.enabled ? 4 : 0) | (noise_.enabled ? 8 : 0);
    }
    return regs_[r] | READ_MASK[r];
}

GB_HOT void APU::write(uint16_t addr, uint8_t v) {
    int r = addr - 0xFF10;
    if (r >= WAVE) { regs_[r] = v; return; }
    if (r == NR52) {
        bool on = v & 0x80;
        if (!on && power_) {
            for (int i = 0; i < NR52; i++) regs_[i] = 0;
            sq_[0].enabled = sq_[1].enabled = wave_.enabled = noise_.enabled = false;
            sq_[0].dac = sq_[1].dac = wave_.dac = noise_.dac = false;
        }
        if (on && !power_) { fs_step_ = 0; sq_[0].pos = sq_[1].pos = 0; }
        power_ = on;
        return;
    }
    if (!power_) return;
    regs_[r] = v;

    switch (r) {
    case NR10:
        sq_[0].sw_period = (v >> 4) & 7; sq_[0].sw_neg = v & 8; sq_[0].sw_shift = v & 7; break;
    case NR11: case NR21: {
        Square& s = sq_[r == NR11 ? 0 : 1];
        s.duty = v >> 6; s.len = 64 - (v & 0x3F); break;
    }
    case NR12: case NR22: {
        Square& s = sq_[r == NR12 ? 0 : 1];
        s.env_init = v >> 4; s.env_up = v & 8; s.env_period = v & 7;
        s.dac = (v & 0xF8) != 0;
        if (!s.dac) s.enabled = false;
        break;
    }
    case NR13: case NR23: {
        Square& s = sq_[r == NR13 ? 0 : 1];
        s.freq = (s.freq & 0x700) | v; break;
    }
    case NR14: case NR24: {
        int i = r == NR14 ? 0 : 1;
        Square& s = sq_[i];
        s.freq = (s.freq & 0xFF) | ((v & 7) << 8);
        s.len_en = v & 0x40;
        if (v & 0x80) trigger_square(i);
        break;
    }
    case NR30:
        wave_.dac = v & 0x80;
        if (!wave_.dac) wave_.enabled = false;
        break;
    case NR31: wave_.len = 256 - v; break;
    case NR32: { static const uint8_t sh[4] = {4, 0, 1, 2}; wave_.shift = sh[(v >> 5) & 3]; break; }
    case NR33: wave_.freq = (wave_.freq & 0x700) | v; break;
    case NR34:
        wave_.freq = (wave_.freq & 0xFF) | ((v & 7) << 8);
        wave_.len_en = v & 0x40;
        if (v & 0x80) trigger_wave();
        break;
    case NR41: noise_.len = 64 - (v & 0x3F); break;
    case NR42:
        noise_.env_init = v >> 4; noise_.env_up = v & 8; noise_.env_period = v & 7;
        noise_.dac = (v & 0xF8) != 0;
        if (!noise_.dac) noise_.enabled = false;
        break;
    case NR43: {
        static const uint8_t div[8] = {8, 16, 32, 48, 64, 80, 96, 112};
        noise_.width7 = v & 8;
        noise_.period = (uint32_t)div[v & 7] << (v >> 4);
        break;
    }
    case NR44:
        noise_.len_en = v & 0x40;
        if (v & 0x80) trigger_noise();
        break;
    default: break;
    }
}

void APU::trigger_square(int i) {
    Square& s = sq_[i];
    s.enabled = s.dac;
    if (s.len == 0) s.len = 64;
    s.timer = (2048 - s.freq) * 4;
    s.vol = s.env_init;
    s.env_timer = s.env_period;
    if (i == 0) {
        s.shadow = s.freq;
        s.sw_timer = s.sw_period ? s.sw_period : 8;
        s.sw_en = s.sw_period || s.sw_shift;
        if (s.sw_shift && sweep_calc() > 2047) s.enabled = false;
    }
}

void APU::trigger_wave() {
    wave_.enabled = wave_.dac;
    if (wave_.len == 0) wave_.len = 256;
    wave_.timer = (2048 - wave_.freq) * 2;
    wave_.pos = 0;
}

void APU::trigger_noise() {
    noise_.enabled = noise_.dac;
    if (noise_.len == 0) noise_.len = 64;
    noise_.vol = noise_.env_init;
    noise_.env_timer = noise_.env_period;
    noise_.lfsr = 0x7FFF;
    noise_.timer = noise_.period;
}

uint16_t APU::sweep_calc() {
    Square& s = sq_[0];
    uint16_t delta = s.shadow >> s.sw_shift;
    uint16_t nf = s.sw_neg ? s.shadow - delta : s.shadow + delta;
    if (nf > 2047) s.enabled = false;
    return nf;
}

void APU::clock_length() {
    for (auto& s : sq_) if (s.len_en && s.len && --s.len == 0) s.enabled = false;
    if (wave_.len_en && wave_.len && --wave_.len == 0) wave_.enabled = false;
    if (noise_.len_en && noise_.len && --noise_.len == 0) noise_.enabled = false;
}

void APU::clock_envelope() {
    for (auto& s : sq_) {
        if (!s.env_period) continue;
        if (--s.env_timer == 0) {
            s.env_timer = s.env_period;
            if (s.env_up && s.vol < 15) s.vol++;
            else if (!s.env_up && s.vol > 0) s.vol--;
        }
    }
    if (noise_.env_period && --noise_.env_timer == 0) {
        noise_.env_timer = noise_.env_period;
        if (noise_.env_up && noise_.vol < 15) noise_.vol++;
        else if (!noise_.env_up && noise_.vol > 0) noise_.vol--;
    }
}

void APU::clock_sweep() {
    Square& s = sq_[0];
    if (--s.sw_timer == 0) {
        s.sw_timer = s.sw_period ? s.sw_period : 8;
        if (s.sw_en && s.sw_period) {
            uint16_t nf = sweep_calc();
            if (nf <= 2047 && s.sw_shift) {
                s.freq = s.shadow = nf;
                sweep_calc();
            }
        }
    }
}

GB_HOT void APU::step(uint32_t cycles) {
    fs_counter_ += cycles;
    while (fs_counter_ >= 8192) {
        fs_counter_ -= 8192;
        if (power_) {
            if (!(fs_step_ & 1)) clock_length();
            if (fs_step_ == 2 || fs_step_ == 6) clock_sweep();
            if (fs_step_ == 7) clock_envelope();
        }
        fs_step_ = (fs_step_ + 1) & 7;
    }
    sample_counter_ += cycles;
    while (sample_counter_ >= 128) {
        sample_counter_ -= 128;
        emit_sample();
    }
}

GB_HOT void APU::emit_sample() {
    int out[4] = {0, 0, 0, 0};

    for (int i = 0; i < 2; i++) {
        Square& s = sq_[i];
        int32_t period = (2048 - s.freq) * 4;
        s.timer -= 128;
        while (s.timer <= 0) { s.timer += period; s.pos = (s.pos + 1) & 7; }
        if (s.enabled && s.dac) out[i] = ((DUTY[s.duty] >> s.pos) & 1) ? s.vol * 2 - 15 : -15;
    }
    {
        int32_t period = (2048 - wave_.freq) * 2;
        wave_.timer -= 128;
        while (wave_.timer <= 0) { wave_.timer += period; wave_.pos = (wave_.pos + 1) & 31; }
        if (wave_.enabled && wave_.dac) {
            uint8_t b = regs_[WAVE + (wave_.pos >> 1)];
            uint8_t smp = (wave_.pos & 1) ? (b & 0x0F) : (b >> 4);
            out[2] = (smp >> wave_.shift) * 2 - 15;
        }
    }
    if (noise_.period < (8u << 14)) {
        noise_.timer -= 128;
        while (noise_.timer <= 0) {
            noise_.timer += noise_.period;
            uint16_t x = (noise_.lfsr ^ (noise_.lfsr >> 1)) & 1;
            noise_.lfsr = (noise_.lfsr >> 1) | (x << 14);
            if (noise_.width7) noise_.lfsr = (noise_.lfsr & ~0x40) | (x << 6);
        }
    }
    if (noise_.enabled && noise_.dac) out[3] = (noise_.lfsr & 1) ? -15 : noise_.vol * 2 - 15;

    int l = 0, r = 0;
    if (power_) {
        uint8_t nr51 = regs_[NR51];
        for (int i = 0; i < 4; i++) {
            if (nr51 & (0x10 << i)) l += out[i];
            if (nr51 & (0x01 << i)) r += out[i];
        }
        l *= ((regs_[NR50] >> 4) & 7) + 1;
        r *= (regs_[NR50] & 7) + 1;
    }
    // High-pass filter (models the output capacitor) removes the DC offset.
    float fl = (float)l, fr = (float)r;
    float ol = fl - hp_l_, or_ = fr - hp_r_;
    hp_l_ = fl - ol * 0.996f;
    hp_r_ = fr - or_ * 0.996f;
    if (nsamples_ < AUDIO_MAX_SAMPLES) {
        int sl = (int)(ol * 64.0f), sr = (int)(or_ * 64.0f);
        if (sl > 32767) sl = 32767;
        if (sl < -32768) sl = -32768;
        if (sr > 32767) sr = 32767;
        if (sr < -32768) sr = -32768;
        buf_[nsamples_ * 2] = (int16_t)sl;
        buf_[nsamples_ * 2 + 1] = (int16_t)sr;
        nsamples_++;
    }
}

} // namespace gb
