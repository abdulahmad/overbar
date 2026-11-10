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
| Input | `src/plat_input.c` | the event pump, keyboard and controller open/close, hotkeys (`` ` ``, F11, F12, F9, Alt+Enter, controller Back), overlay and file-menu routing, cursor hiding, text input, per-slot pad binding and the pad-setup screen, settings file |
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
located in each game's code). Auto-open pops only if the last input was a controller; after a mouse or keyboard it stays
closed. F9 opens or closes it by hand either way. A player can also open it from the overlay. The layer's overlay fields (the Online page)
use the same keyboard on a handheld.

## Select sides, chat and the room (online play phase 2, 2026-11-09)

One select-sides screen for every port (`include/overbar/sides.h`, `src/sides.c`; model and drawing, no SDL), shown when a
game is about to start: each device (the keyboard is device 0, a controller 1 + its slot) starts in the middle, Left / Right
move it to a team, OK says ready, Back takes it back. When every seated device is ready, OK from the deciding machine starts
the game, and `ob_sides_game.apply` writes the seat table into the game's own controller setup. Online the host's model is
the only one that changes: guests send their devices and presses as messages (`OB_NET_TYPE_SIDES`) and get the whole state
back after each change, so every machine applies the same table. `ob_sides_owners` gives ob_net's pad owners for
`ob_net_match_begin`; `ob_sides_hotkeys_filter` clears the keys that act for a team the machine has no seat on (line
changes, goalie), run in the step on every machine.

Chat (`include/overbar/chat.h`): T opens a line, Enter sends, Esc closes; lines show as toasts. Keys are HID codes, so the
on-screen keyboard types into it. The room (`include/overbar/room.h`) ties ob_net to both: `ob_room_poll` routes the
messages and says who joined or left; `ob_room_lines` / `ob_room_draw` is the room page (status, members and pings, seats).
In the layer, `ob_plat_room(sides, chat)` routes the keys and controllers and draws both; `ob_plat_sides_devices` adds the
keyboard and the controllers plugged in (and the layer adds and removes them as they come and go).

## Pause and menus online (online play phase 3, 2026-11-09)

The menu lane (`include/overbar/lane.h`, `src/lane.c`): in a room's match each machine has two more of ob_net's pads
(`OB_LANE_FIRST` + 2 x its number; ob_net has 16 pads since protocol 4, seats use 0 .. 7) and sends its menu input in
them every step: one key, the mouse, a pause request. `ob_lane_owners` adds them to the match's owner table.
`ob_lane_step` runs in the simulation on every machine: the first pause request holds the menu, only the holder's keys and
mouse reach the game until it resumes (its pause request again, or the game's own Resume, `ob_lane_resume`), and others'
requests are dropped. `ob_lane_sim` is part of the game's state. The port's part: where its input layer reads keys and
the mouse (read the lane's instead while paused), when its pause menu opens and closes, and the line editor's fence
(`ob_lane_holder_may` with the holder's teams from the seat table).

## Saves, reconnect and Recover game (online play phase 4, 2026-11-09)

- Shared states (`ob_rollback`'s share / take): a state as bytes another machine of the same build takes, the regions
  whole and the stack, with their addresses. Taken only where every region and the game thread's stack sit at the same
  addresses (`ob_rollback_why` says what differs). Needed for a watcher joining mid-match, a player coming back and Recover.
- `ob_net_match.state`: a match every machine begins from a shared state at `state_step` (Recover game).
  `ob_net_game.hold_seats`: a player who drops does not end the match; it waits (`ob_net_waiting_for`, the status line
  says "waiting for Ben to come back"), and when a machine of that name joins again it gets the same seat and every
  machine goes on from the newest state whose buttons were all known. Ending the match (`ob_net_match_end`) is the
  reconnect screen's other choice.
- `include/overbar/save.h`: the host's saves (each faceoff and every 2 game minutes, the last 3 of each; each period's
  start, all), with a summary for the Recover list (`ob_saves_list`, `ob_save_line`, `ob_save_load`, `ob_save_match`),
  removed once the result is in the league (`ob_saves_done`). The game's part: `ob_saves_event` / `ob_saves_clock` in
  the simulation, `ob_saves_poll` on the host outside it.

## The online season (online play phase 5, 2026-11-09)

- `include/overbar/vdrive.h` (DOS platform): virtual drives for any DOS game. A drive letter is a folder of the
  player's, so a title that asks for a disk always finds it; the port's DOS layer asks `ob_vdrive_path` for each name
  the game opens (parts matched ignoring case) and `ob_vdrive_diskfree` for the drive's space, and answers the game's
  disk prompts itself.
- `include/overbar/files.h`: a folder sent across the room's message lane (`OB_NET_TYPE_FILES`). Only the files the
  receiver lacks are sent, so a folder sent again after a drop sends what had not come; the files go into place together
  once every hash checks; `OB_FILES_MIRROR` makes the target exactly the folder sent.
- `include/overbar/season.h` (`OB_NET_TYPE_SEASON`): shared league or campaign for any game that keeps a master copy
  and a copy per player. "Team" is the game's own unit. Copies stay in step by the port's export, merge, copy and
  import hooks (a DOS port can set a virtual drive on the folder given). A guest's `ob_season_sync`: its teams'
  exports go to the host, which merges them all, then sends a fresh copy of each back to be imported. The first player
  to bring a team owns it. `ob_season_give` is the host making a unit this player's for someone elsewhere (the first
  copy). A match away from the master (C vs D, the host away): the game needs both players' copies and writes the
  result into both; each player's copy goes to the other machines as a mirror (`ob_season_mirror_send`,
  `ob_season_mirror(s, from, ...)`), every machine plays from the same two copies (its own where it has one) and
  drops the mirrors after. Both copies then hold the game as played, so neither player can play it a second time.
  Each player marks it (`ob_season_played`) until a sync merges it; the master hands each game to the merge once per
  team (its ledger keeps game and team merged), so a sync broken off after the merge never counts a game twice.
  Teams for players (2026-11-10): the host can add them all (`ob_season_give`), or a player in the room picks one
  (`ob_season_teams` for who has which, `ob_season_claim`; the port's `add_human` hook is that game's add-a-player,
  with no questions on the host's screen). A team as a file, for a player away from the room: `ob_season_pack` (the
  export and the games played away), `ob_season_take_pack` on the host (merged once: the same file again only gets
  its copy back), `ob_season_import_pack` for the copy back, and `ob_season_pack_copy` for a first copy. Overlay
  **Season** and the window **File** menu (`ob_season_set`) are the screens: Sync league, Pick a team, Export /
  Import / Take a team file, Write a first copy. Files are `.obt` in the folder the client names. `ob_room_modules`
  routes both modules' messages; the room page shows a line about a sync.

## Modem and cable play carried by the room (online play phase 6, 2026-11-09)

- `include/overbar/serial.h` (DOS): an 8250/16550 UART at the register level with a Hayes modem behind it (or a
  null-modem cable), for two machines of a room. The game's own serial driver and modem code run unchanged: AT commands
  answered (E, Q, V, S registers, H, Z, O), dialing reaches the other machine whatever the number, auto-answer or ATA,
  CONNECT at once, `+++` with its guard time, DTR dropped or the other machine leaving: NO CARRIER. Bytes go over the
  room's message lane (`OB_NET_TYPE_SERIAL`), which loses nothing. `ob_room_serial` routes it and tells it when the other
  machine comes and goes. The port: the game's in / out on the COM port to `ob_serial_in` / `ob_serial_out`, the IRQ
  raised on `ob_serial_irq_edge`, `ob_serial_poll` once a frame. In play, a game's own lockstep exchange is replaced by a
  room match (per port; 96: NHL96PC docs/MODEM.md).

## Console features, resync and drop saves (online play phase 7, 2026-11-09)

- `ob_net_game.host_bits`: button bits only the host's pads carry, a control word beside its buttons. The core
  clears them from every other machine's buttons and never holds them in a guess, so a guessed step has none and
  the real one rolls back to it. No protocol change.
- `overbar/console.h` (`ob_console`): on a console the whole machine is the state and one match lasts the whole
  stay. The host's reset (`OB_CONSOLE_RESET`) and its state for everyone (sent over the room, type 245, then
  `OB_CONSOLE_LOAD` on one step once every machine has it) are carried out by every machine after the same step.
  `ob_console_step` also has a "host only in the menus" switch; Abdul chose every pad in the menus, so the
  Genesis port passes 0. `ob_room_console` routes its messages.
- Desync (Abdul: resync from the host's state): in a room whose states can be shared, a desync makes every
  machine go on from the host's newest state whose buttons were all known (a new match, same seats),
  `OB_NET_MAX_RESYNCS` times a match at most; then it ends as before. `ob_net_stats.resyncs`.
- A player who drops (Abdul: end match, and save on disconnect): `ob_saves_poll` writes an `OB_SAVE_DROP` save
  (last 3 kept) as soon as `ob_net_waiting_for` names someone new; a match ended for the drop keeps it for
  Recover game.
- `ob_net_stats_get`, `ob_net_pads_of`. A room can be opened by two "both connect" machines.
- NHL94Genesis-C-Port's netplay is now on ob_net (native/net.c): `make netplay-check` passes all its cases
  (head to head, 2+2, four, 2+1+1, 1+3, delay 0, host reset, host's state for everyone, roster set).

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
