/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * Copyright (c) 2026 abdulahmad
 *
 * The Season page: overlay and File menu for a shared league (overbar/season.h). Overbar draws
 * Sync, Pick a team, and a team as a file; the game's export, import and merge stay the client's
 * hooks. Files are .obt in the folder the client names. Any game with a master copy and per-player
 * copies can attach this page.
 */
#include "overbar/overbar.h"
#include "overbar/season.h"
#include "fsdir.h"

#include <stdio.h>
#include <string.h>

#define FILES_MAX 32

static int ends_obt(const char *n)
{
    int i;
    if (!n) return 0;
    i = (int)strlen(n);
    return i >= 4 && (n[i - 4] == '.') &&
           (n[i - 3] == 'o' || n[i - 3] == 'O') &&
           (n[i - 2] == 'b' || n[i - 2] == 'B') &&
           (n[i - 1] == 't' || n[i - 1] == 'T');
}

static const char *file_name(const ob_host *h)
{
    return h->season_file[0] ? h->season_file : "team.obt";
}

static void file_path(const ob_host *h, char *out, int size)
{
    const char *f = file_name(h);
    if (h->season_dir[0]) ob_fs_join(out, (size_t)size, h->season_dir, f);
    else snprintf(out, (size_t)size, "%s", f);
}

static void back_path(const ob_host *h, char *out, int size)
{
    char tmp[300], *dot;
    file_path(h, tmp, (int)sizeof tmp);
    snprintf(out, (size_t)size, "%s", tmp);
    dot = strrchr(out, '.');
    if (dot && !strchr(dot, '/') && !strchr(dot, '\\')) snprintf(dot, (size_t)(out + size - dot), "-back.obt");
    else {
        size_t n = strlen(out);
        snprintf(out + n, (size_t)size - n, "-back.obt");
    }
}

static void team_label(const ob_host *h, int team, char *buf, int size)
{
    const char *n = h->season_team_name ? h->season_team_name(h->season_name_ud, team) : NULL;
    if (n && n[0]) snprintf(buf, (size_t)size, "%s", n);
    else snprintf(buf, (size_t)size, "Team %d", team);
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
    it->label = label;
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

static void add_field(ob_item *out, int *n, int cap, int id, const char *label, const char *text)
{
    ob_item *it = add(out, n, cap, id, MDRT_ITEM_FIELD, label);
    if (it) snprintf(it->detail, sizeof it->detail, "%s", text ? text : "");
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
    if (dir && dir[0]) {
        snprintf(h->season_dir, sizeof h->season_dir, "%s", dir);
        ob_fs_mkdir(h->season_dir);
    } else
        h->season_dir[0] = 0;
    if (!h->season_file[0]) snprintf(h->season_file, sizeof h->season_file, "%s", "team.obt");
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
    char line[160], who[96], lab[48];
    int n = 0, master = 0, i, have;
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
        add_text(out, &n, cap, OB_ID_SEASON_NOTE, master ? "Master league" : "League copy", l.name);
        if (l.nteam > 0) {
            for (i = 0; i < l.nteam && i < 4; i++) {
                char bit[40];
                team_label(h, l.team[i], bit, (int)sizeof bit);
                if (i == 0) snprintf(who, sizeof who, "%s", bit);
                else {
                    size_t used = strlen(who);
                    snprintf(who + used, sizeof who - used, ", %s", bit);
                }
            }
            if (l.nteam > 4) {
                size_t used = strlen(who);
                snprintf(who + used, sizeof who - used, ", +%d", l.nteam - 4);
            }
            add_text(out, &n, cap, OB_ID_SEASON_NOTE, "Your teams", who);
        }
    } else
        add_text(out, &n, cap, OB_ID_SEASON_NOTE, "League", "none");
    ob_season_status(s, line, (int)sizeof line);
    if (line[0]) add_text(out, &n, cap, OB_ID_SEASON_NOTE, "Status", line);
    if (h->season_note[0]) add_text(out, &n, cap, OB_ID_SEASON_NOTE, h->season_note, NULL);
    begin_group(out, &n, cap);
    if (!master) {
        add_action(out, &n, cap, OB_ID_SEASON_SYNC, "Sync league", have ? NULL : "no league");
        add_action(out, &n, cap, OB_ID_SEASON_PICK, "Pick a team", NULL);
    } else {
        int shown = 0;
        for (i = 0; i < OB_SEASON_TEAMS; i++) {
            if (!s->owner[i][0]) continue;
            team_label(h, i, lab, (int)sizeof lab);
            add_text(out, &n, cap, OB_ID_SEASON_NOTE, lab, s->owner[i]);
            shown++;
            if (shown >= 8) break;
        }
        if (!shown) add_text(out, &n, cap, OB_ID_SEASON_NOTE, "Human teams", "none yet");
    }
    begin_group(out, &n, cap);
    add_field(out, &n, cap, OB_ID_SEASON_FILE, "File", file_name(h));
    if (!master) {
        add_action(out, &n, cap, OB_ID_SEASON_EXPORT, "Export team as a file", file_name(h));
        add_action(out, &n, cap, OB_ID_SEASON_IMPORT, "Import a team file", file_name(h));
    } else {
        add_action(out, &n, cap, OB_ID_SEASON_TAKE, "Take a team file", file_name(h));
        add_action(out, &n, cap, OB_ID_SEASON_COPY, "Write a first copy", file_name(h));
    }
    add_action(out, &n, cap, OB_ID_SEASON_FILES, "Team files", h->season_dir[0] ? h->season_dir : ".");
    return n;
}

