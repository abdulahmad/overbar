/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * Copyright (c) 2026 abdulahmad
 *
 * The Season page: overlay and menu bar page for a shared league (overbar/season.h). Overbar draws
 * who has which team, Pick a team and Sync; the game's export, import and merge stay the client's
 * hooks and run on their own. No file name is shown or typed anywhere: the host sets players up in
 * the game's own menus (a team "played on another computer"), and a guest picks "Edmonton - Bob".
 * Any game with a master copy and per-player copies can attach this page.
 */
#include "overbar/overbar.h"
#include "overbar/season.h"

#include <stdio.h>
#include <string.h>

static void team_label(const ob_host *h, int team, char *buf, int size)
{
    const char *n = h->season_team_name ? h->season_team_name(h->season_name_ud, team) : NULL;
    if (n && n[0]) snprintf(buf, (size_t)size, "%s", n);
    else snprintf(buf, (size_t)size, "Team %d", team);
}

static void team_who(const ob_host *h, int team, const char *who, char *buf, int size)
{
    char t[48];
    team_label(h, team, t, (int)sizeof t);
    if (who && who[0]) snprintf(buf, (size_t)size, "%s - %s", t, who);
    else snprintf(buf, (size_t)size, "%s", t);
}

static void note(ob_host *h, const char *ok, const char *fail)
{
    char buf[320];
    size_t n;
    ob_season *s = h->season;
    if (ok) snprintf(buf, sizeof buf, "%s", ok);
    else if (s && s->why[0]) snprintf(buf, sizeof buf, "%s: %s", fail ? fail : "failed", s->why);
    else snprintf(buf, sizeof buf, "%s", fail ? fail : "failed");
    n = strlen(buf);
    if (n >= sizeof h->season_note) n = sizeof h->season_note - 1;
    memcpy(h->season_note, buf, n);
    h->season_note[n] = 0;
}

static ob_item *add(ob_item *out, int *n, int cap, int id, int kind, const char *label)
{
    ob_item *it;
    if (*n >= cap) return NULL;
    it = &out[(*n)++];
    memset(it, 0, sizeof *it);
    it->id = id;
    it->kind = kind;
    if (label) {                        /* owned: callers build labels in their own buffers */
        snprintf(it->text, sizeof it->text, "%s", label);
        it->label = it->text;
    }
    return it;
}

static void add_text(ob_item *out, int *n, int cap, int id, const char *label, const char *detail)
{
    ob_item *it = add(out, n, cap, id, MDRT_ITEM_TEXT, label);
    if (it && detail) snprintf(it->detail, sizeof it->detail, "%s", detail);
}

static ob_item *add_action(ob_item *out, int *n, int cap, int id, const char *label, const char *detail)
{
    ob_item *it = add(out, n, cap, id, MDRT_ITEM_ACTION, label);
    if (it && detail) snprintf(it->detail, sizeof it->detail, "%s", detail);
    return it;
}

static void begin_group(ob_item *out, int *n, int cap)
{
    if (*n > 0 && out[*n - 1].kind != OB_ITEM_SEP) add(out, n, cap, 0, OB_ITEM_SEP, NULL);
}

int ob_season_ready(const ob_host *h)
{
    return h && h->season != NULL;
}

void ob_season_set(ob_host *h, ob_season *s, int host_machine, const char *dir)
{
    if (!h) return;
    h->season = s;
    h->season_host = host_machine;
    h->season_note[0] = 0;
    (void)dir;                          /* no team files: everything goes through the room */
}

void ob_season_set_hint(ob_host *h, const char *disk_hint)
{
    if (h) h->season_disk_hint = disk_hint;
}

void ob_season_set_names(ob_host *h, const char *(*team_name)(void *ud, int team), void *ud)
{
    if (!h) return;
    h->season_team_name = team_name;
    h->season_name_ud = ud;
}

void ob_season_open(ob_host *h)
{
    if (!h) return;
    if (!h->overlay_open) ob_overlay_open(h);
    h->overlay_page = OB_PAGE_SEASON;
    h->overlay.sel = 0;
    ob_overlay_build(h);
}

