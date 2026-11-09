/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * Copyright (c) 2026 abdulahmad
 *
 * Folders and files for Overbar's modules (inside the library only): making and listing a folder,
 * reading and writing a whole file (written under another name, then renamed into place), a hash. */
#ifndef OVERBAR_FSDIR_H
#define OVERBAR_FSDIR_H

#include <stddef.h>
#include <stdint.h>

#define OB_FS_NAME 64                   /* bytes of a file's name, its 0 included */

int ob_fs_mkdir(const char *path);      /* 0 (made, or there already), -1 */
int ob_fs_is_dir(const char *path);
/* the plain files in dir (no folders, none starting with '.'), sorted by name ignoring case: their count, -1 no folder */
int ob_fs_list(const char *dir, char (*name)[OB_FS_NAME], int max);
/* the name in dir (a file or a folder) that is `name` ignoring case (into out): 1 found, 0 not */
int ob_fs_find(const char *dir, const char *name, char *out, int size);
/* a whole file (malloc'd; free it): 0, -1 */
int ob_fs_read(const char *path, uint8_t **data, size_t *size);
/* writes path through path + "~", renamed into place: 0, -1 */
int ob_fs_write(const char *path, const void *data, size_t size);
/* the files in dir go, then dir itself (folders inside it stay, and so does dir then) */
void ob_fs_clear(const char *dir);
/* a + "/" + b into out (cut to size, always ended) */
void ob_fs_join(char *out, size_t size, const char *a, const char *b);
int ob_fs_stricmp(const char *a, const char *b);
uint64_t ob_fs_hash(const void *data, size_t size);     /* FNV-1a, 64 bits */

#endif
