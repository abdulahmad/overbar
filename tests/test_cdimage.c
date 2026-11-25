/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * Copyright (c) 2026 abdulahmad
 *
 * The CD image reader (src/cdimage.c) on discs this test makes: a data track of two files
 * (ISO 9660), two audio tracks, ripped as a .cue with a file a track (raw 2352 byte sectors with
 * the pregap), as one .bin for the disc, as a plain .iso with loose WAVE tracks beside it, and
 * as a CloneCD .ccd / .img. */
#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif
#include "overbar/cdimage.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#define mk(p) _mkdir(p)
#else
#define mk(p) mkdir(p, 0777)
#endif

static int fails, checks;
#define CHECK(c) do { checks++; if (!(c)) { fails++; printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

#define SECTORS 24
static uint8_t iso[SECTORS * 2048];
static const char hello[] = "HELLO FROM THE DATA TRACK";

static void both32(uint8_t *b, uint32_t v) { for (int i = 0; i < 4; i++) { b[i] = (uint8_t)(v >> 8 * i); b[7 - i] = (uint8_t)(v >> 8 * i); } }

static int dirent_(uint8_t *b, const char *name, uint32_t lba, uint32_t size, int dir)
{
    int nl = (int)strlen(name), len = 33 + nl + (nl % 2 == 0);
    memset(b, 0, (size_t)len);
    b[0] = (uint8_t)len;
    both32(b + 2, lba);
    both32(b + 10, size);
    b[25] = dir ? 2 : 0;
    b[32] = (uint8_t)nl;
    memcpy(b + 33, name, (size_t)nl);
    return len;
}

/* the data track: PVD at 16, root at 18, SUB at 19, GRAPH.BIN at 20 (5000 bytes), SUB/X.TXT at 23 */
static void make_iso(void)
{
    uint8_t *pvd = iso + 16 * 2048, *root = iso + 18 * 2048, *sub = iso + 19 * 2048;
    int p = 0;
    pvd[0] = 1;
    memcpy(pvd + 1, "CD001", 5);
    pvd[6] = 1;
    dirent_(pvd + 156, "\0", 18, 2048, 1);
    iso[17 * 2048] = 255;
    memcpy(iso + 17 * 2048 + 1, "CD001", 5);
    p += dirent_(root + p, "\0", 18, 2048, 1);
    p += dirent_(root + p, "\1", 18, 2048, 1);
    p += dirent_(root + p, "GRAPH.BIN;1", 20, 5000, 0);
    p += dirent_(root + p, "SUB", 19, 2048, 1);
    p = 0;
    p += dirent_(sub + p, "\0", 19, 2048, 1);
    p += dirent_(sub + p, "\1", 18, 2048, 1);
    dirent_(sub + p, "X.TXT;1", 23, sizeof hello - 1, 0);
    for (int i = 0; i < 5000; i++) iso[20 * 2048 + i] = (uint8_t)(i * 7);
    memcpy(iso + 23 * 2048, hello, sizeof hello - 1);
}

/* the data track as raw Mode 1 sectors (sync, header, 2048 bytes, the rest zero) */
static void put_raw(FILE *f, int pregap)
{
    uint8_t s[2352];
    for (int i = 0; i < pregap + SECTORS; i++) {
        memset(s, 0, sizeof s);
        memset(s + 1, 0xFF, 10);
        s[15] = 1;
        if (i >= pregap) memcpy(s + 16, iso + (i - pregap) * 2048, 2048);
        fwrite(s, 1, sizeof s, f);
    }
}

/* audio track t: frames stereo frames, left = t * 1000 + i % 100, right its negative */
static void put_audio(FILE *f, int t, int frames)
{
    for (int i = 0; i < frames; i++) {
        int16_t l = (int16_t)(t * 1000 + i % 100), r = (int16_t)-l;
        uint8_t b[4] = { (uint8_t)l, (uint8_t)(l >> 8), (uint8_t)r, (uint8_t)(r >> 8) };
        fwrite(b, 1, 4, f);
    }
}

static void put_wav(const char *path, int t, int frames)
{
    FILE *f = fopen(path, "wb");
    uint8_t h[44] = { 'R', 'I', 'F', 'F', 0, 0, 0, 0, 'W', 'A', 'V', 'E', 'f', 'm', 't', ' ', 16, 0, 0, 0, 1, 0, 2, 0,
                      0x44, 0xAC, 0, 0, 0x10, 0xB1, 2, 0, 4, 0, 16, 0, 'd', 'a', 't', 'a' };
    uint32_t n = (uint32_t)frames * 4;
    for (int i = 0; i < 4; i++) { h[40 + i] = (uint8_t)(n >> 8 * i); h[4 + i] = (uint8_t)((n + 36) >> 8 * i); }
    fwrite(h, 1, 44, f);
    put_audio(f, t, frames);
    fclose(f);
}

static const char *const need[2] = { "graph.bin", "SUB/X.TXT" };

