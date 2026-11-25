/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * Copyright (c) 2026 abdulahmad
 *
 * A ripped CD image read as the disc (include/overbar/cdimage.h): the sheet or table that says
 * where each track is, the data track's ISO 9660 files, the audio tracks' samples.
 */
#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif
#include "overbar/cdimage.h"

#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define FRAME 2352                      /* a CD frame: audio, or a raw sector */
#define SECTOR 2048
#define PATH 600
#define FILES_MAX 100                   /* the image's files (a track a file: 99 tracks and a sheet's data file) */
#define OPEN_MAX 4                      /* of them open at once (the ones last read) */
#define ISO_MAX 2048                    /* files on the data track */
#define WALK_MAX 4096

/* src into out (size bytes), cut short if it must be */
static void cpy(char *out, size_t size, const char *src)
{
    size_t n = strlen(src);
    if (n >= size) n = size - 1;
    memcpy(out, src, n);
    out[n] = 0;
}

struct src { char path[PATH]; FILE *f; long size; unsigned long used; };
struct track { int file; long start, end; };          /* (byte offsets in its file: end -1 its end) */
struct isofile { char name[96]; uint32_t lba, size; };

struct ob_cd {
    char opened[PATH];
    struct src files[FILES_MAX];
    int nfiles;
    unsigned long reads;                /* (which open file was read last) */
    int data;                           /* the data track's file */
    long data_at;                       /* where it starts in that file */
    int ssize, header, lead;            /* its sectors: their size, the header before the 2048 bytes, a pregap */
    struct track audio[OB_CD_TRACKS + 1];   /* by number; file -1 none */
    struct isofile *iso;
    int niso;
};

/* ---- small helpers ---- */

static int ieq(const char *a, const char *b)
{
    for (; *a && *b; a++, b++) if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return 0;
    return *a == *b;
}

static int ends(const char *s, const char *ext)
{
    size_t n = strlen(s), m = strlen(ext);
    return n >= m && ieq(s + n - m, ext);
}

static const char *base_name(const char *p)
{
    const char *a = strrchr(p, '/'), *b = strrchr(p, '\\');
    if (b && (!a || b > a)) a = b;
    return a ? a + 1 : p;
}

static void dir_of(const char *p, char *out, size_t size)
{
    const char *b = base_name(p);
    size_t n = (size_t)(b - p);
    if (n >= size) n = size - 1;
    memcpy(out, p, n);
    out[n] = 0;
    if (!n) snprintf(out, size, ".");
    else if (n > 1 && (out[n - 1] == '/' || out[n - 1] == '\\')) out[n - 1] = 0;
}

static void join(char *out, size_t size, const char *a, const char *b)
{
    size_t n = strlen(a);
    snprintf(out, size, "%s%s%s", a, n && (a[n - 1] == '/' || a[n - 1] == '\\') ? "" : "/", b);
}

static int is_dir(const char *p) { struct stat st; return stat(p, &st) == 0 && S_ISDIR(st.st_mode); }
static long file_size(const char *p) { struct stat st; return stat(p, &st) == 0 && S_ISREG(st.st_mode) ? (long)st.st_size : -1; }

static uint32_t le32(const uint8_t *b) { return (uint32_t)b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24; }
static uint16_t le16(const uint8_t *b) { return (uint16_t)(b[0] | b[1] << 8); }

/* the files below dir (depth folders down) into list: how many */
static int walk(const char *dir, int depth, char (*list)[PATH], int n, int max)
{
    DIR *d = opendir(dir);
    struct dirent *e;
    if (!d) return n;
    while ((e = readdir(d)) && n < max) {
        char p[PATH];
        if (e->d_name[0] == '.') continue;
        join(p, sizeof p, dir, e->d_name);
        if (is_dir(p)) { if (depth > 0) n = walk(p, depth - 1, list, n, max); }
        else cpy(list[n++], PATH, p);
    }
    closedir(d);
    return n;
}

/* ---- the image's files ---- */

static int add_file(ob_cd *cd, const char *path)
{
    for (int i = 0; i < cd->nfiles; i++) if (!strcmp(cd->files[i].path, path)) return i;
    if (cd->nfiles == FILES_MAX) return -1;
    long sz = file_size(path);
    if (sz < 0) return -1;
    struct src *s = &cd->files[cd->nfiles];
    cpy(s->path, sizeof s->path, path);
    s->f = NULL;
    s->size = sz;
    return cd->nfiles++;
}

