# MiSTer filter files

The files the MiSTer look and the "MiSTer filters" menus read: horizontal and vertical filters, gamma curves, shadow masks and presets.
They come unchanged from the MiSTer FPGA project:

* `Filters/` and `Gamma/`: [Filters_MiSTer](https://github.com/MiSTer-devel/Filters_MiSTer) (the adaptive scanline filters are by Soltan_G42 and Tonurics; the other filters and gamma curves are by the contributors named in each file's header)
* `Shadow_Masks/`: [ShadowMasks_MiSTer](https://github.com/MiSTer-devel/ShadowMasks_MiSTer) (the author of each mask is named in its header, for example TrashUncle for the JVC masks)
* `Presets/`: [Presets_MiSTer](https://github.com/MiSTer-devel/Presets_MiSTer)

## Licence

The MiSTer project ([MiSTer-devel](https://github.com/MiSTer-devel)) is GPL-3.0 licensed, and these repositories state no separate licence.
Please see them for the full lists of contributors and for newer versions. Only the `.txt` and `.ini` data files are included here
(not the scripts of those repositories).

This folder is not under Overbar's MIT licence. The programs only read these files at run time as data; nothing here is compiled or
linked into Overbar or a port, so a port that ships the folder beside its program stays MIT. Keep this README in the folder when
you ship it.

## How a port gets them

Overbar looks for a `mister` folder next to the program, then in the current folder. A port's build copies this folder there:

```make
include native/overbar/overbar.mk
mister: ; $(call overbar_copy_mister,build)        # makes build/mister
```

A shell build script copies `<overbar>/mister` the same way. To use other files, for example from a MiSTer SD card, put them in the
copied folder with the same layout (`Filters`, `Gamma`, `Shadow_Masks`, `Presets`).