static void check_disc(ob_cd *cd, const char *what, int f2, int f3)
{
    uint8_t *b;
    int16_t s[8];
    printf("  %s: %s\n", what, cd ? ob_cd_path(cd) : "(none)");
    CHECK(cd != NULL);
    if (!cd) return;
    long n = ob_cd_read_file(cd, "GRAPH.BIN", &b);
    CHECK(n == 5000 && b && b[4999] == (uint8_t)(4999 * 7) && b[2048] == (uint8_t)(2048 * 7));
    free(b);
    n = ob_cd_read_file(cd, "sub/x.txt", &b);
    CHECK(n == (long)sizeof hello - 1 && b && !memcmp(b, hello, sizeof hello - 1));
    free(b);
    CHECK(ob_cd_read_file(cd, "NOPE.BIN", &b) == -1);
    CHECK(ob_cd_audio_tracks(cd) == 2);
    CHECK(ob_cd_track_frames(cd, 2) == f2 && ob_cd_track_frames(cd, 3) == f3);
    CHECK(ob_cd_track_read(cd, 3, 99, s, 4) == 4 && s[0] == 3099 && s[1] == -3099 && s[2] == 3000);
    CHECK(ob_cd_track_read(cd, 2, f2 - 1, s, 4) == 1 && ob_cd_track_read(cd, 2, f2, s, 4) == 0);
    ob_cd_close(cd);
}

int main(void)
{
    char base[64];
    FILE *f;
    snprintf(base, sizeof base, "test_cdimage_out");
    mk(base);
    make_iso();
    /* 1: a .cue, a file a track, the data with its pregap given by INDEX 01 */
    mk("test_cdimage_out/a");
    f = fopen("test_cdimage_out/a/Game (Track 1).bin", "wb"); put_raw(f, 150); fclose(f);
    f = fopen("test_cdimage_out/a/Game (Track 2).bin", "wb"); put_audio(f, 2, 588 * 3); fclose(f);
    f = fopen("test_cdimage_out/a/Game (Track 3).bin", "wb"); put_audio(f, 3, 588 * 150 + 588 * 2); fclose(f);
    f = fopen("test_cdimage_out/a/Game.cue", "wb");
    fprintf(f, "\xEF\xBB\xBF" "FILE \"Game (Track 1).bin\" BINARY\r\n  TRACK 01 MODE1/2352\r\n    INDEX 01 00:02:00\r\n"
               "FILE \"Game (Track 2).bin\" BINARY\r\n  TRACK 02 AUDIO\r\n    INDEX 01 00:00:00\r\n"
               "FILE \"C:\\Rips\\old\\Game (Track 3).bin\" BINARY\r\n  TRACK 03 AUDIO\r\n    INDEX 00 00:00:00\r\n    INDEX 01 00:02:00\r\n");
    fclose(f);
    check_disc(ob_cd_find("test_cdimage_out/a", need, 2, NULL, 0), "cue, a file a track", 588 * 3, 588 * 2);
    /* 2: one .bin for the disc */
    mk("test_cdimage_out/b");
    f = fopen("test_cdimage_out/b/disc.bin", "wb"); put_raw(f, 0); put_audio(f, 2, 588 * 10); put_audio(f, 9, 588 * 2); put_audio(f, 3, 588 * 2); fclose(f);
    f = fopen("test_cdimage_out/b/disc.cue", "wb");
    fprintf(f, "FILE disc.bin BINARY\n TRACK 1 MODE1/2352\n  INDEX 1 00:00:00\n TRACK 2 AUDIO\n  INDEX 1 00:00:%02d\n TRACK 3 AUDIO\n  INDEX 0 00:00:%02d\n  INDEX 1 00:00:%02d\n",
            SECTORS, SECTORS + 10, SECTORS + 12);
    fclose(f);
    check_disc(ob_cd_find("test_cdimage_out/b", need, 2, NULL, 0), "cue, one file", 588 * 10, 588 * 2);
    /* 3: a cooked .iso and loose WAVE tracks */
    mk("test_cdimage_out/c");
    f = fopen("test_cdimage_out/c/data.iso", "wb"); fwrite(iso, 1, sizeof iso, f); fclose(f);
    put_wav("test_cdimage_out/c/Track02.wav", 2, 500);
    put_wav("test_cdimage_out/c/03 - Song.wav", 3, 700);
    check_disc(ob_cd_find("test_cdimage_out/c", need, 2, NULL, 0), "iso and loose wav", 500, 700);
    /* 4: CloneCD */
    mk("test_cdimage_out/d");
    f = fopen("test_cdimage_out/d/rip.img", "wb"); put_raw(f, 0); put_audio(f, 2, 588 * 6); put_audio(f, 3, 588 * 5); fclose(f);
    f = fopen("test_cdimage_out/d/rip.ccd", "wb");
    fprintf(f, "[Entry 0]\nPoint=0xa0\n[Entry 3]\nPoint=0x01\nControl=0x04\nPLBA=0\n[Entry 4]\nPoint=0x02\nControl=0x00\nPLBA=%d\n"
               "[Entry 5]\nPoint=0x03\nControl=0x00\nPLBA=%d\n", SECTORS, SECTORS + 6);
    fclose(f);
    check_disc(ob_cd_find("test_cdimage_out/d", need, 2, NULL, 0), "CloneCD", 588 * 6, 588 * 5);
    /* not this disc: its files missing */
    static const char *const other[1] = { "HOCKEY.BIN" };
    CHECK(ob_cd_find("test_cdimage_out", other, 1, NULL, 0) == NULL);
    printf("test_cdimage: %d checks, %d failed\n", checks, fails);
    return fails != 0;
}
