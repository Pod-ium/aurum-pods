// PodBoy - Game Boy emulator firmware for the ClockworkPi PicoCalc, on the PicoPods libraries.
//
// Core 0: emulation, video (IndexedBlitter, DMA), audio, SD card, UI.
// Core 1: keyboard polling (the PicoPods background keyboard: the controller needs
//         slow, blocking I2C reads).
#include <cstdio>
#include <cstring>
#include <string>

#include "config.h"
#include "gb/gb.h"
#include "pd/blit.h"
#include "pd/flash_cache.h"
#include "pd/pd.h"

using namespace pd;

namespace {

constexpr int GAME_Y = 16;   // top of the 320x288 game area
constexpr int GAME_H = 288;

gb::GameBoy gbx;
uint8_t framebuf[2][gb::SCREEN_W * gb::SCREEN_H];
uint8_t cart_ram[CART_RAM_MAX];

std::string cur_dir = ROM_DIR;
std::string rom_path, sav_path;

struct Palette { const char* name; Color rgb[4]; };
const Palette PALETTES[] = {
    {"DMG green", {0xE0F8D0, 0x88C070, 0x346856, 0x081820}},
    {"Pocket",    {0xC4CFA1, 0x8B956D, 0x4D533C, 0x1F1F1F}},
    {"Grey",      {0xFFFFFF, 0xAAAAAA, 0x555555, 0x000000}},
    {"Amber",     {0xFFE9B0, 0xE0A040, 0x8A4A10, 0x2A1004}},
};
constexpr int NUM_PALETTES = array_count(PALETTES);
const char* const PALETTE_NAMES[] = {"DMG green", "Pocket", "Grey", "Amber"};
int palette_idx = 0;
int volume_setting = DEFAULT_VOLUME;

Terminal* term = nullptr;
hal::AudioOut* audio = nullptr;
ui::Context* ui_ctx = nullptr;
IndexedBlitter video;

void set_palette(int idx) {
    Color pal[256];   // shades are 0..3; the rest repeat them, like the old "& 3"
    for (int i = 0; i < 256; ++i) pal[i] = PALETTES[idx].rgb[i & 3];
    video.set_palette(pal, 256);
}

void show_message(const char* title, const char* l1, const char* l2 = nullptr, bool error = true) {
    ui::Context& ui = *ui_ctx;
    ui.kbd.flush();
    std::vector<std::string> lines = {l1};
    if (l2) lines.push_back(l2);
    ui::message(ui, title, lines, error ? ui.theme().error : kDefaultColor);
}

// ------------------------------------------------------------------ saves
void load_save() {
    const gb::CartInfo& ci = gbx.info();
    memset(cart_ram, 0xFF, sizeof cart_ram);
    if (!ci.battery || ci.ram_size == 0) return;
    fs::File f;
    if (!f.open(sav_path, fs::Mode::Read)) return;
    f.read(cart_ram, ci.ram_size);
}

bool write_save() {
    const gb::CartInfo& ci = gbx.info();
    if (!ci.battery || ci.ram_size == 0) return true;
    fs::File f;
    if (!f.open(sav_path, fs::Mode::Write)) return false;
    const int put = f.write(cart_ram, ci.ram_size);
    f.close();
    gbx.clear_ram_dirty();
    return put == int(ci.ram_size);
}

// ------------------------------------------------------------------ loading
bool load_rom(const std::string& p) {
    ui::Context& ui = *ui_ctx;
    const std::string name = path::base_name(p);
    ui::progress(ui, "Loading", name, 0, 1);
    uint32_t last = 0;
    auto progress = [&](uint32_t done, uint32_t total) {
        if (done == total || done - last >= 32768) {
            last = done;
            ui::progress(ui, "Loading", name, done, total);
        }
        return true;
    };
    uint32_t size = 0;
    const char* err = nullptr;
    const uint8_t* rom = flash_cache::load(p.c_str(), &size, progress, &err);
    if (!rom) {
        show_message("Cannot load ROM", err ? err : "Unknown error");
        return false;
    }
    if (size < 0x8000) {
        show_message("Cannot load ROM", "File too small to be a ROM");
        return false;
    }
    switch (gbx.load(rom, size, cart_ram, sizeof cart_ram)) {
        case gb::LoadResult::OK: break;
        case gb::LoadResult::CGB_ONLY:
            show_message("Game Boy Color only", "This game needs a GBC.", "PodBoy emulates the original DMG.");
            return false;
        case gb::LoadResult::UNSUPPORTED_MBC: {
            char m[40];
            snprintf(m, sizeof m, "Cartridge type 0x%02X", rom[0x147]);
            show_message("Unsupported cartridge", m);
            return false;
        }
        default:
            show_message("Cannot load ROM", "Not a valid Game Boy ROM");
            return false;
    }
    rom_path = p;
    sav_path = path::with_extension(p, ".sav");
    load_save();
    gbx.reset();   // cart RAM contents are preserved; reset clears only the console
    return true;
}

// ------------------------------------------------------------------ game screen
void draw_game_chrome() {
    const gfx::Font& f = gfx::font(gfx::FONT_6x12);
    gfx::fill(0, 0, gfx::width(), GAME_Y, 0x000000);
    gfx::fill(0, GAME_Y + GAME_H, gfx::width(), gfx::height() - GAME_Y - GAME_H, 0x000000);
    const char* title = gbx.info().title[0] ? gbx.info().title : "PodBoy";
    gfx::text(4, 2, 0, f, title, 0x606870, 0x000000);
    gfx::text(gfx::width() - 4 - 9 * f.w, 2, 0, f, "Esc: menu", 0x606870, 0x000000);
}

enum class PauseAction { RESUME, RESET, CHANGE_ROM };

// Lists the current key bindings (taken from config.h, so it stays accurate if you remap)
void show_controls() {
    auto two = [](uint8_t a, uint8_t b) { return std::string(key::name(a)) + " or " + key::name(b); };
    ui_ctx->kbd.flush();
    ui::message(*ui_ctx, "Controls",
                {"D-pad     Arrow keys", "A         " + two(BIND_A1, BIND_A2), "B         " + two(BIND_B1, BIND_B2),
                 std::string("Start     ") + key::name(BIND_START), "Select    " + two(BIND_SELECT1, BIND_SELECT2),
                 "Menu      " + two(BIND_PAUSE1, BIND_PAUSE2)});
}

// bg: the game frame to restore behind the menu after a larger dialog was shown
PauseAction pause_menu(const uint8_t* bg) {
    const bool saved_ok = write_save();
    const char* footer = !gbx.info().battery ? nullptr : saved_ok ? "Game saved" : "Save FAILED";
    ui::MenuItem items[] = {
        ui::action("Resume", "Esc"),
        ui::action("Reset game"),
        ui::action("Load another ROM"),
        ui::action("Controls"),
        ui::choice("Palette", &palette_idx, PALETTE_NAMES, NUM_PALETTES),
        ui::range("Volume", &volume_setting, 0, 100, 10, "%d%%"),
    };
    ui::MenuOptions opt;
    opt.footer = footer;
    opt.on_change = [](int) { if (audio) audio->set_volume(volume_setting); };
    ui_ctx->kbd.flush();
    for (;;) {
        switch (ui::menu(*ui_ctx, "PAUSED", items, opt)) {
            case -1: case 0: return PauseAction::RESUME;
            case 1: return PauseAction::RESET;
            case 2: return PauseAction::CHANGE_ROM;
            case 3:
                show_controls();
                if (bg) {   // repaint the game image, then the menu reopens on top of it
                    video.invalidate();
                    video.submit(bg);
                    video.wait();
                }
                opt.initial = 3;
                break;
            default: break;
        }
    }
}

uint8_t map_key(uint8_t key) {
    switch (key::lower(key)) {
        case key::Up: return gb::BTN_UP;
        case key::Down: return gb::BTN_DOWN;
        case key::Left: return gb::BTN_LEFT;
        case key::Right: return gb::BTN_RIGHT;
        case BIND_A1: case BIND_A2: return gb::BTN_A;
        case BIND_B1: case BIND_B2: return gb::BTN_B;
        case BIND_START: return gb::BTN_START;
        case BIND_SELECT1: case BIND_SELECT2: return gb::BTN_SELECT;
        default: return 0;
    }
}

// "60/11 fps" in the title bar: emulated frames and screen updates per second
void draw_stats(uint32_t emu, uint32_t shown, bool audio_ok) {
    const gfx::Font& f = gfx::font(gfx::FONT_6x12);
    char s[40];
    snprintf(s, sizeof s, "%u/%u fps%s", unsigned(emu), unsigned(shown), audio_ok ? "" : " no snd");
    gfx::text(112, 2, 24 * f.w, f, s, 0x606870, 0x000000);
}

// Runs the loaded game until the user chooses "Load another ROM".
void run_game() {
    constexpr int AUDIO_TARGET = 1100;   // frames buffered ahead (~34 ms)
    constexpr int64_t FRAME_US = 16742;  // 70224 cycles at 4.194304 MHz
    input::Keyboard& kbd = term->keyboard();
    int w = 0, last_shown = -1;
    uint8_t buttons = 0;
    uint8_t latch[8] = {0};
    bool save_pending = false;
    int quiet_frames = 0;
    bool audio_ok = audio != nullptr;
    uint64_t next_frame = sys::now_us();
    uint32_t next_stats = sys::now_ms() + 1000;
    uint32_t emu_frames = 0, shown_frames = 0, stats_emu = 0, stats_shown = 0;

    set_palette(palette_idx);
    memset(framebuf, 0, sizeof framebuf);
    gfx::clear(0x000000);
    draw_game_chrome();
    kbd.flush();
    if (audio) audio->silence();

    for (;;) {
        // ---- input
        bool pause = false;
        hal::RawKey ev;
        while (kbd.poll_raw(ev)) {
            if (ev.state == hal::KEY_PRESSED && (ev.code == BIND_PAUSE1 || ev.code == BIND_PAUSE2)) {
                pause = true;
                continue;
            }
            const uint8_t b = map_key(ev.code);
            if (!b) continue;
            if (ev.state == hal::KEY_RELEASED) {
                buttons &= uint8_t(~b);
            } else {
                buttons |= b;
                // A quick tap arrives as press+release together: keep it visible to the
                // game for a few frames, since games only read the buttons once per frame.
                for (int i = 0; i < 8; i++)
                    if (b & (1 << i)) latch[i] = 4;
            }
        }
        if (pause) {
            video.wait();
            if (audio) audio->silence();
            const PauseAction a = pause_menu(last_shown >= 0 ? framebuf[last_shown] : nullptr);
            save_pending = false;
            if (a == PauseAction::CHANGE_ROM) {
                if (audio) audio->silence();
                return;
            }
            if (a == PauseAction::RESET) {
                gbx.reset();
                memset(framebuf, 0, sizeof framebuf);
            }
            set_palette(palette_idx);
            draw_game_chrome();
            buttons = 0;
            kbd.flush();
            if (audio) audio->silence();
            memset(latch, 0, sizeof latch);
            video.invalidate();
            if (last_shown >= 0) video.submit(framebuf[last_shown]);
            next_frame = sys::now_us();
            continue;
        }
        uint8_t effective = buttons;
        for (int i = 0; i < 8; i++)
            if (latch[i]) {
                effective |= uint8_t(1 << i);
                latch[i]--;
            }
        gbx.set_buttons(effective);

        // ---- emulate one frame
        gbx.set_framebuffer(framebuf[w]);
        gbx.run_frame();
        emu_frames++;
        if (!video.busy()) {
            // Between two screen frames the LCD is free: refresh the status readout
            if (sys::reached(next_stats)) {
                draw_stats(emu_frames - stats_emu, shown_frames - stats_shown, audio_ok);
                stats_emu = emu_frames;
                stats_shown = shown_frames;
                next_stats = sys::now_ms() + 1000;
            }
            video.submit(framebuf[w]);
            last_shown = w;
            w ^= 1;
            shown_frames++;
        }

        // ---- audio + pacing
        // Normally the audio clock sets the speed. If the audio DMA ever stops
        // advancing, fall back to a 59.73 Hz timer instead of waiting forever.
        if (audio) audio->push(gbx.apu().samples(), gbx.apu().sample_count());
        gbx.apu().clear_samples();
        if (audio_ok) {
            const uint32_t give_up = sys::now_ms() + 100;
            while (audio->buffered() > AUDIO_TARGET) {
                video.poll();
                if (sys::reached(give_up)) {
                    audio_ok = false;
                    break;
                }
            }
            next_frame = sys::now_us() + FRAME_US;
        } else {
            while (sys::now_us() < next_frame) video.poll();
            next_frame += FRAME_US;
            if (sys::now_us() > next_frame + 100000) next_frame = sys::now_us();
        }

        // ---- autosave a few seconds after the game stops writing cart RAM
        if (gbx.ram_dirty()) {
            gbx.clear_ram_dirty();
            save_pending = true;
            quiet_frames = 0;
        } else if (save_pending && ++quiet_frames >= AUTOSAVE_FRAMES) {
            write_save();
            save_pending = false;
        }
    }
}

}  // namespace