int ob_season_team_items(const ob_host *h, ob_item *out, int cap)
{
    ob_season *s;
    char lab[48], det[96];
    int n = 0, i;
    if (!h || !h->season || cap < 1) return 0;
    s = h->season;
    if (!s->roster_have)
        add_text(out, &n, cap, OB_ID_SEASON_NOTE, "Teams", "asking the host");
    else {
        add_text(out, &n, cap, OB_ID_SEASON_NOTE, "League",
                 s->roster_league[0] ? s->roster_league : "(none)");
        for (i = 0; i < OB_SEASON_TEAMS && n < cap - 1; i++) {
            /* A name callback returning NULL means that slot is not a team in this game. */
            if (s->roster_kind[i] == OB_SEASON_FREE && h->season_team_name
                && !h->season_team_name(h->season_name_ud, i))
                continue;
            team_label(h, i, lab, (int)sizeof lab);
            if (s->roster_kind[i] == OB_SEASON_FREE)
                add_action(out, &n, cap, OB_ID_SEASON_TEAM + i, lab, "free");
            else if (s->roster_kind[i] == OB_SEASON_HOSTS)
                add_text(out, &n, cap, OB_ID_SEASON_NOTE, lab, "the host's");
            else if (s->roster_kind[i] == OB_SEASON_OWNED) {
                snprintf(det, sizeof det, "%s's", s->roster[i]);
                add_text(out, &n, cap, OB_ID_SEASON_NOTE, lab, det);
            }
        }
    }
    if (h->season_note[0]) add_text(out, &n, cap, OB_ID_SEASON_NOTE, h->season_note, NULL);
    if (s->claim >= OB_SEASON_TEAMS) {
        team_label(h, s->claim - OB_SEASON_TEAMS, lab, (int)sizeof lab);
        add_text(out, &n, cap, OB_ID_SEASON_NOTE, "Yours", lab);
    }
    return n;
}

int ob_season_file_items(const ob_host *h, ob_item *out, int cap)
{
    char names[FILES_MAX][OB_FS_NAME];
    const char *dir;
    int n = 0, k, i, listed = 0;
    if (!h || cap < 1) return 0;
    dir = h->season_dir[0] ? h->season_dir : ".";
    add_text(out, &n, cap, OB_ID_SEASON_NOTE, "Folder", dir);
    k = ob_fs_list(dir, names, FILES_MAX);
    if (k < 0)
        add_text(out, &n, cap, OB_ID_SEASON_NOTE, "Team files", "no folder");
    else {
        for (i = 0; i < k && n < cap - 1; i++) {
            if (!ends_obt(names[i])) continue;
            add_action(out, &n, cap, OB_ID_SEASON_PACK + listed, names[i], names[i]);
            listed++;
        }
        if (!listed) add_text(out, &n, cap, OB_ID_SEASON_NOTE, "Team files", "none yet");
    }
    if (h->season_note[0]) add_text(out, &n, cap, OB_ID_SEASON_NOTE, h->season_note, NULL);
    return n;
}

