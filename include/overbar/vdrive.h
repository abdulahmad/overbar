/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * Copyright (c) 2026 abdulahmad
 *
 * Virtual drives for DOS games: a drive letter (A:, B: ...) that is a folder of the player's, so a
 * game that asks for a disk always finds it there, and nobody is asked to swap. Any DOS title can
 * use this; the online season (overbar/season.h) is one client (each player's copy in a folder).
 * The port's DOS layer asks ob_vdrive_path for each file name the game opens and ob_vdrive_diskfree
 * for the drive's space, and answers the game's disk prompts itself. A name's parts are matched
 * ignoring case, as DOS does. No SDL. */
#ifndef OVERBAR_VDRIVE_H
#define OVERBAR_VDRIVE_H

#ifdef __cplusplus
extern "C" {
#endif

#define OB_VDRIVE_PATH 260

typedef struct ob_vdrive {
    unsigned virt;                      /* bit k: drive 'A' + k is virtual */
    char dir[26][OB_VDRIVE_PATH];       /* its folder ("" no disk in it) */
} ob_vdrive;

void ob_vdrive_init(ob_vdrive *v);
/* drive letter becomes virtual with dir as its disk (made if missing; NULL or "": no disk in it) */
void ob_vdrive_mount(ob_vdrive *v, int letter, const char *dir);
void ob_vdrive_unmount(ob_vdrive *v, int letter);      /* no longer virtual: the port's own again */
/* a DOS name ("A:\\LEAGUE\\PLAYER.ID", "a:player.id") as a path of the player's (out): 1, 0 not on a
 * virtual drive (the port's own), -1 a virtual drive with no disk in it (the game's "drive not ready").
 * Parts that are there are written as they are on disk; the rest as given, in capitals (a file to make) */
int ob_vdrive_path(const ob_vdrive *v, const char *dos, char *out, int size);
/* a virtual drive's space, as DOS's get-disk-free gives it: 0, -1 (not virtual, or no disk). A disk is
 * 32 MB of 512-byte clusters, so an export never runs out of room */
int ob_vdrive_diskfree(const ob_vdrive *v, int letter, unsigned *total_clusters, unsigned *free_clusters,
                       unsigned *sectors_per_cluster, unsigned *bytes_per_sector);

#ifdef __cplusplus
}
#endif

#endif
