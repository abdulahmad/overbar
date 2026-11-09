/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * Copyright (c) 2026 abdulahmad
 *
 * Folders and files (src/fsdir.h). */
#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif
#include "fsdir.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

int ob_fs_stricmp(const char *a, const char *b)
{
    for (;; a++, b++) {
        int x = (unsigned char)*a, y = (unsigned char)*b;
        if (x >= 'a' && x <= 'z') x -= 32;
        if (y >= 'a' && y <= 'z') y -= 32;
        if (x != y || !x) return x - y;
    }
}

void ob_fs_join(char *out, size_t size, const char *a, const char *b)
{
    size_t na = strlen(a), nb = strlen(b);
    if (!size) return;
    if (na > size - 1) na = size - 1;
    memmove(out, a, na);
    if (na + 1 < size) out[na++] = '/';
    if (nb > size - 1 - na) nb = size - 1 - na;
    memcpy(out + na, b, nb);
    out[na + nb] = 0;
}

uint64_t ob_fs_hash(const void *data, size_t size)
{
    const uint8_t *p = (const uint8_t *)data;
    uint64_t h = 0xCBF29CE484222325ull;
    size_t i;
    for (i = 0; i < size; i++) h = (h ^ p[i]) * 0x100000001B3ull;
    return h;
}

int ob_fs_is_dir(const char *path)
{
#ifdef _WIN32
    DWORD a = GetFileAttributesA(path);
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
#else
    struct stat st;
    return !stat(path, &st) && S_ISDIR(st.st_mode);
#endif
}

int ob_fs_mkdir(const char *path)
{
    if (ob_fs_is_dir(path)) return 0;
#ifdef _WIN32
    _mkdir(path);
#else
    mkdir(path, 0755);
#endif
    return ob_fs_is_dir(path) ? 0 : -1;
}

static void add_sorted(char (*name)[OB_FS_NAME], int *n, int max, const char *s)
{
    int j;
    if (*n >= max || !s[0] || s[0] == '.' || strlen(s) >= OB_FS_NAME || s[strlen(s) - 1] == '~') return;   /* (~: half written) */
    for (j = *n; j > 0 && ob_fs_stricmp(s, name[j - 1]) < 0; j--) memcpy(name[j], name[j - 1], OB_FS_NAME);
    memcpy(name[j], s, strlen(s) + 1);
    (*n)++;
}

static int list(const char *dir, char (*name)[OB_FS_NAME], int max, int folders)
{
    int n = 0;
#ifdef _WIN32
    char pat[300];
    WIN32_FIND_DATAA fd;
    HANDLE h;
    if (!ob_fs_is_dir(dir)) return -1;
    snprintf(pat, sizeof pat, "%s/*", dir);
    h = FindFirstFileA(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;
    do {
        if (folders || !(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) add_sorted(name, &n, max, fd.cFileName);
    } while (FindNextFileA(h, &fd));
    FindClose(h);
#else
    DIR *d = opendir(dir);
    struct dirent *de;
    if (!d) return -1;
    while ((de = readdir(d))) {
        char path[600];
        struct stat st;
        ob_fs_join(path, sizeof path, dir, de->d_name);
        if (!stat(path, &st) && (S_ISREG(st.st_mode) || (folders && S_ISDIR(st.st_mode)))) add_sorted(name, &n, max, de->d_name);
    }
    closedir(d);
#endif
    return n;
}

int ob_fs_list(const char *dir, char (*name)[OB_FS_NAME], int max) { return list(dir, name, max, 0); }

int ob_fs_find(const char *dir, const char *name, char *out, int size)
{
    static char all[256][OB_FS_NAME];       /* (one caller at a time: the library's modules run on one thread) */
    int n = list(dir, all, 256, 1), i;
    for (i = 0; i < n; i++)
        if (!ob_fs_stricmp(all[i], name)) { snprintf(out, (size_t)size, "%s", all[i]); return 1; }
    return 0;
}

int ob_fs_read(const char *path, uint8_t **data, size_t *size)
{
    FILE *f = fopen(path, "rb");
    long len;
    uint8_t *b;
    *data = NULL;
    *size = 0;
    if (!f) return -1;
    if (fseek(f, 0, SEEK_END) || (len = ftell(f)) < 0 || fseek(f, 0, SEEK_SET)) { fclose(f); return -1; }
    b = (uint8_t *)malloc(len ? (size_t)len : 1);
    if (!b || (len && fread(b, 1, (size_t)len, f) != (size_t)len)) { free(b); fclose(f); return -1; }
    fclose(f);
    *data = b;
    *size = (size_t)len;
    return 0;
}

int ob_fs_write(const char *path, const void *data, size_t size)
{
    char tmp[600];
    FILE *f;
    snprintf(tmp, sizeof tmp, "%s~", path);
    f = fopen(tmp, "wb");
    if (!f) return -1;
    if ((size && fwrite(data, 1, size, f) != size) | fclose(f)) { remove(tmp); return -1; }
    remove(path);                       /* (Windows' rename does not replace) */
    if (rename(tmp, path)) { remove(tmp); return -1; }
    return 0;
}

void ob_fs_clear(const char *dir)
{
    static char files[256][OB_FS_NAME];
    char path[600];
    int n = ob_fs_list(dir, files, 256), i;
    for (i = 0; i < n; i++) { ob_fs_join(path, sizeof path, dir, files[i]); remove(path); }
#ifdef _WIN32
    _rmdir(dir);
#else
    rmdir(dir);
#endif
}
