# AGENTS.md: aurum-pods

Instructions for an AI agent that adds features, fixes bugs or writes
documentation **in this repository**. Read this file and the README of the
part you are changing (`pods/podboy/README.md`, or the PicoPods Web section of
`README.md`) before touching code.

## What this repository is

Two things of the Pod-ium family for the ClockworkPi **PicoCalc** (Raspberry
Pi **Pico 2 W**):

| Part | Folder | What it is |
|---|---|---|
| **PodBoy** 1.2 | `pods/podboy` | Game Boy (DMG) emulator. **Needs an RP2350** (276 KB video buffer) |
| **PicoPods Web** | `web/`, `tools/build_web.sh`, `.github/workflows/pages.yml` | The whole PicoCalc system in a browser: **every** repository's pods built with Emscripten, a touch PicoCalc keyboard, sound, an IndexedDB SD card, pods loadable like cartridges |

Built on the **PicoPods libraries** of
[hydrogenium-pods](https://github.com/Pod-ium/hydrogenium-pods)
(`picopods_import.cmake`: `PICOPODS_PATH`, otherwise a git fetch of its
`main`). Siblings: helium-pods (games), carbo-pods (productivity). The web
site depends on **all three** siblings.

## Layout

```
pods/podboy/src/      main.cpp (ROM browser, pause menu, main loop), config.h (keys, tunables),
                      gb/ (emulator core: cpu.cpp, ppu.cpp, apu.cpp, gb.cpp, gb.h)
pods/podboy/tools/host_test/main.cpp
tests/                CMakeLists.txt (scripted session)
web/                  index.html, app.js (launcher, pod runner, IndexedDB library), keyboard.js (touch keyboard)
tools/                gate.sh, package_release.sh, build_web_pods.sh (this repo's pods),
                      build_web.sh (the whole site), ci_ref.sh, ci_apt.sh
.github/workflows/    ci.yml, release.yml, pages.yml (GitHub Pages)
picopods_import.cmake
```

## What belongs here, and what doesn't

- PodBoy: the emulator core (`src/gb/`), its front end and controls.
- PicoPods Web: the page, the launcher, the touch keyboard, how the site is
  assembled, the Pages workflow.
- **Not here**: the browser terminal, Asyncify yields in `pd::sys`, the SD
  card backend, `IndexedBlitter`, `pd_flash_cache`, audio drivers. Those are
  in **hydrogenium-pods**. How a sibling repository builds its own pods for
  the web is its `tools/build_web_pods.sh`, not this repository's.

## Rules for PodBoy

- **RP2350 only**; built for `pico2_w` and `pico2`.
- **Timing is paced by audio**: PWM at 32768 Hz, emulation regulated on the
  queued samples. Don't add other pacing.
- Core 0 emulates; the keyboard is read on **core 1** (`keyboard_on_core1`).
  Video: 160×144 scaled 2× and sent by DMA one line at a time through
  `IndexedBlitter`, only changed lines. Don't add a second framebuffer.
- ROMs are copied to flash with `pd_flash_cache` and read memory-mapped;
  never write flash any other way.
- Emulator core changes: keep CPU timing (cycle counts) exact; test against
  the behaviour the README documents (MBC1/2/3 with RTC/5, DMG only,
  post-boot state, `.sav` for battery cartridges). Game Boy Color-only ROMs
  are refused with a message.
- Key bindings live in `src/config.h`; the pause menu's **Controls** screen
  reads them. Update the README's controls table with them.
- Plain C++17 outside what the libraries allow; must build for host and web.

### Compatibility
- `.sav` files next to the ROM must keep loading. `/roms` is the default
  browser folder (falls back to the root).

## Rules for PicoPods Web

- Plain HTML/CSS/JS, no build step and no framework; it must work on a phone
  (touch keyboard) and offline once loaded.
- One pod per page load (switching reloads the page via `#pod-<file>` /
  `#lib-<id>`), so every pod starts clean. Keep that model.
- Storage: the SD card and the loaded-pods library are separate IndexedDB
  databases (`picopods-library` for pods). Don't change database names or
  store schemas without a migration (users' SD contents live there).
- The site is assembled from each repository's `pods.json` + `<pod>.pod.js`
  (`tools/build_web.sh`); carbo/helium paths are optional, hydrogenium is
  required.
- `BLURBS` in `web/app.js` is keyed by the **pod name** that
  `build_web_pods.sh` emits. **Known bug:** the PodCraft entry is keyed
  `picocraft`, but helium-pods builds `podcraft`, so its description does
  not show; fix it to `podcraft`.
- Wi-Fi, Git and Bluetooth can't work in a browser; pods report "not
  available". Don't try to emulate sockets.

## Build and gates

Toolchain (as CI): Pico SDK **2.1.1**, Arm GNU Toolchain **13.2.Rel1**,
CMake ≥ 3.13, Ninja, C++17 compiler, Python 3, `libmbedtls-dev`,
`libsdl2-dev`; Emscripten **3.1.74**.

| Gate | Command | Must |
|---|---|---|
| G-host | `PICO_SDK_PATH=… tools/gate.sh host` | PC build, `podboy_script` passes |
| G-fw | `PICO_SDK_PATH=… tools/gate.sh fw pico2_w pico2` | `podboy.uf2` links on both RP2350 boards |
| G-web (pods) | `PICO_SDK_PATH=… tools/build_web_pods.sh` | `podboy.pod.js` builds |
| G-web (site) | `PICO_SDK_PATH=… PICOPODS_PATH=… CARBO_PODS_PATH=… HELIUM_PODS_PATH=… tools/build_web.sh` | the full site in `dist/web` |

For any `web/` change, also **load the site**: `python3 -m http.server -d dist/web 8000`
and open it in headless Chromium (Playwright). Check: the pod list appears
with titles and descriptions, a pod starts and draws, the keyboard sends
keys, no console errors; at a phone viewport too.

If you also changed hydrogenium-pods, set `PICOPODS_PATH` to that checkout.
Logs: `build-gate/*.log`.

Run PodBoy on the PC:
`PD_SDCARD=./sd PD_FRAMES=./frames PD_KEYS="down,enter,wait:3000,shot" ./build-gate/host/pods/podboy/podboy`
(window with SDL2 when `PD_KEYS` is unset). Never commit commercial ROMs;
use homebrew or test ROMs that permit redistribution, or generate them.

## Tests

- Scripted sessions in `tests/CMakeLists.txt` (`PD_SDCARD`, `PD_KEYS`,
  `PD_FRAMES`). Key tokens: `enter esc bksp space up down left right
  f1…f12 ctrl+x shift+x`, single characters, `text:<s>`, `wait:<ms>`, `shot`.
- `pods/podboy/tools/host_test/main.cpp` is the emulator core's own PC
  harness (`host_test <rom.gb> [frames] [out.pgm]`; not built by CMake:
  compile it with the `src/gb/*.cpp` sources). It prints what a ROM sends on
  the serial port, which is how Blargg's CPU test ROMs report
  "Passed"/"Failed": use it for core (CPU/PPU/APU/MBC) regression checks.
- Feature ⇒ a test that exercises it. Bug fix ⇒ a test that failed before.
  Assert on deterministic results (`.sav` contents, emulator state, output),
  not on pixel-exact frames.

## Documentation

- **`pods/podboy/README.md` is written in Italian**: keep it in Italian when
  editing it (same structure: Novità, Installazione rapida, Comandi,
  Compilazione, Come funziona, Limiti noti). Everything else is English.
- Root README: parts table, build, CI/releases, the PicoPods Web section
  (how to build and host it, the `PICOPODS_PAGES` / `*_REF` variables),
  layout. Its Build / CI / Layout sections are parallel to the siblings'.
- The README lists the pods the site shows: keep it in sync with the
  sibling repositories (the game is **PodCraft**, not *PicoCraft*).
- **CHANGELOG.md**: add to `## Unreleased` under `### Pods` (PodBoy) or
  `### PicoPods Web` (or `### Repository` for build/CI); say what the user
  sees.
- Versions: `VERSION` in `picopods_add_pod(...)` and the README heading
  together, only when asked or for a release. Release assets:
  `podboy_<v>.uf2`, `picopods-web_<v>.zip`, `SHA256SUMS_<v>.txt`.
- Style: British spelling in English docs, terse, every command verified.
  Old name *PicoBoy* only as provenance.

## Git

- Work on the branch you are given; push to `main` only when explicitly told
  to. Open a pull request only when asked. Pushing to `main` publishes the
  site when `PICOPODS_PAGES` is `true`.
- Changes that need a sibling repository use **the same branch name** there;
  CI's `tools/ci_ref.sh` builds against it. Library changes land in
  hydrogenium-pods `main` first.
- Commit subject `PodBoy: …`, `PicoPods Web: …`, `CI: …` or `Docs: …`,
  wrapped body with what and why and the test name. Add the attribution lines
  your harness requires. No model identifiers in commits, code or docs.
- Never skip, disable or loosen a test or CI step to get green.

## Done means

- [ ] Change is in the right repository.
- [ ] Test added or extended (or, for `web/`, the site checked in a browser).
- [ ] G-host, G-fw (`pico2_w`, `pico2`), G-web pass; full site built when
      `web/` or `build_web.sh` changed.
- [ ] README(s) and CHANGELOG updated and verified.
- [ ] Report: what changed (`file:line`), gates and results, what needs a
      check on a real PicoCalc (frame rate, audio, flash cache) or a real phone.
