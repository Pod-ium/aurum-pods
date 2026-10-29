// PodBoy - PPU: scanline renderer with DMG mode timing
#include "gb.h"
#include <string.h>

namespace gb {

enum { R_IF = 0x0F, R_LCDC = 0x40, R_STAT = 0x41, R_SCY = 0x42, R_SCX = 0x43, R_LYC = 0x45,
       R_BGP = 0x47, R_OBP0 = 0x48, R_OBP1 = 0x49, R_WY = 0x4A, R_WX = 0x4B };

GB_HOT void GameBoy::update_stat() {
    if (!(io_[R_LCDC] & 0x80)) { stat_line_ = false; return; }
    uint8_t s = io_[R_STAT];
    bool line = ((s & 0x40) && ly_ == io_[0x45]) ||
                ((s & 0x08) && mode_ == 0) ||
                ((s & 0x10) && mode_ == 1) ||
                ((s & 0x20) && mode_ == 2);
    if (line && !stat_line_) io_[R_IF] |= 0x02;
    stat_line_ = line;
}

GB_HOT void GameBoy::ppu_step(uint32_t cycles) {
    if (!(io_[R_LCDC] & 0x80)) return;
    dot_ += cycles;
    for (;;) {
        switch (mode_) {
        case 2:
            if (dot_ < 80) return;
            mode_ = 3;
            break;
        case 3:
            if (dot_ < 252) return;
            render_line();
            mode_ = 0;
            update_stat();
            break;
        case 0:
            if (dot_ < 456) return;
            dot_ -= 456;
            ly_++;
            if (ly_ == 144) {
                mode_ = 1;
                io_[R_IF] |= 0x01;
                frame_done_ = true;
            } else {
                mode_ = 2;
            }
            update_stat();
            break;
        default: // 1 (VBlank)
            if (dot_ < 456) return;
            dot_ -= 456;
            ly_++;
            if (ly_ > 153) { ly_ = 0; win_line_ = 0; mode_ = 2; }
            update_stat();
            break;
        }
    }
}

GB_HOT void GameBoy::render_line() {
    if (!fb_) return;
    const uint8_t lcdc = io_[R_LCDC];
    const int ly = ly_;
    uint8_t* out = fb_ + ly * SCREEN_W;
    uint8_t bgidx[SCREEN_W];   // raw background colour index, for sprite priority

    const uint8_t bgp = io_[R_BGP];
    if (lcdc & 0x01) {
        const bool signed_tiles = !(lcdc & 0x10);
        const uint8_t* bgmap = vram_ + ((lcdc & 0x08) ? 0x1C00 : 0x1800);
        const int wx = io_[R_WX] - 7;
        const bool win = (lcdc & 0x20) && ly >= io_[R_WY] && io_[R_WX] <= 166;
        const int bg_end = win ? (wx < 0 ? 0 : wx) : SCREEN_W;

        // Background
        {
            const uint8_t y = ly + io_[R_SCY];
            const uint8_t* row = bgmap + (y >> 3) * 32;
            const int ty = (y & 7) * 2;
            uint8_t x = io_[R_SCX];
            int px = 0;
            while (px < bg_end) {
                uint8_t tile = row[x >> 3];
                const uint8_t* t = signed_tiles ? vram_ + 0x1000 + (int8_t)tile * 16 : vram_ + tile * 16;
                uint8_t lo = t[ty], hi = t[ty + 1];
                int bit = 7 - (x & 7);
                for (; bit >= 0 && px < bg_end; bit--, px++, x++) {
                    uint8_t ci = ((lo >> bit) & 1) | (((hi >> bit) & 1) << 1);
                    bgidx[px] = ci;
                    out[px] = (bgp >> (ci * 2)) & 3;
                }
            }
        }
        // Window
        if (win && wx < SCREEN_W) {
            const uint8_t* wmap = vram_ + ((lcdc & 0x40) ? 0x1C00 : 0x1800);
            const uint8_t* row = wmap + (win_line_ >> 3) * 32;
            const int ty = (win_line_ & 7) * 2;
            int px = wx < 0 ? 0 : wx;
            int wxpos = px - wx;
            while (px < SCREEN_W) {
                uint8_t tile = row[(wxpos >> 3) & 31];
                const uint8_t* t = signed_tiles ? vram_ + 0x1000 + (int8_t)tile * 16 : vram_ + tile * 16;
                uint8_t lo = t[ty], hi = t[ty + 1];
                int bit = 7 - (wxpos & 7);
                for (; bit >= 0 && px < SCREEN_W; bit--, px++, wxpos++) {
                    uint8_t ci = ((lo >> bit) & 1) | (((hi >> bit) & 1) << 1);
                    bgidx[px] = ci;
                    out[px] = (bgp >> (ci * 2)) & 3;
                }
            }
            win_line_++;
        }
    } else {
        memset(bgidx, 0, sizeof bgidx);
        memset(out, 0, SCREEN_W);
    }

    // Sprites
    if (lcdc & 0x02) {
        const int h = (lcdc & 0x04) ? 16 : 8;
        uint8_t sel[10]; int n = 0;
        for (int i = 0; i < 40 && n < 10; i++) {
            int sy = oam_[i * 4] - 16;
            if (ly >= sy && ly < sy + h) sel[n++] = i;
        }
        // Priority: smaller X wins, ties go to lower OAM index. The first opaque
        // sprite pixel claims the spot, even if the background then hides it.
        for (int i = 1; i < n; i++) {
            uint8_t k = sel[i]; int j = i - 1;
            while (j >= 0 && oam_[sel[j] * 4 + 1] > oam_[k * 4 + 1]) { sel[j + 1] = sel[j]; j--; }
            sel[j + 1] = k;
        }
        bool claimed[SCREEN_W];
        memset(claimed, 0, sizeof claimed);
        for (int s = 0; s < n; s++) {
            const uint8_t* o = oam_ + sel[s] * 4;
            int sy = o[0] - 16, sx = o[1] - 8;
            uint8_t tile = o[2], attr = o[3];
            int line = ly - sy;
            if (attr & 0x40) line = h - 1 - line;
            if (h == 16) tile &= 0xFE;
            const uint8_t* t = vram_ + tile * 16 + line * 2;
            uint8_t lo = t[0], hi = t[1];
            uint8_t pal = (attr & 0x10) ? io_[R_OBP1] : io_[R_OBP0];
            for (int i = 0; i < 8; i++) {
                int px = sx + i;
                if (px < 0 || px >= SCREEN_W) continue;
                int bit = (attr & 0x20) ? i : 7 - i;
                uint8_t ci = ((lo >> bit) & 1) | (((hi >> bit) & 1) << 1);
                if (!ci || claimed[px]) continue;
                claimed[px] = true;
                if ((attr & 0x80) && bgidx[px]) continue;
                out[px] = (pal >> (ci * 2)) & 3;
            }
        }
    }
}

} // namespace gb