static long read_at(ob_cd *cd, int file, long at, void *out, long n)
{
    struct src *s;
    if (file < 0 || file >= cd->nfiles || at < 0) return 0;
    s = &cd->files[file];
    if (!s->f) {
        int open = 0, old = -1;
        for (int i = 0; i < cd->nfiles; i++)
            if (cd->files[i].f) { open++; if (old < 0 || cd->files[i].used < cd->files[old].used) old = i; }
        if (open >= OPEN_MAX && old >= 0) { fclose(cd->files[old].f); cd->files[old].f = NULL; }
        if (!(s->f = fopen(s->path, "rb"))) return 0;
    }
    s->used = ++cd->reads;
    if (at >= s->size) return 0;
    if (n > s->size - at) n = s->size - at;
    if (fseek(s->f, at, SEEK_SET) != 0) return 0;
    return (long)fread(out, 1, (size_t)n, s->f);
}

/* ---- the data track: its sectors' layout, its ISO 9660 directory ---- */

static int read_sector(ob_cd *cd, uint32_t lba, uint8_t out[SECTOR])
{
    long at = cd->data_at + ((long)lba + cd->lead) * cd->ssize + cd->header;
    return read_at(cd, cd->data, at, out, SECTOR) == SECTOR;
}

static int find_layout(ob_cd *cd)
{
    static const int lay[][2] = { { 2048, 0 }, { 2352, 16 }, { 2352, 24 }, { 2336, 8 }, { 2448, 16 }, { 2448, 24 } };
    static const int leads[2] = { 0, 150 };
    uint8_t b[8];
    for (int l = 0; l < 2; l++)
        for (unsigned i = 0; i < sizeof lay / sizeof lay[0]; i++) {
            long at = cd->data_at + (16L + leads[l]) * lay[i][0] + lay[i][1];
            if (read_at(cd, cd->data, at, b, 8) == 8 && !memcmp(b + 1, "CD001", 5)) {
                cd->ssize = lay[i][0];
                cd->header = lay[i][1];
                cd->lead = leads[l];
                return 1;
            }
        }
    return 0;
}

static void read_dir(ob_cd *cd, uint32_t lba, uint32_t size, const char *prefix, int depth)
{
    uint8_t sec[SECTOR];
    if (depth > 8 || size > 1u << 20) return;
    for (uint32_t off = 0; off < size; off += SECTOR) {
        if (!read_sector(cd, lba + off / SECTOR, sec)) return;
        for (int p = 0; p < SECTOR;) {
            int len = sec[p];
            if (!len || p + len > SECTOR || len < 34) break;
            uint32_t rl = le32(sec + p + 2), rs = le32(sec + p + 10);
            int flags = sec[p + 25], nl = sec[p + 32];
            char name[96];
            int k = 0;
            if (33 + nl > len) break;
            for (int i = 0; i < nl && k < 60; i++) {
                char c = (char)sec[p + 33 + i];
                if (c == ';') break;
                name[k++] = c;
            }
            name[k] = 0;
            if (k && name[k - 1] == '.') name[--k] = 0;
            p += len;
            if (!(nl == 1 && sec[p - len + 33] <= 1) && k) {
                char full[96];
                if (snprintf(full, sizeof full, "%s%s%s", prefix, *prefix ? "/" : "", name) >= (int)sizeof full) continue;
                if (flags & 2) read_dir(cd, rl, rs, full, depth + 1);
                else if (cd->niso < ISO_MAX) {
                    cpy(cd->iso[cd->niso].name, sizeof cd->iso[0].name, full);
                    cd->iso[cd->niso].lba = rl;
                    cd->iso[cd->niso++].size = rs;
                }
            }
        }
    }
}

static int open_data(ob_cd *cd)
{
    uint8_t pvd[SECTOR];
    if (!find_layout(cd) || !read_sector(cd, 16, pvd) || pvd[0] != 1) return 0;
    if (!cd->iso && !(cd->iso = calloc(ISO_MAX, sizeof *cd->iso))) return 0;
    cd->niso = 0;
    read_dir(cd, le32(pvd + 158), le32(pvd + 166), "", 0);
    return cd->niso > 0;
}

static int iso_index(const ob_cd *cd, const char *name)
{
    while (*name == '/' || *name == '\\') name++;
    for (int i = 0; i < cd->niso; i++) if (ieq(cd->iso[i].name, name)) return i;
    return -1;
}

