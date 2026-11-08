/* Tests of the memory states (overbar/snap.h). */
#include "overbar/snap.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); return 1; } } while (0)
#define PG 4096u

/* a mock write watch: the pages the test says it wrote */
static unsigned dirty[64], ndirty;
static void *watched_block;
static int mock_watch(void *addr, size_t size, void **pages, size_t *n, int reset)
{
    size_t i, k = 0;
    (void)reset;
    if (addr != watched_block) return -1;                    /* (b is not a watched block) */
    if (!pages) { ndirty = 0; return 0; }                    /* forget */
    for (i = 0; i < ndirty && k < *n; i++) {
        uint8_t *p = (uint8_t *)addr + (size_t)dirty[i] * PG;
        if (p < (uint8_t *)addr + size) pages[k++] = p;
    }
    *n = k;
    return 0;
}

static int round_trip(ob_snap_watch_fn watch)
{
    static uint8_t a[16 * PG], b[3 * PG + 100];
    ob_snap_region r[2];
    ob_snap_store *s = ob_snap_store_new(watch);
    ob_snap s1, s2, s3;
    size_t bn;
    uint8_t keep[16 * PG], keepb[sizeof b];
    memset(&s1, 0, sizeof s1); memset(&s2, 0, sizeof s2); memset(&s3, 0, sizeof s3);
    memset(a, 1, sizeof a); memset(b, 2, sizeof b);
    r[0].p = a; r[0].n = sizeof a; r[1].p = b; r[1].n = sizeof b;
    CHECK(s);
    watched_block = a;
    ndirty = 0;
    CHECK(ob_snap_take(s, &s1, r, 2, "one", 4) == 0);          /* the first state is the base */
    CHECK(s1.ndiff == 0);
    a[5 * PG + 7] = 9; dirty[ndirty++] = 5;
    b[3 * PG + 50] = 8;                                       /* (the last, short page) */
    CHECK(ob_snap_take(s, &s2, r, 2, "two", 4) == 0);
    CHECK(s2.ndiff >= 1);
    memcpy(keep, a, sizeof a); memcpy(keepb, b, sizeof b);
    a[0] = 77; dirty[ndirty++] = 0; a[9 * PG] = 66; dirty[ndirty++] = 9;
    CHECK(ob_snap_take(s, &s3, r, 2, NULL, 0) == 0);
    CHECK(ob_snap_fits(s, &s3, r, 2) == 0);
    CHECK(ob_snap_put(s, &s2) == 0);                          /* back to the second */
    CHECK(!memcmp(a, keep, sizeof a));
    CHECK(!memcmp(b, keepb, sizeof b));
    CHECK(!strcmp((const char *)ob_snap_blob(&s2, &bn), "two") && bn == 4);
    CHECK(ob_snap_put(s, &s1) == 0);                          /* back to the first */
    CHECK(a[5 * PG + 7] == 1 && a[0] == 1 && b[3 * PG + 50] == 2);
    CHECK(ob_snap_put(s, &s3) == 0);                          /* and forward again */
    CHECK(a[0] == 77 && a[9 * PG] == 66 && a[5 * PG + 7] == 9 && b[3 * PG + 50] == 8);
    r[1].n -= 10;                                             /* the layout changes: a new base, the old states are unusable */
    CHECK(ob_snap_fits(s, &s3, r, 2) != 0);
    { ob_snap s4; memset(&s4, 0, sizeof s4); CHECK(ob_snap_take(s, &s4, r, 2, NULL, 0) == 0); CHECK(ob_snap_put(s, &s1) != 0); CHECK(ob_snap_put(s, &s4) == 0); ob_snap_free(&s4); }
    ob_snap_free(&s1); ob_snap_free(&s2); ob_snap_free(&s3);
    ob_snap_store_free(s);
    return 0;
}

int main(void)
{
    if (round_trip(NULL)) return 1;
    printf("snap without a watch: ok\n");
    if (round_trip(mock_watch)) return 1;
    printf("snap with a watch: ok\n");
    return 0;
}
