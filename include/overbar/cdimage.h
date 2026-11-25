/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * Copyright (c) 2026 abdulahmad
 *
 * A CD image the player ripped, read as the disc: its data track's files (ISO 9660) and its audio
 * tracks (Red Book, 16-bit stereo 44.1 kHz). Any common ripping: a .cue sheet (one file per track
 * or one for the disc, BINARY or WAVE files, renamed or moved files found by name or contents), a
 * CloneCD .ccd with its .img, a plain .iso / .bin / .img of the data track (2048 byte sectors or
 * raw 2352 / 2336 / 2448, with or without the 2 second pregap), and numbered loose track files
 * beside it ("Track 02.wav", "02 - Name.bin"). Plain C, no SDL; a port asks for a disc by the
 * files its data track must hold, so any game's disc can be found by contents, not names.
 */
#ifndef OVERBAR_CDIMAGE_H
#define OVERBAR_CDIMAGE_H
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define OB_CD_TRACKS 99
#define OB_CD_RATE 44100

typedef struct ob_cd ob_cd;

/* the first disc under dir (its folders too, three deep) whose data track holds every file in
 * need (root names, any case); NULL when none, with note (when given) saying what was found
 * that could not be read (a .chd, FLAC music ...) or "" */
ob_cd *ob_cd_find(const char *dir, const char *const *need, int nneed, char *note, int note_size);
/* one image: a .cue, .ccd, or a data track file; NULL if it is not a disc with need's files */
ob_cd *ob_cd_open(const char *path, const char *const *need, int nneed);
void ob_cd_close(ob_cd *cd);
const char *ob_cd_path(const ob_cd *cd);           /* what was opened (the sheet, else the data file) */

/* a file of the data track by its path ("GRAPH2.BIN", "DIR/NAME.EXT"; any case): its size,
 * *out malloc'd (the caller frees); -1 not there */
long ob_cd_read_file(ob_cd *cd, const char *name, uint8_t **out);
/* its size, or -1 */
long ob_cd_file_size(const ob_cd *cd, const char *name);
/* the data track's i-th file name (0 ..), NULL past the last */
const char *ob_cd_file_name(const ob_cd *cd, int i);

/* audio track n (2-99): its length in stereo frames at 44.1 kHz, 0 when there is none */
long ob_cd_track_frames(const ob_cd *cd, int n);
/* frames of track n from frame at into out (left, right, native byte order): how many (0 at its end) */
long ob_cd_track_read(ob_cd *cd, int n, long at, int16_t *out, long frames);
int ob_cd_audio_tracks(const ob_cd *cd);           /* how many audio tracks it has */

#ifdef __cplusplus
}
#endif
#endif