static int has_all(const ob_cd *cd, const char *const *need, int n)
{
    for (int i = 0; i < n; i++) if (iso_index(cd, need[i]) < 0) return 0;
    return 1;
}

/* path (a file) as a data track holding need's files: into cd (its data and layout), 1; else 0 */
static int try_data(ob_cd *cd, const char *path, long at, const char *const *need, int nneed)
{
    int f = add_file(cd, path);
    if (f < 0) return 0;
    cd->data = f;
    cd->data_at = at;
    return open_data(cd) && has_all(cd, need, nneed);
}

/* ---- WAVE files ---- */

/* the samples of a 16-bit stereo 44.1 kHz PCM WAVE: their offset and bytes, 1; else 0 */
static int wav_info(const char *path, long *off, long *len)
{
    uint8_t h[4096];
    FILE *f = fopen(path, "rb");
    long n, total = file_size(path);
    int ok = 0, fmt = 0;
    if (!f) return 0;
    n = (long)fread(h, 1, sizeof h, f);
    fclose(f);
    if (n < 44 || memcmp(h, "RIFF", 4) || memcmp(h + 8, "WAVE", 4)) return 0;
    for (long at = 12; at + 8 <= n;) {
        uint32_t size = le32(h + at + 4);
        if (!memcmp(h + at, "fmt ", 4) && at + 24 <= n)
            fmt = (le16(h + at + 8) == 1 || le16(h + at + 8) == 0xFFFE) && le16(h + at + 10) == 2 && le32(h + at + 12) == OB_CD_RATE && le16(h + at + 22) == 16;
        if (!memcmp(h + at, "data", 4)) {
            *off = at + 8;
            *len = size == 0 || size == 0xFFFFFFFFu || (long)size > total - *off ? total - *off : (long)size;
            ok = fmt;
            break;
        }
        at += 8 + (long)size + (size & 1);
    }
    return ok;
}

/* ---- .cue sheets ---- */

/* name (a sheet's FILE) as a file: beside the sheet in any case, by its path, else by bare name below */
static int resolve(const char *folder, const char *name, char *out, size_t size)
{
    char p[PATH], list[64][PATH];
    cpy(p, sizeof p, name);
    for (char *c = p; *c; c++) if (*c == '\\') *c = '/';
    if (file_size(p) >= 0 && (p[0] == '/' || (p[0] && p[1] == ':'))) { cpy(out, size, p); return 1; }
    join(out, size, folder, p);
    if (file_size(out) >= 0) return 1;
    const char *b = base_name(p);
    int n = walk(folder, 2, list, 0, 64);
    for (int i = 0; i < n; i++) if (ieq(base_name(list[i]), b)) { cpy(out, size, list[i]); return 1; }
    return 0;
}

struct cue_track { int n, audio, wav, size; char file[PATH]; int have; long i0, i1; };

static int parse_cue(const char *sheet, struct cue_track *t, int max)
{
    FILE *f = fopen(sheet, "rb");
    char line[1024], folder[PATH], file[PATH] = "";
    int n = 0, have = 0, wav = 0;
    if (!f) return 0;
    dir_of(sheet, folder, sizeof folder);
    while (fgets(line, sizeof line, f)) {
        char *s = line, kw[16];
        int k = 0;
        if ((uint8_t)s[0] == 0xEF && (uint8_t)s[1] == 0xBB && (uint8_t)s[2] == 0xBF) s += 3;
        while (*s == ' ' || *s == '\t') s++;
        while (isalpha((unsigned char)*s) && k < 15) kw[k++] = (char)toupper((unsigned char)*s++);
        kw[k] = 0;
        while (*s == ' ' || *s == '\t') s++;
        for (char *e = s + strlen(s); e > s && (e[-1] == '\n' || e[-1] == '\r' || e[-1] == ' ' || e[-1] == '\t'); ) *--e = 0;
        if (!strcmp(kw, "FILE")) {
            char name[PATH];
            char *type;
            if (*s == '"' || *s == '\'') {
                char q = *s++, *e = strchr(s, q);
                if (!e) continue;
                snprintf(name, sizeof name, "%.*s", (int)(e - s), s);
                type = e + 1;
            } else {
                char *sp = strrchr(s, ' ');
                snprintf(name, sizeof name, "%.*s", sp ? (int)(sp - s) : (int)strlen(s), s);
                type = sp ? sp : s + strlen(s);
            }
            while (*type == ' ') type++;
            wav = !strncmp(type, "WAV", 3) || !strncmp(type, "wav", 3);
            have = resolve(folder, name, file, sizeof file);
            if (!have) file[0] = 0;
        } else if (!strcmp(kw, "TRACK") && n < max) {
            struct cue_track *c = &t[n++];
            memset(c, 0, sizeof *c);
            c->n = atoi(s);
            while (*s && *s != ' ') s++;
            while (*s == ' ') s++;
            c->audio = !strncmp(s, "AUDIO", 5) || !strncmp(s, "audio", 5);
            c->size = c->audio ? FRAME : strchr(s, '/') ? atoi(strchr(s, '/') + 1) : FRAME;
            c->wav = wav;
            c->have = have;
            cpy(c->file, sizeof c->file, file);
            c->i0 = c->i1 = -1;
        } else if (!strcmp(kw, "INDEX") && n > 0) {
            int idx = atoi(s), mm, ss, ff;
            while (*s && *s != ' ') s++;
            if (sscanf(s, " %d:%d:%d", &mm, &ss, &ff) == 3) {
                long fr = ((long)mm * 60 + ss) * 75 + ff;
                if (idx == 0) t[n - 1].i0 = fr;
                else if (idx == 1) t[n - 1].i1 = fr;
            }
        }
    }
    fclose(f);
    return n;
}

