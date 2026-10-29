// PodBoy host test harness: runs the emulator core on a PC.
//   host_test <rom.gb> [frames] [out.pgm]
// Prints anything the ROM sends over the serial port (Blargg tests report
// "Passed"/"Failed" this way) and optionally dumps the final screen as a PGM image.
#include "../../src/gb/gb.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>
#include <string>
#include <chrono>

static std::string serial_out;
static void on_serial(uint8_t b, void*) { serial_out += (char)b; putchar(b); fflush(stdout); }

int main(int argc, char** argv) {
    if (argc < 2) { fprintf(stderr, "usage: %s rom.gb [frames] [out.pgm]\n", argv[0]); return 1; }
    FILE* f = fopen(argv[1], "rb");
    if (!f) { perror("open"); return 1; }
    std::vector<uint8_t> rom;
    uint8_t buf[4096]; size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) rom.insert(rom.end(), buf, buf + n);
    fclose(f);
    int frames = argc > 2 ? atoi(argv[2]) : 3000;

    static gb::GameBoy gbx;
    static uint8_t cram[131072];
    static uint8_t fb[160 * 144];
    gbx.set_framebuffer(fb);
    gbx.serial_cb = on_serial;
    gb::LoadResult r = gbx.load(rom.data(), (uint32_t)rom.size(), cram, sizeof cram);
    if (r != gb::LoadResult::OK) { fprintf(stderr, "load failed (%d)\n", (int)r); return 1; }
    printf("[title: %s  mbc: %d  ram: %u]\n", gbx.info().title, gbx.info().mbc, gbx.info().ram_size);

    long total_samples = 0;
    auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < frames; i++) {
        gbx.run_frame();
        total_samples += gbx.apu().sample_count();
        gbx.apu().clear_samples();
        if (serial_out.find("Passed") != std::string::npos || serial_out.find("Failed") != std::string::npos) break;
    }
    double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    printf("\n[%ld audio samples, %.2fs host time]\n", total_samples, secs);

    if (argc > 3) {
        FILE* o = fopen(argv[3], "wb");
        fprintf(o, "P5 160 144 255\n");
        for (int i = 0; i < 160 * 144; i++) fputc(255 - fb[i] * 85, o);
        fclose(o);
    }
    return serial_out.find("Failed") != std::string::npos ? 2 : 0;
}