int ob_season_items(const ob_host *h, ob_item *out, int cap)
{
    ob_season *s;
    ob_season_league l;
    char line[160], lab[96];
    int n = 0, master = 0, i, have, shown = 0;
    if (!h || cap < 1) return 0;
    s = h->season;
    if (!s) {
        add_text(out, &n, cap, OB_ID_SEASON_NOTE, "Season", "not in this build");
        return n;
    }
    memset(&l, 0, sizeof l);
    have = s->ops.league && s->ops.league(s->ops.ud, &l) == 0 && l.name[0];
    if (have) {
        master = l.master;
        add_text(out, &n, cap, OB_ID_SEASON_NOTE, "League", l.name);
        add_text(out, &n, cap, OB_ID_SEASON_NOTE, master ? "You run this league" : "Your copy", NULL);
    } else
        add_text(out, &n, cap, OB_ID_SEASON_NOTE, "League", "none yet");
    ob_season_status(s, line, (int)sizeof line);
    if (line[0]) add_text(out, &n, cap, OB_ID_SEASON_NOTE, "Status", line);
    if (h->season_note[0]) add_text(out, &n, cap, OB_ID_SEASON_NOTE, h->season_note, NULL);
    begin_group(out, &n, cap);
    if (master) {
        int kind[OB_SEASON_TEAMS];
        char who[OB_SEASON_TEAMS][OB_NET_NAME + 1];
        if (ob_season_roster(s, kind, who) == 0) {
            for (i = 0; i < OB_SEASON_TEAMS && n < cap - 2; i++) {
                const char *what = kind[i] == OB_SEASON_HOSTS ? "on this computer"
                    : kind[i] == OB_SEASON_WAITING ? "waiting for its player"
                    : kind[i] == OB_SEASON_OWNED ? "picked" : NULL;
                if (!what) continue;
                team_who(h, i, who[i], lab, (int)sizeof lab);
                add_text(out, &n, cap, OB_ID_SEASON_NOTE, lab, what);
                shown++;
            }
        }
        if (!shown) add_text(out, &n, cap, OB_ID_SEASON_NOTE, "Players", "none set up yet");
    } else {
        int t = s->disk_team;
        if (t >= 0 && (s->disk >> t & 1)) {
            team_label(h, t, lab, (int)sizeof lab);
            add_text(out, &n, cap, OB_ID_SEASON_NOTE, lab,
                     h->season_disk_hint ? h->season_disk_hint : "yours: load it from the game's menus");
        }
        add_action(out, &n, cap, OB_ID_SEASON_PICK, "Pick a team", NULL);
        if (have) add_action(out, &n, cap, OB_ID_SEASON_SYNC, "Sync league", NULL);
    }
    return n;
}

int ob_season_team_items(const ob_host *h, ob_item *out, int cap)
{
    ob_season *s;
    char lab[96];
    int n = 0, i, k, listed = 0;
    static const int order[] = { OB_SEASON_WAITING, OB_SEASON_FREE, OB_SEASON_OWNED, OB_SEASON_HOSTS };
    if (!h || !h->season || cap < 1) return 0;
    s = h->season;
    if (s->claim >= OB_SEASON_TEAMS) {
        team_label(h, s->claim - OB_SEASON_TEAMS, lab, (int)sizeof lab);
        add_text(out, &n, cap, OB_ID_SEASON_NOTE, "Yours", lab);
        if (s->disk >> (s->claim - OB_SEASON_TEAMS) & 1)
            add_text(out, &n, cap, OB_ID_SEASON_NOTE,
                     h->season_disk_hint ? h->season_disk_hint : "load it from the game's menus", NULL);
    } else if (s->claim >= 0)
        add_text(out, &n, cap, OB_ID_SEASON_NOTE, "Asking the host", NULL);
    if (h->season_note[0]) add_text(out, &n, cap, OB_ID_SEASON_NOTE, h->season_note, NULL);
    if (!s->roster_have) {
        add_text(out, &n, cap, OB_ID_SEASON_NOTE, "Teams", "asking the host");
        return n;
    }
    add_text(out, &n, cap, OB_ID_SEASON_NOTE, "League", s->roster_league[0] ? s->roster_league : "(none)");
    begin_group(out, &n, cap);
    for (k = 0; k < 4; k++)
        for (i = 0; i < OB_SEASON_TEAMS && n < cap - 1; i++) {
            if (s->roster_kind[i] != order[k]) continue;
            /* a name callback returning NULL: that slot is not a team to pick in this game */
            if (order[k] == OB_SEASON_FREE && (!h->season_team_name || !h->season_team_name(h->season_name_ud, i)))
                continue;
            team_who(h, i, s->roster[i], lab, (int)sizeof lab);
            if (order[k] == OB_SEASON_WAITING) add_action(out, &n, cap, OB_ID_SEASON_TEAM + i, lab, NULL);
            else if (order[k] == OB_SEASON_FREE) add_action(out, &n, cap, OB_ID_SEASON_TEAM + i, lab, "new player");
            else add_text(out, &n, cap, OB_ID_SEASON_NOTE, lab, order[k] == OB_SEASON_HOSTS ? "the host's" : "taken");
            listed++;
        }
    if (!listed) add_text(out, &n, cap, OB_ID_SEASON_NOTE, "No teams set up for players yet", NULL);
    return n;
}

int ob_season_apply(ob_host *h, int id, int value)
{
    ob_season *s;
    (void)value;
    if (!h || !h->season) return 0;
    s = h->season;
    if (id == OB_ID_SEASON_SYNC) {
        if (ob_season_sync(s, h->season_host) == 0) note(h, "Syncing league", NULL);
        else note(h, NULL, "Could not sync");
        return 1;
    }
    if (id == OB_ID_SEASON_PICK) {
        if (ob_season_teams(s, h->season_host) == 0) h->season_note[0] = 0;
        else note(h, NULL, "Could not ask who has which team");
        return 1;
    }
    if (id >= OB_ID_SEASON_TEAM && id < OB_ID_SEASON_TEAM + OB_SEASON_TEAMS) {
        int team = id - OB_ID_SEASON_TEAM;
        if (ob_season_claim(s, h->season_host, team) == 0) h->season_note[0] = 0;
        else note(h, NULL, "Could not ask for that team");
        return 1;
    }
    return 0;
}
