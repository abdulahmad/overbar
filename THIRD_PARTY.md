# Third-party parts

Overbar itself is PolyForm Noncommercial 1.0.0 ([LICENSE](LICENSE)). These parts are not covered by it. Texts and notices are in [NOTICE](NOTICE) and [licenses/](licenses/).

| Part | Where | License | Notes |
|---|---|---|---|
| PulseMan menu and UDP code | `third_party/pulseman/menu.c`, `third_party/pulseman/udp.c`, `include/mdrt/menu.h` | MIT, Copyright (c) 2026 The pulseman authors | Keep the headers and [licenses/MIT-PulseMan.txt](licenses/MIT-PulseMan.txt) |
| MiSTer filter, gamma, shadow mask and preset files | `mister/` | GPL-3.0 as declared by MiSTer-devel's Distribution_MiSTer | Unchanged data files read at run time; no MiSTer code is in Overbar. Individual authors are listed below. The status of each author's grant is **not confirmed** (the source repos Filters_MiSTer, ShadowMasks_MiSTer and Presets_MiSTer have no license file) |

## MiSTer files by author (from the file headers and the source repos)

| Group | Files | Author evidence | Confidence for commercial use |
|---|---|---|---|
| Shadow masks | 1,144 | Tonurics | confirm with author |
| Shadow masks | 47 | TrashUncle | confirm with author |
| Shadow masks | 10 | Soltan_G42 | confirm with author |
| Adaptive scanline filters | 66 | Soltan_G42, recomputed by Tonurics | confirm with authors |
| Other filters | about 250 | no header; repo history: ghogan42 (Gregory Hogan), 78Gausen, Keith F. Kelly and others | not established |
| Shadow masks `Simple (Monochrome)/RetroTink/*` | 6 | "Mike Chi (Converted by TrashUncle)" | **avoid** until permission |
| Gamma `CRT Simulation.txt` | 1 | "CRT gamma by c0d3h4x0r" | **avoid** until permission |
| Other gamma files | 33 | no header | not established |
| Presets | 44 | Jamie Blanks, Roberto Garcia-Lago, Trash Uncle and others | not established (small config files) |

Policy: no GPL, LGPL, AGPL, noncommercial or unlicensed code or assets are compiled into Overbar. Permissive licenses (MIT, BSD, zlib, Apache-2.0) are fine. Before any commercial release, ship only files whose rights are confirmed.
