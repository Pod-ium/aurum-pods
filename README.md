# aurum-pods

[![CI](https://github.com/Pod-ium/aurum-pods/actions/workflows/ci.yml/badge.svg)](https://github.com/Pod-ium/aurum-pods/actions/workflows/ci.yml)

**Emulators** for the **ClockworkPi PicoCalc** with a Raspberry Pi **Pico 2 W**,
built on the PicoPods libraries, and **PicoPods Web**: the whole PicoCalc
system in a browser.

| Pod | Folder | What it is |
|---|---|---|
| **PodBoy** 1.2 | [`pods/podboy`](pods/podboy) | Game Boy (DMG) emulator. Needs an RP2350 |
| **PicoPods Web** | [`web/`](web) | Every pod (PodBoy, Pod Writer, Pod Files, PicoCraft, the demo) in a browser page with a touch PicoCalc keyboard |

Part of the Pod-ium family of PicoCalc firmwares: [hydrogenium-pods](https://github.com/Pod-ium/hydrogenium-pods)
(libraries and examples), [helium-pods](https://github.com/Pod-ium/helium-pods) (games),
[carbo-pods](https://github.com/Pod-ium/carbo-pods) (productivity),
[aurum-pods](https://github.com/Pod-ium/aurum-pods) (emulators, PicoPods Web).

## Build

The libraries come from [hydrogenium-pods](https://github.com/Pod-ium/hydrogenium-pods):
`picopods_import.cmake` uses `-DPICOPODS_PATH=/path/to/hydrogenium-pods` (or
the environment variable) when given, otherwise CMake fetches them with git
into the build tree (`-DPICOPODS_FETCH_FROM_GIT_TAG=`, default `main`).

```sh
# Firmware for PicoCalc + Pico 2 W (default)
cmake -B build -DPICO_SDK_PATH=/path/to/pico-sdk
cmake --build build -j
# -> build/pods/podboy/podboy.uf2

# Other boards
cmake -B build-pico2 -DPD_BOARD=pico2       # PodBoy needs an RP2350 (pico2, pico2_w)

# PC build: simulator + the pods' scripted sessions
cmake -B build-host -DPD_TERMINAL=host -DPICO_SDK_PATH=/path/to/pico-sdk
cmake --build build-host -j && ctest --test-dir build-host
# ...and with SDL2 installed, the pod runs in a window (keyboard, sound):
PD_SDCARD=./sd ./build-host/pods/podboy/podboy

# The pods for the browser (this repository's pods only; the whole site is below)
PICO_SDK_PATH=/path/to/pico-sdk tools/build_web_pods.sh   # -> dist/web-pods

# Both merge gates in one go
PICO_SDK_PATH=/path/to/pico-sdk tools/gate.sh all
```

Needs Pico SDK 2.1.x, `arm-none-eabi-gcc` and CMake ≥ 3.13; the host build
needs a C++17 compiler, Python 3 and `libmbedtls-dev` (and `libsdl2-dev` for
the window). Flash a `.uf2` in BOOTSEL mode, or copy it to `pico2-apps/` of
the SD card and start it from [UF2 Loader](https://github.com/pelrun/uf2loader).

## CI and releases

- **Pull requests and pushes** (`.github/workflows/ci.yml`): G-host (PC build
  + the PodBoy session), G-fw (`pico2_w`, `pico2`: PodBoy needs an RP2350; `.uf2` files kept as artifacts
  `aurum-<board>`) and G-web (the whole site with every repository's pods, artifact `picopods-web`).
- **Tags and releases** (`.github/workflows/release.yml`): pushing a tag
  `v*` creates its GitHub release, and publishing a release from the UI
  works too; either way `podboy_<version>.uf2`, `picopods-web_<version>.zip` (the site) and `SHA256SUMS_<version>.txt` are attached
  (tag `v1.2.0` gives version `1.2.0`). Locally:
  `PICO_SDK_PATH=… tools/package_release.sh <version>`. It can also be run
  by hand from the Actions tab for an existing tag.
- **Which libraries.** CI and releases build against hydrogenium-pods
  `main`, or the ref in the repository variable `PICOPODS_REF` (set it to a
  hydrogenium-pods tag to make releases reproducible). On a branch, CI uses
  the hydrogenium-pods branch of the same name when it exists, so a change
  across both repositories is tested together.

## PicoPods Web

`tools/build_web.sh` builds every pod with Emscripten into one page plus one
`<pod>.pod.js` file per pod (`dist/web`). Each repository builds its own pods
(`tools/build_web_pods.sh`), so the script needs their checkouts:

```sh
source /path/to/emsdk/emsdk_env.sh        # Emscripten 3.1.74
PICO_SDK_PATH=/path/to/pico-sdk PICOPODS_PATH=/path/to/hydrogenium-pods \
CARBO_PODS_PATH=/path/to/carbo-pods HELIUM_PODS_PATH=/path/to/helium-pods \
    tools/build_web.sh                    # -> dist/web
python3 -m http.server -d dist/web 8000
```

(`CARBO_PODS_PATH` and `HELIUM_PODS_PATH` are optional: without them those
pods are left out.) Open it on a phone: the PicoCalc screen with a touch
PicoCalc keyboard under it, sound, and an SD card kept in the browser (add
ROMs and files from its **SD** panel). Pod files you build yourself (any
repository's `*-web-pods` CI artifact) can be loaded from the page like
cartridges and stay in its list.

- Take the `picopods-web` artifact of any CI run, or the zip of a release.
- To have it online at `https://pod-ium.github.io/aurum-pods/`, enable GitHub
  Pages with source "GitHub Actions" and set the repository variable
  `PICOPODS_PAGES` to `true`: `.github/workflows/pages.yml` then publishes
  it on every push to `main`, every Monday (to pick up the other
  repositories' changes) and when run by hand. The other repositories are
  taken at `main`, or at the refs in the variables `PICOPODS_REF`,
  `CARBO_PODS_REF` and `HELIUM_PODS_REF`.
- Wi-Fi, Git and Bluetooth do not work there (a web page cannot open
  sockets). Like the PC simulator, it runs the pods' code, not the hardware
  drivers. Details: [docs/PORTING.md of hydrogenium-pods](https://github.com/Pod-ium/hydrogenium-pods/blob/main/docs/PORTING.md#in-the-browser).

## Layout

```
pods/                   podboy/
tests/                  the pods' scripted host sessions (ctest)
tools/                  gate.sh (merge gates), package_release.sh (release .uf2),
                        build_web_pods.sh (.pod.js), ci_ref.sh (CI: sibling repository ref)
                        build_web.sh (PicoPods Web, every repository's pods)
picopods_import.cmake   finds the PicoPods libraries (hydrogenium-pods)
web/                    PicoPods Web: index.html, app.js, keyboard.js
```

See [`CHANGELOG.md`](CHANGELOG.md) for what changed.
