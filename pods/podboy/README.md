# PodBoy 1.2

Emulatore Game Boy (DMG) in C++ per **ClockworkPi PicoCalc** con **Raspberry Pi Pico 2 W**.

**PodBoy** si chiamava *PicoBoy*: è lo stesso emulatore, rinominato quando è entrato in PicoPods.

**Novità della 1.2:** lo schermo va al doppio della velocità (SPI del display a 50 MHz invece di 25): quando il gioco scorre (camminare in Pokémon, livelli a scorrimento) l'immagine si aggiorna a circa 20 fps invece di 11, molto meno a scatti.

**Novità della 1.1:** il firmware è costruito sulle librerie comuni di PicoPods. Display, tastiera (letta sul core 1), audio PWM, scheda SD, cache delle ROM in flash, menu e file manager vengono dalle librerie condivise ([hydrogenium-pods](https://github.com/Pod-ium/hydrogenium-pods)); in questa cartella restano solo il core dell'emulatore (`src/gb/`), `src/main.cpp` e `src/config.h`. Funziona anche su PC (build host di PicoPods). Serve un RP2350 (Pico 2 / Pico 2 W): il buffer video da 276 KB non entra nella RAM di un Pico originale.

- Scelta della ROM da un file manager sulla scheda SD
- Menu di pausa (Esc): riprendi, reset, **carica un'altra ROM senza riavviare**, schema dei comandi, palette, volume
- Salvataggi `.sav` automatici per le cartucce con batteria
- Audio stereo PWM, 4 canali
- Cartucce supportate: ROM only, MBC1, MBC2, MBC3 (con RTC), MBC5

## Installazione rapida

1. Copia `podboy.uf2` sul Pico 2 W in modalità BOOTSEL, oppure mettilo nella cartella `pico2-apps/` della SD e avvialo da **UF2 Loader**.
2. Crea una cartella `/roms` sulla SD e copiaci i file `.gb` (sottocartelle ammesse). Se `/roms` non esiste, il browser parte dalla radice.
3. La SD deve essere FAT32 o exFAT.

## Comandi

| Game Boy | PicoCalc |
|---|---|
| D-pad | Frecce |
| A | `X` oppure `K` |
| B | `Z` oppure `J` |
| Start | Invio |
| Select | Spazio oppure Backspace |
| Pausa / menu | Esc oppure F1 |

Nel browser: frecce su/giù per scorrere, Invio/destra per aprire, sinistra/Backspace per risalire di cartella.
I tasti si cambiano in `src/config.h`; la voce **Controls** del menu di pausa mostra sempre la mappatura attuale.

## Compilazione

PodBoy è uno dei pod dell'albero PicoPods. Dalla cartella principale del repository:

```sh
cmake -B build -DPICO_SDK_PATH=/percorso/pico-sdk      # PicoCalc + Pico 2 W
cmake --build build -j --target podboy
# -> build/pods/podboy/podboy.uf2
```

Serve il Pico SDK 2.1.x e il toolchain `arm-none-eabi-gcc`. Non serve un linker script personalizzato: UF2 Loader esegue gli UF2 standard. Per il Pico 2 senza Wi-Fi: `-DPD_BOARD=pico2`.

Su PC (`cmake -B build-host -DPD_TERMINAL=host`) l'emulatore gira con tasti da script e salva le schermate:

```sh
PD_SDCARD=./sd PD_FRAMES=./frames PD_KEYS="down,enter,wait:3000,shot" ./build-host/pods/podboy/podboy
```

## Come funziona

- **Le librerie PicoPods** forniscono display, font, menu e file manager (`pd_ui`, `pd_gfx`), tastiera (`pd_input`, lettura sul core 1), audio (`PwmAudio`), SD e FatFs (`pd_fs`), cache in flash (`pd_flash_cache`) e l'invio dei fotogrammi via DMA (`IndexedBlitter`). Vedi `docs/MIGRATION.md` e `lib/README.md` in [hydrogenium-pods](https://github.com/Pod-ium/hydrogenium-pods).
- **Core 0** esegue l'emulazione. Lo schermo 160×144 viene scalato 2× (320×288) e inviato al display via DMA, una riga alla volta da interrupt, così l'emulazione non si ferma durante il trasferimento.
- **Core 1** legge la tastiera. Il controller STM32 della tastiera richiede letture I2C lente e bloccanti.
- **La ROM viene copiata in flash** e letta direttamente da lì, perché le ROM fino a 1–2 MB non entrano nei 520 KB di RAM. La copia avviene solo quando cambi gioco; riaprire lo stesso file è istantaneo. Con UF2 Loader il firmware rileva la partizione dell'app (rimappata dal loader) e resta al suo interno.
- **La velocità è sincronizzata sull'audio**: il PWM gira a 32768 Hz, la frequenza nativa del Game Boy, e l'emulatore si regola in base a quanti campioni sono in coda.

## Limiti noti

- **Frame rate del display circa 20 fps quando tutto lo schermo cambia** (scorrimento): il pannello accetta solo colore a 18 bit via SPI, quindi un fotogramma intero sono 276 KB, 44 ms a 50 MHz. Il gioco gira comunque a velocità piena, a 60 Hz, e a schermo arriva un fotogramma ogni tre; le scene ferme o con pochi sprite in movimento si aggiornano molto più spesso, perché si inviano solo le righe cambiate. Il contatore nella barra in alto (`60/20 fps`) mostra fotogrammi emulati / mostrati.
- **Solo Game Boy originale**. I giochi "solo Game Boy Color" vengono rifiutati con un messaggio; quelli compatibili con entrambi girano in modalità DMG.
- L'orologio MBC3 (RTC) avanza solo mentre il gioco è in esecuzione e non viene salvato nel `.sav`.
- Nessun boot ROM Nintendo: l'emulatore parte direttamente dallo stato post-boot.

## Se qualcosa non va

| Sintomo | Cosa provare |
|---|---|
| Nessun audio | `PD_PICOCALC_AUDIO_L=26` in `CMakeLists.txt` (le schede più vecchie usano GP26 invece di GP28) |
| Display con artefatti o colori sbagliati | Riporta `PD_PICOCALC_LCD_HZ` a `25000000` in `CMakeLists.txt` (la 1.2 usa 50 MHz) |
| Instabilità o crash casuali | `SYS_CLOCK_KHZ 150000` in `src/config.h` (niente overclock) |
| Tastiera poco reattiva | `PD_PICOCALC_KBD_GAP_US=2000` in `CMakeLists.txt` |

L'uscita seriale (UART0, 115200 baud) è attiva per il debug.

## Test del core su PC

Il core dell'emulatore (`src/gb/`) non dipende dal Pico e si compila anche su PC:

```sh
g++ -O2 -o host_test tools/host_test/main.cpp src/gb/*.cpp   # da pods/podboy
./host_test rom.gb [frame] [schermata.pgm]
```

Il core è stato verificato così:

- supera tutti i test `cpu_instrs` di Blargg (gli 11 singoli e la ROM combinata MBC1) e `instr_timing`;
- riproduce **dmg-acid2** identico pixel per pixel all'immagine di riferimento.

## Licenze

- Codice PodBoy: tuo.
- FatFs (ChaN): licenza BSD-style, vedi `lib/services/fs/fatfs/LICENSE.txt`.
- Font Terminus (Dimitar Toshkov Zhekov): SIL Open Font License 1.1.
- Driver del display (librerie PicoPods) basato su PicoCalc Writer e sul codice di riferimento ClockworkPi.

Usa ROM ricavate dalle tue cartucce, oppure giochi homebrew.

**Stato:** la 1.1 è stata compilata e provata su PC (supera `cpu_instrs` e riproduce dmg-acid2 nella build host); non è ancora stata provata sull'hardware.
