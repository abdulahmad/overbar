# The box (experimental)

On a game's first launch its retail box opens on screen in 3D: the player takes out the manual and the media, reads the
manual, and installs the disks (PC) or puts the cartridge in and the TV and console come on (Genesis). Then the game
starts, and later launches go straight to it. The Overbar menu gets "Open the box (experimental)", "Read the manual" and
"Box at start (experimental)": First launch, Never or Every launch.

## Parts

- Overbar: `include/overbar/box.h`, `src/box.c` (the full-window screen, tweens and timers, 2D, hot spots and keys,
  the page reader, synthesized sounds, `box.ini`, the menu entries) and `include/overbar/r3d.h`, `src/r3d.c` (a small
  software 3D renderer: Lambert / Phong / unlit materials as three.js r150 draws them, mipmaps, a pick buffer). It draws
  into the full-window screen (`overbar/screen.h`), so it is the same on every port and needs no GPU.
- A port: its scene (a `bx_scene`: states, hot spots, captions; NHL 94 PC `port/box_pc94.c` and `port/box_install.c`,
  the Genesis C port `native/box_gen94.c`), its models (`box/<dir>/` beside the program), and a call to `ob_box_init`
  once its host is set up; it holds its game while `ob_screen_up()`.
- `tools/box/export.js`: exports the mockup's three.js scenes to `scene.txt`, `scene.vtx`, `scene.idx` and the drawn
  textures. `tools/box/make-art.py`: copies the player's scans into `box-art/<dir>/` by key.

## The art

The box art is the player's own scans, never shipped: `box-art/<dir>/<key>.jpg|png` beside the program (or `OB_BOX_ART`,
or `art=` in `box.ini`). A missing scan draws plain. Keys are in the export's `art-map.txt`.

## Test aids

`OB_BOX=1` shows the box as on a first launch, `2` as reopened from the menu, `0` never. `OB_BOX_DATA` and `OB_BOX_ART`
point at other folders. With `OVERBAR_TEST_KEYS` (Return, arrows, Backspace) a port can be driven through it headless.
