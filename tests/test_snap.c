/* Tests of the memory states (overbar/snap.h). */
#include "overbar/snap.h"
#include "overbar/rollback.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); return 1; } } while (0)
#define PG 4096u
#define NOINLINE_TEST __attribute__((noinline))

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

/* ---- rollback (overbar/rollback.h): a game loop whose step 6 loads the state of step 2 ---- */
static uint8_t game_mem[3 * PG];
static int steps, loaded_once, held, last_tag, landed;
static uint8_t blob2[64];
static size_t blob2_n;
static ob_rollback *rbt;
static int shared_mode;                 /* 1: step 6 takes a shared copy of step 2's state instead of loading it */
static uint8_t *shared;
static size_t shared_n;

static int game_regions(void *user, ob_snap_region *r, int max)
{
    (void)user;
    if (max < 1) return -1;
    r[0].p = game_mem; r[0].n = sizeof game_mem;
    return 1;
}
static uint32_t game_hold(void *user) { (void)user; held++; return (uint32_t)steps; }
static void game_release(void *user, uint32_t tag, int loaded) { (void)user; if (loaded) { landed++; last_tag = (int)tag; } }

static int fake_step(void *user)
{
    const ob_net_states *st = ob_rollback_states(rbt);
    (void)user;
    steps++;
    if (steps == 2) {
        blob2_n = st->save(st->user, blob2, sizeof blob2);
        if (shared_mode) {
            shared_n = st->share(st->user, blob2, blob2_n, NULL, 0);
            shared = malloc(shared_n);
            if (!shared || st->share(st->user, blob2, blob2_n, shared, shared_n) != shared_n) return -1;
        }
    } else if (steps == 6 && !loaded_once) {
        loaded_once = 1;
        if (shared_mode) {
            uint8_t keep = shared[16];
            shared[16] ^= 1;                                   /* (a region at another address: refused) */
            if (st->take(st->user, shared, shared_n) == 0 || !ob_rollback_why(rbt)[0]) return -1;
            shared[16] = keep;
            if (st->take(st->user, shared, shared_n - 1) == 0) return -1;
            game_mem[PG + 5] = 200;                            /* (the shared state puts it back) */
            if (st->take(st->user, shared, shared_n)) { printf("take: %s\n", ob_rollback_why(rbt)); return -1; }
        } else if (st->load(st->user, blob2, blob2_n)) return -1;
        return OB_NET_RESTART;
    } else (void)st->save(st->user, blob2 + 32, 32);           /* (more states, as the core takes them) */
    return OB_NET_RUN;
}

static NOINLINE_TEST int game_loop(void)
{
    volatile int i, on_stack[4] = { 0, 0, 0, 0 };
    for (i = 0; i < 10; i++) {
        if (ob_rollback_frame(rbt, fake_step, NULL) != OB_NET_RUN) return -1;
        game_mem[PG + 5] = (uint8_t)(game_mem[PG + 5] + 1);  /* the game's memory and its stack go on */
        on_stack[i % 4]++;
    }
    return on_stack[0] + on_stack[1] + on_stack[2] + on_stack[3];
}

static NOINLINE_TEST int rollback_test(void)
{
    ob_rollback_game g;
    int r;
    memset(&g, 0, sizeof g);
    g.regions = game_regions;
    g.hold = game_hold;
    g.release = game_release;
    g.slots = 8;
    rbt = ob_rollback_new(&g, __builtin_frame_address(0));
    CHECK(rbt);
    r = game_loop();
    CHECK(r == 10);                 /* the loop's counters on the stack went back with the state: 10, not 14 */
    CHECK(steps == 15);             /* 6, then 9 more from the second iteration */
    CHECK(game_mem[PG + 5] == 10);
    CHECK(landed == 1 && last_tag == 2);   /* the tag hold gave when step 2's state was taken */
    ob_rollback_free(rbt);
    return 0;
}

int main(void)
{
    if (round_trip(NULL)) return 1;
    printf("snap without a watch: ok\n");
    if (round_trip(mock_watch)) return 1;
    printf("snap with a watch: ok\n");
    if (rollback_test()) return 1;
    printf("rollback: ok\n");
    steps = loaded_once = held = last_tag = landed = 0;
    memset(game_mem, 0, sizeof game_mem);
    shared_mode = 1;
    if (rollback_test()) return 1;
    free(shared);
    printf("rollback, a shared state taken: ok\n");
    return 0;
}
