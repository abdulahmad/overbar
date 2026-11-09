/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * Copyright (c) 2026 abdulahmad
 *
 * Virtual drives (overbar/vdrive.h). */
#include "overbar/vdrive.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fsdir.h"

#define CLUSTERS 65535u

static int slot(int letter)
{
    if (letter >= 'a' && letter <= 'z') letter -= 32;
    return letter >= 'A' && letter <= 'Z' ? letter - 'A' : -1;
}

void ob_vdrive_init(ob_vdrive *v) { memset(v, 0, sizeof *v); }

void ob_vdrive_mount(ob_vdrive *v, int letter, const char *dir)
{
    int k = slot(letter);
    if (k < 0) return;
    v->virt |= 1u << k;
    snprintf(v->dir[k], sizeof v->dir[k], "%s", dir ? dir : "");
    if (v->dir[k][0]) ob_fs_mkdir(v->dir[k]);
}

void ob_vdrive_unmount(ob_vdrive *v, int letter)
{
    int k = slot(letter);
    if (k < 0) return;
    v->virt &= ~(1u << k);
    v->dir[k][0] = 0;
}

int ob_vdrive_path(const ob_vdrive *v, const char *dos, char *out, int size)
{
    char path[1024], part[OB_FS_NAME], found[OB_FS_NAME];
    const char *p;
    int k, len, missing = 0;
    if (!dos || !dos[0] || dos[1] != ':' || (k = slot(dos[0])) < 0 || !(v->virt >> k & 1)) return 0;
    if (!v->dir[k][0]) return -1;
    snprintf(path, sizeof path, "%s", v->dir[k]);
    for (p = dos + 2; *p;) {
        while (*p == '\\' || *p == '/') p++;
        for (len = 0; p[len] && p[len] != '\\' && p[len] != '/'; len++) {}
        if (!len) break;
        if (len >= OB_FS_NAME) len = OB_FS_NAME - 1;
        memcpy(part, p, (size_t)len);
        part[len] = 0;
        p += len;
        while (*p && *p != '\\' && *p != '/') p++;
        if (!strcmp(part, ".")) continue;
        if (!missing && ob_fs_find(path, part, found, sizeof found)) memcpy(part, found, sizeof part);
        else {
            int i;
            for (i = 0; part[i]; i++) if (part[i] >= 'a' && part[i] <= 'z') part[i] -= 32;
            missing = 1;
        }
        if (strlen(path) + strlen(part) + 2 > sizeof path) return -1;
        strcat(path, "/");
        strcat(path, part);
    }
    if ((int)strlen(path) >= size) return -1;
    memcpy(out, path, strlen(path) + 1);
    return 1;
}

int ob_vdrive_diskfree(const ob_vdrive *v, int letter, unsigned *total_clusters, unsigned *free_clusters,
                       unsigned *sectors_per_cluster, unsigned *bytes_per_sector)
{
    static char list[256][OB_FS_NAME];
    char path[600];
    unsigned long used = 0;
    int k = slot(letter), n, i;
    if (k < 0 || !(v->virt >> k & 1) || !v->dir[k][0]) return -1;
    n = ob_fs_list(v->dir[k], list, 256);
    for (i = 0; i < n; i++) {
        FILE *f;
        ob_fs_join(path, sizeof path, v->dir[k], list[i]);
        if ((f = fopen(path, "rb"))) { if (!fseek(f, 0, SEEK_END)) used += ((unsigned long)ftell(f) + 511) / 512; fclose(f); }
    }
    *total_clusters = CLUSTERS;
    *free_clusters = used >= CLUSTERS ? 0 : CLUSTERS - (unsigned)used;
    *sectors_per_cluster = 1;
    *bytes_per_sector = 512;
    return 0;
}