static void sheet_audio(ob_cd *cd, const struct cue_track *t, int n)
{
    for (int i = 0; i < n; i++) {
        const struct cue_track *c = &t[i];
        long off = 0, len = 0, start;
        if (!c->audio || !c->have || c->n < 2 || c->n > OB_CD_TRACKS || cd->audio[c->n].file >= 0) continue;
        if ((c->wav || ends(c->file, ".wav")) && !wav_info(c->file, &off, &len)) continue;
        start = c->i1 >= 0 ? c->i1 : c->i0 >= 0 ? c->i0 : 0;
        int f = add_file(cd, c->file);
        if (f < 0) continue;
        cd->audio[c->n].file = f;
        cd->audio[c->n].start = off + start * FRAME;
        cd->audio[c->n].end = len ? off + len : -1;
        if (i + 1 < n && !strcmp(t[i + 1].file, c->file)) {   /* (one file for the disc: to the next track) */
            long at = t[i + 1].i0 >= 0 ? t[i + 1].i0 : t[i + 1].i1;
            if (at >= 0) cd->audio[c->n].end = off + at * FRAME;
        }
    }
}

static int open_cue(ob_cd *cd, const char *sheet, const char *const *need, int nneed)
{
    static struct cue_track t[OB_CD_TRACKS + 1];
    int n = parse_cue(sheet, t, OB_CD_TRACKS + 1), ok = 0;
    for (int i = 0; i < n && !ok; i++) {
        if (t[i].audio) continue;
        if (t[i].have) ok = try_data(cd, t[i].file, (t[i].i1 > 0 ? t[i].i1 : 0) * (long)t[i].size, need, nneed);
        if (!ok) {                                        /* (renamed or moved: a data file near the sheet by its contents) */
            char folder[PATH], list[256][PATH];
            dir_of(sheet, folder, sizeof folder);
            int m = walk(folder, 2, list, 0, 256);
            for (int k = 0; k < m && !ok; k++)
                if (ends(list[k], ".bin") || ends(list[k], ".iso") || ends(list[k], ".img") || ends(list[k], ".mdf"))
                    ok = try_data(cd, list[k], 0, need, nneed);
        }
    }
    if (!ok) return 0;
    cpy(cd->opened, sizeof cd->opened, sheet);
    sheet_audio(cd, t, n);
    return 1;
}

/* ---- CloneCD ---- */