static int export_team_of(const ob_host *h)
{
    ob_season_league l;
    int t = h->season_team;
    memset(&l, 0, sizeof l);
    if (h->season && h->season->ops.league) h->season->ops.league(h->season->ops.ud, &l);
    if (l.nteam <= 0) return t;
    {
        int i;
        for (i = 0; i < l.nteam; i++) if (l.team[i] == t) return t;
    }
    return l.team[0];
}

static int do_pack_file(ob_host *h, int id)
{
    ob_season *s = h->season;
    char path[300], back[300];
    int team, r;
    if (h->season_dir[0]) ob_fs_mkdir(h->season_dir);
    file_path(h, path, (int)sizeof path);
    back_path(h, back, (int)sizeof back);
    team = export_team_of(h);
    if (id == OB_ID_SEASON_EXPORT) {
        r = ob_season_pack(s, team, path);
        note(h, r == 0 ? "Exported the team as a file" : NULL, "Export failed");
        return 1;
    }
    if (id == OB_ID_SEASON_IMPORT) {
        r = ob_season_import_pack(s, path);
        note(h, r == 0 ? "Imported the host's copy" : NULL, "Import failed");
        return 1;
    }
    if (id == OB_ID_SEASON_TAKE) {
        r = ob_season_take_pack(s, path, back);
        note(h, r == 0 ? "Took the file; a copy is waiting" : NULL, "Take failed");
        return 1;
    }
    if (id == OB_ID_SEASON_COPY) {
        const char *who = h->online.name[0] ? h->online.name : (s->me[0] ? s->me : "player");
        r = ob_season_pack_copy(s, who, team, path);
        note(h, r == 0 ? "Wrote a first copy" : NULL, "First copy failed");
        return 1;
    }
    return 0;
}

int ob_season_apply(ob_host *h, int id, int value)
{
    ob_season *s;
    (void)value;
    if (!h || !h->season) return 0;
    s = h->season;
    if (id == OB_ID_SEASON_FILE) return 1;
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
    if (id == OB_ID_SEASON_FILES) return 1;
    if (id >= OB_ID_SEASON_TEAM && id < OB_ID_SEASON_TEAM + OB_SEASON_TEAMS) {
        int team = id - OB_ID_SEASON_TEAM;
        h->season_team = team;
        if (ob_season_claim(s, h->season_host, team) == 0) note(h, "Asked for that team", NULL);
        else note(h, NULL, "Could not ask for that team");
        return 1;
    }
    if (id >= OB_ID_SEASON_PACK && id < OB_ID_SEASON_PACK + FILES_MAX) {
        char names[FILES_MAX][OB_FS_NAME];
        const char *dir = h->season_dir[0] ? h->season_dir : ".";
        int k = ob_fs_list(dir, names, FILES_MAX), i, listed = 0, want = id - OB_ID_SEASON_PACK;
        ob_season_league l;
        int master = 0;
        memset(&l, 0, sizeof l);
        if (s->ops.league && s->ops.league(s->ops.ud, &l) == 0) master = l.master;
        for (i = 0; i < k; i++) {
            if (!ends_obt(names[i])) continue;
            if (listed == want) {
                snprintf(h->season_file, sizeof h->season_file, "%s", names[i]);
                return do_pack_file(h, master ? OB_ID_SEASON_TAKE : OB_ID_SEASON_IMPORT);
            }
            listed++;
        }
        note(h, NULL, "That file is gone");
        return 1;
    }
    if (id == OB_ID_SEASON_EXPORT || id == OB_ID_SEASON_IMPORT || id == OB_ID_SEASON_TAKE || id == OB_ID_SEASON_COPY)
        return do_pack_file(h, id);
    return 0;
}
