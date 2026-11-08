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
| Windows bar | `src/plat_winbar.c` | built only on `_WIN32`; the standard bar from `ob_host`; a port may replace items or the whole bar |
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

## Rules

- No port includes `<SDL.h>` except through its own non-Overbar needs (PulseMan core stays untouched).
- Windows-only code (`windows.h`, the bar, WinSock) lives in Overbar behind `_WIN32`; ports have none.
- Desktop OpenGL is Overbar's option (`OB_PRESENT_GL`) for the PC ports' shaders; handhelds never use it.
- Library tests build without SDL (hooks NULL, a dummy backend that records calls).

## Order of work

1. Layer + tests + winbar in Overbar (this repo, `main`).
2. SNES port: delete its window/input/audio glue, implement hooks.
3. Genesis port: the `native/pulseman/sdl2.c` copy becomes a thin adapter onto the layer.
4. 95 PC and 96 PC (shared code), then 94 PC.
5. aarch64 builds and device checks after each.

## Open points

- PC ports pace the game by audio and the timer ISR; the layer must expose the device clock and let the port
  choose push or pull.
- 94 PC uses x86 emulation timing; 95/96 run native code. The hooks must not assume a frame-locked loop.
- Genesis keeps PulseMan's own rollback and sound path for now; the adapter forwards its audio unchanged.