static int open_ccd(ob_cd *cd, const char *ccd, const char *const *need, int nneed)
{
    char img[PATH], line[256], folder[PATH];
    int point = -1, control = 0, num = 0;
    long lba[OB_CD_TRACKS + 2];
    int data[OB_CD_TRACKS + 2];
    FILE *f;
    snprintf(img, sizeof img, "%.*s.img", (int)(strlen(ccd) - 4), ccd);
    dir_of(ccd, folder, sizeof folder);
    if (file_size(img) < 0 && !resolve(folder, base_name(img), img, sizeof img)) return 0;
    if (!try_data(cd, img, 0, need, nneed)) return 0;
    cpy(cd->opened, sizeof cd->opened, ccd);
    for (int i = 0; i <= OB_CD_TRACKS + 1; i++) { lba[i] = -1; data[i] = 0; }
    if (!(f = fopen(ccd, "rb"))) return 1;
    while (fgets(line, sizeof line, f)) {
        char *eq = strchr(line, '=');
        if (line[0] == '[') { point = -1; control = 0; continue; }
        if (!eq) continue;
        long v = strtol(eq + 1, NULL, 0);
        if (!strncmp(line, "Point", 5)) point = (int)v;
        else if (!strncmp(line, "Control", 7)) control = (int)v;
        else if (!strncmp(line, "PLBA", 4) && point >= 1 && point <= OB_CD_TRACKS) { lba[point] = v; data[point] = control & 4; if (point > num) num = point; }
    }
    fclose(f);
    for (int t = 2; t <= num; t++) {
        if (lba[t] < 0 || data[t]) continue;
        cd->audio[t].file = cd->data;
        cd->audio[t].start = lba[t] * FRAME;
        cd->audio[t].end = t < num && lba[t + 1] >= 0 ? lba[t + 1] * FRAME : -1;
    }
    return 1;
}

/* ---- loose numbered tracks beside the data track ---- */

static int track_number(const char *file)
{
    const char *b = base_name(file), *p;
    char stem[PATH];
    cpy(stem, sizeof stem, b);
    char *dot = strrchr(stem, '.');
    if (dot) *dot = 0;
    for (p = stem; *p; p++)
        if (!strncmp(p, "Track", 5) || !strncmp(p, "track", 5) || !strncmp(p, "TRACK", 5)) {
            p += 5;
            while (*p == ' ' || *p == '_' || *p == '-') p++;
            return isdigit((unsigned char)*p) ? atoi(p) : -1;
        }
    if (isdigit((unsigned char)stem[0])) return atoi(stem);
    int last = -1;
    for (p = stem; *p; p++) if (isdigit((unsigned char)*p) && (p == stem || !isdigit((unsigned char)p[-1]))) last = atoi(p);
    return last;
}

static void loose_audio(ob_cd *cd)
{
    static char list[256][PATH];
    char folder[PATH];
    dir_of(cd->files[cd->data].path, folder, sizeof folder);
    int n = walk(folder, 0, list, 0, 256);
    for (int i = 0; i < n; i++) {
        int t = track_number(list[i]);
        long off = 0, len = 0, sz = file_size(list[i]);
        if (t < 2 || t > OB_CD_TRACKS || cd->audio[t].file >= 0 || !strcmp(list[i], cd->files[cd->data].path)) continue;
        if (ends(list[i], ".wav")) { if (!wav_info(list[i], &off, &len)) continue; }
        else if (!(ends(list[i], ".bin") || ends(list[i], ".raw") || ends(list[i], ".cdda")) || sz < FRAME * 75 || sz % FRAME) continue;
        int f = add_file(cd, list[i]);
        if (f < 0) continue;
        cd->audio[t].file = f;
        cd->audio[t].start = off;
        cd->audio[t].end = len ? off + len : -1;
    }
}

/* ---- the API ---- */

static ob_cd *fresh(void)
{
    ob_cd *cd = calloc(1, sizeof *cd);
    if (!cd) return NULL;
    for (int i = 0; i <= OB_CD_TRACKS; i++) cd->audio[i].file = -1;
    cd->data = -1;
    return cd;
}

static void reset(ob_cd *cd)
{
    for (int i = 0; i < cd->nfiles; i++) if (cd->files[i].f) fclose(cd->files[i].f);
    cd->nfiles = 0;
    cd->data = -1;
    cd->niso = 0;
    for (int i = 0; i <= OB_CD_TRACKS; i++) cd->audio[i].file = -1;
}

ob_cd *ob_cd_open(const char *path, const char *const *need, int nneed)
{
    ob_cd *cd = fresh();
    int ok;
    if (!cd) return NULL;
    if (ends(path, ".cue")) ok = open_cue(cd, path, need, nneed);
    else if (ends(path, ".ccd")) ok = open_ccd(cd, path, need, nneed);
    else {
        ok = try_data(cd, path, 0, need, nneed);
        if (ok) cpy(cd->opened, sizeof cd->opened, path);
    }
    if (!ok) { ob_cd_close(cd); return NULL; }
    if (ob_cd_audio_tracks(cd) < 2) loose_audio(cd);
    return cd;
}

