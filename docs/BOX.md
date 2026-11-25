# The box (experimental)

On a game's first launch its retail box opens on screen in 3D: the player takes out the manual and the media, reads the
manual, and installs the disks (PC) or puts the cartridge in and the TV and console come on (Genesis). Then the game
starts, and later launches go straight to it. The Overbar overlay and the menu bar get one entry, "View Manual & Materials", on the Game page/menu. It opens
the box with everything in it (manual, disks or disc, case...), at any time, installed or not. That screen has
"Replay the opening at next start", which plays the whole opening again on the next launch.

## Parts

- Overbar: `include/overbar/box.h`, `src/box.c` (the full-window screen, tweens and timers, 2D, hot spots and keys,
  the page reader, synthesized sounds, `box.ini`, the menu entries) and `include/overbar/r3d.h`, `src/r3d.c` (a small
  software 3D renderer: Lambert / Phong / unlit materials as three.js r150 draws them, mipmaps chosen per pixel with up to
  4 taps along a slanted surface (anisotropic), a pick buffer). It draws at the window's own size (up to 4K) in bands of
  rows on every core, into the full-window screen (`include/overbar/screen.h`), so it is the same on every port and needs no GPU.
- A port: its scene (a `bx_scene`: states, hot spots, captions; NHL 94 PC `port/box_pc94.c` and `port/box_install.c`,
  the Genesis C port `native/box_gen94.c`), its models (`box/<dir>/` beside the program), and a call to `ob_box_init`
  once its host is set up; it holds its game while `ob_screen_up()`.
- The CD box (NHL 95, 96, 97 PC): `include/overbar/box_cd.h`, `src/box_cd.c` (the big box: its flap opens, the
  manual, the technical card, the jewel case that opens with the disc in it, the cards, the lineup foldouts) and
  `src/box_cd_install.c` (the first launch: the CD-ROM tray opens, the disc goes in, and the game's installer runs on the
  monitor screen by screen). A port fills a `bx_cd_game` (its manual size and its install steps) and passes
  `bx_cd_scene(&game)` to `ob_box_init`. The installer screens are the player's own captures in `box-art/<dir>/`.
- Zoom: the mouse wheel and a two-finger pinch zoom the camera in and out (in the page reader they zoom the page). The
  pointer stays visible while the box is up.
- `tools/box/export.js` (`node export.js <mockup> <out>`, or `... pc95,pc96,pc97` for the CD boxes): exports the mockup's three.js scenes to `scene.txt`, `scene.vtx`, `scene.idx` and the drawn
  textures (for NHL 94 PC also the desk the install runs on: `pcDesk`, `pc.tower`, `pc.slot`, `pc.screen`, `pc.disk1`..4).
  Scans in the mockup's `art-private/pc94/hires/` (cut from the full-size layouts, up to 2048 pixels) win over the web-sized ones. For the Genesis case, sleeve, manual and cartridge, scans cut to size in the mockup's `models/art/<key>.jpg` win too, and add the keys `genManualBack` (the manual's back cover), `genCartTop` (the label's strip on the cartridge's top edge) and `genCartBack` (the cartridge's back plate); without those three the parts draw plain. `tools/box/make-art.py`: copies the player's scans into `box-art/<dir>/` by key.

## The art

The box art is the player's own scans, never shipped: `box-art/<dir>/<key>.jpg|png` beside the program (or `OB_BOX_ART`,
or `art=` in `box.ini`). A missing scan draws plain. Keys are in the export's `art-map.txt`.

## Test aids

`OB_BOX=1` shows the box as on a first launch, `2` as reopened from the menu, `0` never. `OB_BOX_DATA` and `OB_BOX_ART`
point at other folders. With `OVERBAR_TEST_KEYS` (Return, arrows, Backspace) a port can be driven through it headless.