int main() {
    TerminalConfig cfg;
    cfg.clock_khz = SYS_CLOCK_KHZ;
    cfg.keyboard_on_core1 = true;   // keyboard reads never stall the emulator
    term = &terminal_init(cfg);
    gfx::init(term->display());
    static ui::Context ui(term->keyboard(), ui::THEME_NIGHT);
    ui_ctx = &ui;

    audio = term->audio();
    if (audio) {
        audio->start(gb::AUDIO_RATE);
        audio->set_volume(volume_setting);
    }
    if (!video.begin(term->display(), {gb::SCREEN_W, gb::SCREEN_H, 2, 0, GAME_Y})) {
        show_message("PodBoy", "Not enough memory for video buffers");
    }
    flash_cache::init(ROMCACHE_MAX_BYTES);

    app::splash(ui.theme(), "PodBoy", "Game Boy emulator", true);   // bars: red, green, blue, white
    sys::sleep_ms(1500);

    app::mount_storage(ui);
    if (!fs::is_dir(ROM_DIR)) cur_dir = "/";

    std::string select;
    for (;;) {
        std::string file;
        ui::BrowseOptions opt;
        opt.filter = ui::extension_filter(".gb,.gbc");
        opt.select = select;
        opt.empty_hint = "No .gb files here";
        opt.allow_cancel = false;
        ui.kbd.flush();
        if (!ui::browse(ui, "PodBoy", cur_dir, file, opt)) {
            fs::unmount();
            app::mount_storage(ui);
            continue;
        }
        select = path::base_name(file);
        if (!load_rom(file)) continue;
        run_game();
    }
}