ob_cd *ob_cd_find(const char *dir, const char *const *need, int nneed, char *note, int note_size)
{
    static char list[WALK_MAX][PATH];
    static const char *const kinds[3][5] = { { ".cue" }, { ".ccd" }, { ".iso", ".bin", ".img", ".mdf", ".raw" } };
    int n = walk(dir, 3, list, 0, WALK_MAX);
    if (note && note_size > 0) note[0] = 0;
    for (int k = 0; k < 3; k++)
        for (int i = 0; i < n; i++) {
            int match = 0;
            for (int e = 0; e < 5 && kinds[k][e]; e++) match |= ends(list[i], kinds[k][e]);
            if (!match) continue;
            if (k == 2 && file_size(list[i]) < 16L * 2048) continue;
            ob_cd *cd = ob_cd_open(list[i], need, nneed);
            if (cd) return cd;
        }
    for (int i = 0; i < n && note && note_size > 0; i++) {
        static const char *const no[] = { ".chd", ".nrg", ".cdi", ".mds", ".ecm", ".7z", ".zip", ".rar" };
        for (unsigned e = 0; e < sizeof no / sizeof no[0]; e++)
            if (ends(list[i], no[e])) { snprintf(note, (size_t)note_size, "%s: convert it to .cue/.bin", base_name(list[i])); return NULL; }
    }
    return NULL;
}

void ob_cd_close(ob_cd *cd)
{
    if (!cd) return;
    reset(cd);
    free(cd->iso);
    free(cd);
}

const char *ob_cd_path(const ob_cd *cd) { return cd ? cd->opened : ""; }

long ob_cd_file_size(const ob_cd *cd, const char *name)
{
    int i = cd ? iso_index(cd, name) : -1;
    return i < 0 ? -1 : (long)cd->iso[i].size;
}

const char *ob_cd_file_name(const ob_cd *cd, int i) { return cd && i >= 0 && i < cd->niso ? cd->iso[i].name : NULL; }

const char *ob_cd_file_at(const ob_cd *cd, long lba)
{
    for (int i = 0; cd && i < cd->niso; i++) if (cd->iso[i].lba == (uint32_t)lba) return cd->iso[i].name;
    return NULL;
}

long ob_cd_read_file(ob_cd *cd, const char *name, uint8_t **out)
{
    int i = cd ? iso_index(cd, name) : -1;
    *out = NULL;
    if (i < 0) return -1;
    uint32_t size = cd->iso[i].size;
    uint8_t *b = malloc(size ? size : 1), sec[SECTOR];
    if (!b) return -1;
    for (uint32_t at = 0; at < size; at += SECTOR) {
        uint32_t n = size - at < SECTOR ? size - at : SECTOR;
        if (!read_sector(cd, cd->iso[i].lba + at / SECTOR, sec)) { free(b); return -1; }
        memcpy(b + at, sec, n);
    }
    *out = b;
    return (long)size;
}

int ob_cd_audio_tracks(const ob_cd *cd)
{
    int n = 0;
    for (int i = 2; cd && i <= OB_CD_TRACKS; i++) n += cd->audio[i].file >= 0;
    return n;
}

static long track_end(const ob_cd *cd, int n)
{
    const struct track *t = &cd->audio[n];
    return t->end >= 0 ? t->end : cd->files[t->file].size;
}

long ob_cd_track_frames(const ob_cd *cd, int n)
{
    if (!cd || n < 2 || n > OB_CD_TRACKS || cd->audio[n].file < 0) return 0;
    long b = track_end(cd, n) - cd->audio[n].start;
    return b > 0 ? b / 4 : 0;
}

long ob_cd_track_read(ob_cd *cd, int n, long at, int16_t *out, long frames)
{
    long total = ob_cd_track_frames(cd, n), got;
    uint8_t *b = (uint8_t *)out;
    if (at < 0 || at >= total || frames <= 0) return 0;
    if (frames > total - at) frames = total - at;
    got = read_at(cd, cd->audio[n].file, cd->audio[n].start + at * 4, out, frames * 4) / 4;
    for (long i = 0; i < got * 2; i++) out[i] = (int16_t)(b[2 * i] | b[2 * i + 1] << 8);   /* (little endian on the disc) */
    return got;
}
