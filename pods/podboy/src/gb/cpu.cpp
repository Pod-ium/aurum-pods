// PodBoy - Sharp SM83 (LR35902) CPU interpreter
// Timing: every memory access costs 4 T-cycles, plus explicit internal delays,
// which reproduces the documented per-instruction cycle counts.
#include "gb.h"

namespace gb {

enum { FZ = 0x80, FN = 0x40, FH = 0x20, FC = 0x10 };

#define BC ((uint16_t)((b_ << 8) | c_))
#define DE ((uint16_t)((d_ << 8) | e_))
#define HL ((uint16_t)((h_ << 8) | l_))
#define SET_BC(v) do { uint16_t _v = (v); b_ = _v >> 8; c_ = _v & 0xFF; } while (0)
#define SET_DE(v) do { uint16_t _v = (v); d_ = _v >> 8; e_ = _v & 0xFF; } while (0)
#define SET_HL(v) do { uint16_t _v = (v); h_ = _v >> 8; l_ = _v & 0xFF; } while (0)
#define IDLE() (cyc_ += 4)

inline uint8_t GameBoy::fetch8() {
    uint8_t v = rd(pc_);
    if (halt_bug_) halt_bug_ = false; else pc_++;
    cyc_ += 4;
    return v;
}
inline uint16_t GameBoy::fetch16() { uint16_t lo = fetch8(); return lo | (fetch8() << 8); }
inline void GameBoy::push16(uint16_t v) {
    sp_--; wr(sp_, v >> 8); cyc_ += 4;
    sp_--; wr(sp_, v & 0xFF); cyc_ += 4;
}
inline uint16_t GameBoy::pop16() {
    uint16_t lo = rd(sp_++); cyc_ += 4;
    uint16_t hi = rd(sp_++); cyc_ += 4;
    return lo | (hi << 8);
}

inline uint8_t GameBoy::get_r(int i) {
    switch (i) {
        case 0: return b_; case 1: return c_; case 2: return d_; case 3: return e_;
        case 4: return h_; case 5: return l_; case 6: { uint8_t v = rd(HL); cyc_ += 4; return v; }
        default: return a_;
    }
}
inline void GameBoy::set_r(int i, uint8_t v) {
    switch (i) {
        case 0: b_ = v; break; case 1: c_ = v; break; case 2: d_ = v; break; case 3: e_ = v; break;
        case 4: h_ = v; break; case 5: l_ = v; break; case 6: wr(HL, v); cyc_ += 4; break;
        default: a_ = v; break;
    }
}

inline void GameBoy::alu(int op, uint8_t v) {
    unsigned r;
    switch (op) {
        case 0: // ADD
            r = a_ + v;
            f_ = ((r & 0xFF) ? 0 : FZ) | (((a_ & 0xF) + (v & 0xF)) > 0xF ? FH : 0) | (r > 0xFF ? FC : 0);
            a_ = r; break;
        case 1: { // ADC
            unsigned c = (f_ & FC) ? 1 : 0;
            r = a_ + v + c;
            f_ = ((r & 0xFF) ? 0 : FZ) | (((a_ & 0xF) + (v & 0xF) + c) > 0xF ? FH : 0) | (r > 0xFF ? FC : 0);
            a_ = r; break;
        }
        case 2: // SUB
            r = a_ - v;
            f_ = FN | ((r & 0xFF) ? 0 : FZ) | ((a_ & 0xF) < (v & 0xF) ? FH : 0) | (a_ < v ? FC : 0);
            a_ = r; break;
        case 3: { // SBC
            int c = (f_ & FC) ? 1 : 0;
            int res = (int)a_ - v - c;
            f_ = FN | ((res & 0xFF) ? 0 : FZ) | (((int)(a_ & 0xF) - (v & 0xF) - c) < 0 ? FH : 0) | (res < 0 ? FC : 0);
            a_ = res; break;
        }
        case 4: a_ &= v; f_ = (a_ ? 0 : FZ) | FH; break;
        case 5: a_ ^= v; f_ = a_ ? 0 : FZ; break;
        case 6: a_ |= v; f_ = a_ ? 0 : FZ; break;
        default: // CP
            r = a_ - v;
            f_ = FN | ((r & 0xFF) ? 0 : FZ) | ((a_ & 0xF) < (v & 0xF) ? FH : 0) | (a_ < v ? FC : 0);
            break;
    }
}

GB_HOT void GameBoy::exec_cb() {
    uint8_t op = fetch8();
    int r = op & 7;
    int y = (op >> 3) & 7;
    uint8_t v = get_r(r);
    switch (op >> 6) {
        case 0: {
            uint8_t res; uint8_t c = 0;
            switch (y) {
                case 0: c = v >> 7; res = (v << 1) | c; break;                         // RLC
                case 1: c = v & 1; res = (v >> 1) | (c << 7); break;                   // RRC
                case 2: c = v >> 7; res = (v << 1) | ((f_ & FC) ? 1 : 0); break;       // RL
                case 3: c = v & 1; res = (v >> 1) | ((f_ & FC) ? 0x80 : 0); break;    // RR
                case 4: c = v >> 7; res = v << 1; break;                               // SLA
                case 5: c = v & 1; res = (v >> 1) | (v & 0x80); break;                 // SRA
                case 6: res = (v << 4) | (v >> 4); break;                              // SWAP
                default: c = v & 1; res = v >> 1; break;                               // SRL
            }
            f_ = (res ? 0 : FZ) | (c ? FC : 0);
            set_r(r, res);
            break;
        }
        case 1: // BIT
            f_ = (f_ & FC) | FH | ((v & (1 << y)) ? 0 : FZ);
            break;
        case 2: set_r(r, v & ~(1 << y)); break;   // RES
        default: set_r(r, v | (1 << y)); break;   // SET
    }
}

GB_HOT uint32_t GameBoy::cpu_step() {
    cyc_ = 0;
    uint8_t pending = ie_ & io_[0x0F] & 0x1F;
    if (halted_) {
        if (!pending) return 4;
        halted_ = false;
    }
    if (ime_ && pending) {
        int bit = __builtin_ctz(pending);
        ime_ = false;
        io_[0x0F] &= ~(1 << bit);
        IDLE(); IDLE();
        push16(pc_);
        IDLE();
        pc_ = 0x40 + bit * 8;
        return cyc_;
    }

    uint8_t op = fetch8();

    if (op >= 0x40 && op < 0x80) {            // LD r,r' / HALT
        if (op == 0x76) {
            if (!ime_ && pending) halt_bug_ = true;
            else halted_ = true;
        } else {
            set_r((op >> 3) & 7, get_r(op & 7));
        }
    } else if (op >= 0x80 && op < 0xC0) {     // ALU A,r
        alu((op >> 3) & 7, get_r(op & 7));
    } else {
        switch (op) {
        case 0x00: break;
        case 0x01: SET_BC(fetch16()); break;
        case 0x11: SET_DE(fetch16()); break;
        case 0x21: SET_HL(fetch16()); break;
        case 0x31: sp_ = fetch16(); break;
        case 0x02: wr(BC, a_); cyc_ += 4; break;
        case 0x12: wr(DE, a_); cyc_ += 4; break;
        case 0x22: { uint16_t hl = HL; wr(hl, a_); cyc_ += 4; SET_HL(hl + 1); break; }
        case 0x32: { uint16_t hl = HL; wr(hl, a_); cyc_ += 4; SET_HL(hl - 1); break; }
        case 0x0A: a_ = rd(BC); cyc_ += 4; break;
        case 0x1A: a_ = rd(DE); cyc_ += 4; break;
        case 0x2A: { uint16_t hl = HL; a_ = rd(hl); cyc_ += 4; SET_HL(hl + 1); break; }
        case 0x3A: { uint16_t hl = HL; a_ = rd(hl); cyc_ += 4; SET_HL(hl - 1); break; }
        case 0x03: SET_BC(BC + 1); IDLE(); break;
        case 0x13: SET_DE(DE + 1); IDLE(); break;
        case 0x23: SET_HL(HL + 1); IDLE(); break;
        case 0x33: sp_++; IDLE(); break;
        case 0x0B: SET_BC(BC - 1); IDLE(); break;
        case 0x1B: SET_DE(DE - 1); IDLE(); break;
        case 0x2B: SET_HL(HL - 1); IDLE(); break;
        case 0x3B: sp_--; IDLE(); break;
        // INC r / DEC r / LD r,n
        case 0x04: case 0x0C: case 0x14: case 0x1C: case 0x24: case 0x2C: case 0x34: case 0x3C: {
            int r = (op >> 3) & 7; uint8_t v = get_r(r); uint8_t res = v + 1;
            f_ = (f_ & FC) | (res ? 0 : FZ) | ((v & 0xF) == 0xF ? FH : 0);
            set_r(r, res); break;
        }
        case 0x05: case 0x0D: case 0x15: case 0x1D: case 0x25: case 0x2D: case 0x35: case 0x3D: {
            int r = (op >> 3) & 7; uint8_t v = get_r(r); uint8_t res = v - 1;
            f_ = (f_ & FC) | FN | (res ? 0 : FZ) | ((v & 0xF) == 0 ? FH : 0);
            set_r(r, res); break;
        }
        case 0x06: case 0x0E: case 0x16: case 0x1E: case 0x26: case 0x2E: case 0x36: case 0x3E: {
            uint8_t n = fetch8(); set_r((op >> 3) & 7, n); break;
        }
        case 0x07: { uint8_t c = a_ >> 7; a_ = (a_ << 1) | c; f_ = c ? FC : 0; break; }            // RLCA
        case 0x0F: { uint8_t c = a_ & 1; a_ = (a_ >> 1) | (c << 7); f_ = c ? FC : 0; break; }      // RRCA
        case 0x17: { uint8_t c = a_ >> 7; a_ = (a_ << 1) | ((f_ & FC) ? 1 : 0); f_ = c ? FC : 0; break; } // RLA
        case 0x1F: { uint8_t c = a_ & 1; a_ = (a_ >> 1) | ((f_ & FC) ? 0x80 : 0); f_ = c ? FC : 0; break; } // RRA
        case 0x08: { uint16_t addr = fetch16(); wr(addr, sp_ & 0xFF); wr(addr + 1, sp_ >> 8); cyc_ += 8; break; }
        case 0x09: case 0x19: case 0x29: case 0x39: {
            uint16_t hl = HL;
            uint16_t v = op == 0x09 ? BC : op == 0x19 ? DE : op == 0x29 ? hl : sp_;
            uint32_t r = hl + v;
            f_ = (f_ & FZ) | (((hl & 0xFFF) + (v & 0xFFF)) > 0xFFF ? FH : 0) | (r > 0xFFFF ? FC : 0);
            SET_HL(r); IDLE(); break;
        }
        case 0x10: fetch8(); break;   // STOP: treated as a 2-byte NOP
        case 0x18: { int8_t e = (int8_t)fetch8(); pc_ += e; IDLE(); break; }
        case 0x20: case 0x28: case 0x30: case 0x38: {
            int8_t e = (int8_t)fetch8();
            bool cond;
            switch (op) {
                case 0x20: cond = !(f_ & FZ); break; case 0x28: cond = f_ & FZ; break;
                case 0x30: cond = !(f_ & FC); break; default: cond = f_ & FC; break;
            }
            if (cond) { pc_ += e; IDLE(); }
            break;
        }
        case 0x27: { // DAA
            uint8_t a = a_; bool c = f_ & FC;
            if (!(f_ & FN)) {
                if (c || a > 0x99) { a += 0x60; c = true; }
                if ((f_ & FH) || (a & 0x0F) > 0x09) a += 0x06;
            } else {
                if (c) a -= 0x60;
                if (f_ & FH) a -= 0x06;
            }
            a_ = a;
            f_ = (f_ & FN) | (a ? 0 : FZ) | (c ? FC : 0);
            break;
        }
        case 0x2F: a_ = ~a_; f_ |= FN | FH; break;               // CPL
        case 0x37: f_ = (f_ & FZ) | FC; break;                   // SCF
        case 0x3F: f_ = (f_ & FZ) | ((f_ & FC) ? 0 : FC); break; // CCF

        // ---- control flow ----
        case 0xC0: case 0xC8: case 0xD0: case 0xD8: {  // RET cc
            bool cond;
            switch (op) {
                case 0xC0: cond = !(f_ & FZ); break; case 0xC8: cond = f_ & FZ; break;
                case 0xD0: cond = !(f_ & FC); break; default: cond = f_ & FC; break;
            }
            IDLE();
            if (cond) { pc_ = pop16(); IDLE(); }
            break;
        }
        case 0xC9: pc_ = pop16(); IDLE(); break;                           // RET
        case 0xD9: pc_ = pop16(); IDLE(); ime_ = true; ei_delay_ = 0; break;  // RETI
        case 0xC2: case 0xCA: case 0xD2: case 0xDA: {  // JP cc,nn
            uint16_t addr = fetch16();
            bool cond;
            switch (op) {
                case 0xC2: cond = !(f_ & FZ); break; case 0xCA: cond = f_ & FZ; break;
                case 0xD2: cond = !(f_ & FC); break; default: cond = f_ & FC; break;
            }
            if (cond) { pc_ = addr; IDLE(); }
            break;
        }
        case 0xC3: pc_ = fetch16(); IDLE(); break;
        case 0xE9: pc_ = HL; break;
        case 0xC4: case 0xCC: case 0xD4: case 0xDC: {  // CALL cc,nn
            uint16_t addr = fetch16();
            bool cond;
            switch (op) {
                case 0xC4: cond = !(f_ & FZ); break; case 0xCC: cond = f_ & FZ; break;
                case 0xD4: cond = !(f_ & FC); break; default: cond = f_ & FC; break;
            }
            if (cond) { IDLE(); push16(pc_); pc_ = addr; }
            break;
        }
        case 0xCD: { uint16_t addr = fetch16(); IDLE(); push16(pc_); pc_ = addr; break; }
        case 0xC7: case 0xCF: case 0xD7: case 0xDF: case 0xE7: case 0xEF: case 0xF7: case 0xFF:
            IDLE(); push16(pc_); pc_ = op & 0x38; break;                // RST

        case 0xC1: SET_BC(pop16()); break;
        case 0xD1: SET_DE(pop16()); break;
        case 0xE1: SET_HL(pop16()); break;
        case 0xF1: { uint16_t v = pop16(); a_ = v >> 8; f_ = v & 0xF0; break; }
        case 0xC5: IDLE(); push16(BC); break;
        case 0xD5: IDLE(); push16(DE); break;
        case 0xE5: IDLE(); push16(HL); break;
        case 0xF5: IDLE(); push16((a_ << 8) | f_); break;

        case 0xC6: case 0xCE: case 0xD6: case 0xDE: case 0xE6: case 0xEE: case 0xF6: case 0xFE:
            alu((op >> 3) & 7, fetch8()); break;

        case 0xCB: exec_cb(); break;

        case 0xE0: { uint8_t n = fetch8(); wr(0xFF00 | n, a_); cyc_ += 4; break; }
        case 0xF0: { uint8_t n = fetch8(); a_ = rd(0xFF00 | n); cyc_ += 4; break; }
        case 0xE2: wr(0xFF00 | c_, a_); cyc_ += 4; break;
        case 0xF2: a_ = rd(0xFF00 | c_); cyc_ += 4; break;
        case 0xEA: { uint16_t addr = fetch16(); wr(addr, a_); cyc_ += 4; break; }
        case 0xFA: { uint16_t addr = fetch16(); a_ = rd(addr); cyc_ += 4; break; }

        case 0xE8: {   // ADD SP,e
            uint8_t u = fetch8(); int8_t e = (int8_t)u;
            f_ = (((sp_ & 0xF) + (u & 0xF)) > 0xF ? FH : 0) | (((sp_ & 0xFF) + u) > 0xFF ? FC : 0);
            sp_ += e; IDLE(); IDLE(); break;
        }
        case 0xF8: {   // LD HL,SP+e
            uint8_t u = fetch8(); int8_t e = (int8_t)u;
            f_ = (((sp_ & 0xF) + (u & 0xF)) > 0xF ? FH : 0) | (((sp_ & 0xFF) + u) > 0xFF ? FC : 0);
            SET_HL(sp_ + e); IDLE(); break;
        }
        case 0xF9: sp_ = HL; IDLE(); break;

        case 0xF3: ime_ = false; ei_delay_ = 0; break;          // DI
        case 0xFB: if (!ime_ && !ei_delay_) ei_delay_ = 2; break; // EI (takes effect after next instr)

        default: // illegal opcodes lock up the real CPU; approximate by halting forever
            halted_ = true; ie_ = 0; break;
        }
    }

    if (ei_delay_ && --ei_delay_ == 0) ime_ = true;
    return cyc_;
}

} // namespace gb
