# Overbar platform layer (SDL2)

Status: design, stage 0. Nothing here is built yet.

Anything that uses Overbar goes through one SDL2 platform layer. The port keeps only what is its own game:
its frame, its pad bits, its sound, its options. SDL2 (not SDL3) because the handhelds (muOS, Knulli, ROCKNIX,
stock firmware) ship SDL2.

## What moves into Overbar

Today every port carries its own copy of this (SNES `platform/main.c`: window, `events()`, `draw()`, overlay
key/mouse routing, cursor hiding, text input, FPS, controller open/close and pad setup, settings file):

| Module | File | Owns |
|---|---|---|
| Window and present | `src/plat_window.c` | SDL_Init, window flags (fullscreen on handheld), SDL_Renderer on handhelds, OpenGL only where the port asks, vsync, fullscreen toggle, picture rect from `ob_view`, FPS/stats, the overlay and banner drawn over the frame |
| Input | `src/plat_input.c` | the event pump, keyboard and controller open/close, hotkeys (`` ` ``, F11, F12, Alt+Enter, controller Back), overlay and file-menu routing, cursor hiding, text input, per-slot pad binding and the pad-setup screen, settings file |
| Audio | `src/plat_audio.c` | one SDL audio device, a sample ring the game pushes into (or a pull callback), resampling to the device rate, mute online |
| Desktop menu bar | `src/bar.c` (model) + `bar_win.c`, `bar_mac.m` | the standard bar built once from `ob_host` as a neutral item tree (check marks, radio groups, actions). Windows: native HMENU. macOS: native NSMenu in the screen's menu bar. Linux has no standard native menu bar, so it gets none (the Overbar overlay menu is the way in there, as on handhelds). A port may replace items or the whole tree |
| Loop | `src/plat_run.c` | optional: `ob_plat_run()` owns the main loop and calls the hooks; ports with their own loop (PC ports) call `ob_plat_pump()` and `ob_plat_present()` instead |

## API (one header, `include/overbar/plat.h`)

```c
typedef struct ob_frame {            /* what the game hands over each frame */
    const void *px; int w, h, pitch;
    int fmt;                         /* OB_PX_ARGB8888, OB_PX_RGB565, OB_PX_INDEXED8 */
    const uint32_t *palette;         /* for INDEXED8 (DOS) */
    ob_class cls;                    /* console / VGA shape for ob_view */
} ob_frame;

typedef struct ob_game_hooks {       /* per-game; every hook may be NULL */
    int  (*frame)(ob_frame *out);                    /* current picture */
    void (*pads)(int slot, uint32_t held);           /* normalized buttons for the game's own mapping */
    int  (*map_key)(int scancode, int down);         /* game keys not bound to a pad; return 1 if used */
    void (*audio_fill)(int16_t *lr, int frames);     /* pull mode */
    void (*pause)(int paused);
    void (*quit)(void);
    const ob_pad_layout *pad_layout;                 /* button names for the pad-setup screen */
    const char *title, *settings_name;
} ob_game_hooks;

int  ob_plat_open(const ob_game_hooks *g, ob_host *h);   /* SDL_Init, window, audio, controllers */
void ob_plat_pump(void);        /* events -> hooks and overlay */
void ob_plat_present(void);     /* draw frame + overlay, swap */
void ob_plat_audio_push(const int16_t *lr, int frames);  /* push mode */
int  ob_plat_run(void);         /* pump + frame + present + audio at the game's rate */
void ob_plat_close(void);
```

`ob_shell` (picture, capture, hold, vsync, quit, controllers) stays as the library's seam to the layer;
the layer installs it, so ports no longer do.

## On-screen keyboard (planned, Abdul 2026-11-08)

The PC ports need it for typing league names, team owner names, created players and so on, on handhelds and with a
controller on a desktop. `src/osk.c` (model and drawing, no SDL; `ob_plat_osk_*` in the layer routes the input): a grid drawn over the picture (the same way as the pad setup screen), moved with
the d-pad, A types, B deletes, Start accepts. It does not know the game: it calls a hook `ob_plat_hooks.key(scancode, down)` (USB HID codes, Shift sent around a shifted letter)
that the port turns into the game's own keyboard (the DOS keyboard interrupt for the PC ports). The port tells the layer when
the game is waiting for text (`ob_plat_osk_open()` / `ob_plat_osk_close()`: for the PC ports found from the game's text-entry routines, to be
located in each game's code); a player can also open it by hand from the overlay. The layer's overlay fields (the Online page)
use the same keyboard on a handheld.

## Rules

- No port includes `<SDL.h>` except through its own non-Overbar needs (PulseMan core stays untouched).
- OS-specific code (`windows.h`, Cocoa, WinSock) lives in Overbar behind `_WIN32` / `__APPLE__`; ports have none.
- Desktop OpenGL is Overbar's option (`OB_PRESENT_GL`) for the PC ports' shaders; handhelds never use it.
- Library tests build without SDL (hooks NULL, a dummy backend that records calls).

## Order of work

1. Layer + tests + the bar model and the Windows bar in Overbar (the macOS bar needs a Mac to check; written and compiled-checked first) (this repo, `main`).
2. SNES port: delete its window/input/audio glue, implement hooks.
3. Genesis port: the `native/pulseman/sdl2.c` copy becomes a thin adapter onto the layer.
4. 95 PC and 96 PC (shared code), then 94 PC.
5. aarch64 builds and device checks after each.

## Open points

- PC ports pace the game by audio and the timer ISR; the layer must expose the device clock and let the port
  choose push or pull.
- 94 PC uses x86 emulation timing; 95/96 run native code. The hooks must not assume a frame-locked loop.
- Genesis keeps PulseMan's own rollback and sound path for now; the adapter forwards its audio unchanged.
