/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * Copyright (c) 2026 abdulahmad
 *
 * Shared league or campaign (the "online season"): copies kept in step by the *game's* export, import
 * and merge. For any title, on any platform, that keeps a master copy on one machine and a copy per
 * player. "Team" here is that game's own unit (a franchise, a save slot, a character). Overbar carries
 * the files and the room; the port's hooks are the game's routines. A DOS port can point the game at a
 * virtual drive (overbar/vdrive.h) so it never asks for a disk.
 *
 * Sync (the room's host holds the master): a guest that joins, or that changed its unit, calls
 * ob_season_sync. It says which league and teams it has, and the games it played on its copy away from
 * the master (ob_season_played). The host checks the league and who owns each team (the first player to
 * bring a team owns it), and the guest exports each of its teams (the game's export into a folder) and
 * sends it (overbar/files.h). The host merges every team of that guest, then makes a fresh copy for each
 * and sends it back, and the guest imports them. A game played away from the master is handed to the
 * merge once: the host keeps the keys of those merged.
 *
 * A match away from the master (C vs D with the host away): the game needs both players' copies, and it
 * writes the result into both, each side's stats into its own copy. So each player of the match sends
 * its copy to the other machines as a mirror (ob_season_mirror_send); every machine plays from the same
 * two copies (its own where it has one, the mirror of the other), and drops the mirrors after, keeping
 * none of their writes. Both players' copies then have the game played, so neither can play it again,
 * and each marks it (ob_season_played). Its result reaches the master at each one's next sync, one team
 * from each copy.
 *
 * A unit made this player's on another machine is ob_season_give: the copy goes to that machine and its
 * game imports it. The host can do it all, or a player in the room picks one (ob_season_claim): the
 * host's game adds them (add_human) and gives a first copy. ob_season_teams asks the host who has which
 * team, for a picking screen. A team the host's game set up for a player on another computer (the original's
 * export to floppy, ops.player) shows to guests as "Edmonton - Bob"; picking it gives that copy, and with
 * ops.give_as_disk it waits on the guest as a disk (ob_season_disk) that the game's own menus load. No file name
 * is ever shown or typed.
 *
 * A team as a file (a player away from the room): ob_season_pack writes a team's export (with the games
 * played away) into one file; the host takes it (ob_season_take_pack: the merge, as a sync does it) and
 * writes a fresh copy into a file back, which the player imports (ob_season_import_pack). A first copy
 * can go the same way (ob_season_pack_copy on the host).
 *
 * Messages are of type OB_NET_TYPE_SEASON (ob_room_modules routes them, and the files'). Each machine
 * keeps a small ledger in its work folder (owners and merged games on the master; games played away
 * elsewhere). No SDL. */
#ifndef OVERBAR_SEASON_H
#define OVERBAR_SEASON_H

#include <stddef.h>
#include <stdint.h>
#include "overbar/files.h"
#include "overbar/net.h"

#ifdef __cplusplus
extern "C" {
#endif

#define OB_SEASON_TEXT 31
#define OB_SEASON_TEAMS 32              /* team numbers 0 .. 31 */
#define OB_SEASON_PLAYED 64             /* games played away from the master and not merged yet, kept */
#define OB_SEASON_MERGED 1024           /* keys of merged games the master keeps */
#define OB_SEASON_PATH 260

typedef struct ob_season_league {
    char name[OB_SEASON_TEXT + 1];      /* the league's name */
    char id[OB_SEASON_TEXT + 1];        /* what tells it from another league of the same name */
    int master;                         /* this machine has the master copy */
    int team[OB_SEASON_TEAMS];          /* (a copy) the human teams it is for */
    int nteam;
} ob_season_league;

typedef struct ob_season_ops {
    void *ud;
    /* the league this machine plays now: 0, or -1 none */
    int (*league)(void *ud, ob_season_league *l);
    /* (a guest) the game's export of its team into dir: 0, or -1 */
    int (*export_team)(void *ud, int team, const char *dir);
    /* (the master) the game's merge of team's copy in dir. played: the games played on that copy away from the
     * master whose result for this team is not merged yet (a key is handed once for each team): 0, or -1 */
    int (*merge_team)(void *ud, int team, const char *dir, const uint32_t *played, int nplayed);
    /* (the master) a fresh copy for team into dir (the game's copy for a human team): 0, or -1 */
    int (*copy_team)(void *ud, int team, const char *dir);
    /* (a guest) the game's import of the master's copy for team from dir: 0, or -1 */
    int (*import_team)(void *ud, int team, const char *dir);
    /* the folder of this machine's league, for a mirror: 0, or -1 */
    int (*league_dir)(void *ud, char *dir, int size);
    /* (the master) make this slot this player's, for the player name (no questions on the host's
     * screen): 0, or -1 (NULL: players cannot pick; the host adds them) */
    int (*add_human)(void *ud, int team, const char *name);
    /* (the master) the player this human team was set up for on another computer (the game's own "export to
     * floppy" for that player): "Bob", or NULL (a computer team, or one played here). Such a team shows as
     * "Edmonton - Bob" when a guest picks a team, and picking it gives that copy with no question on the host */
    const char *(*player)(void *ud, int team);
    /* (a guest) 1: a first copy is not imported but kept as this player's disk (ob_season_disk) for the game's own
     * menus to load, as the original's floppy was; 0: imported by import_team */
    int give_as_disk;
} ob_season_ops;

enum {                                  /* ob_season.roster_kind: a team ... */
    OB_SEASON_FREE,                     /* a computer team (picking it asks the host's game to add a player) */
    OB_SEASON_HOSTS,                    /* human, played on the host */
    OB_SEASON_OWNED,                    /* a player has it (roster[]: who) */
    OB_SEASON_WAITING                   /* set up for a player who has not picked it yet (roster[]: that player) */
};

enum {                                  /* ob_season.state: a guest's sync */
    OB_SEASON_IDLE, OB_SEASON_ASKED, OB_SEASON_SENDING, OB_SEASON_TAKING, OB_SEASON_SYNCED, OB_SEASON_FAILED
};

typedef struct ob_season_peer {         /* (the master) a guest's sync */
    int state;                          /* 0 none, 1 its teams coming, 2 copies going back */
    char name[OB_NET_NAME + 1];
    int team[OB_SEASON_TEAMS], nteam;
    unsigned came, went;                /* by team: its export came; its copy was taken */
    uint32_t played[OB_SEASON_PLAYED];
    int nplayed;
} ob_season_peer;

typedef struct ob_season {
    ob_season_ops ops;
    ob_files *files;
    void (*send)(void *ud, int to, const void *data, size_t size);
    void *send_ud;
    char base[OB_SEASON_PATH];          /* the work folder: base/L<league>/..., base/mirror */
    char me[OB_NET_NAME + 1];           /* this machine's player name */
    /* a guest's sync */
    int state;
    int host;                           /* the machine it syncs with */
    int team[OB_SEASON_TEAMS], nteam;   /* the teams the master took */
    unsigned taken;                     /* by team: its copy imported */
    int sent_played;                    /* how many of played[] went with the sync */
    /* the ledger of this machine's league */
    ob_season_league league;
    char owner[OB_SEASON_TEAMS][OB_NET_NAME + 1];   /* (the master) who owns each team ("" nobody yet) */
    uint32_t merged[OB_SEASON_MERGED];  /* (the master) keys of games merged from copies ... */
    signed char merged_team[OB_SEASON_MERGED];  /* ... and for which team (-1: any, an older ledger) */
    int nmerged;
    uint32_t took[OB_SEASON_TEAMS];     /* (the master) by team: the hash of the last team's file taken (0 none) */
    uint32_t played[OB_SEASON_PLAYED];  /* (a copy) games played on it away from the master, not merged yet */
    int nplayed;
    ob_season_peer peer[OB_NET_MAX_MACHINES];
    /* a match away from the master */
    unsigned given;                     /* (a guest) by team: a first copy taken (ob_season_give) */
    unsigned mirror_to, mirror_ok, mirror_bad;      /* (a player of the match) machines sent its copy, taken, failed */
    unsigned mirror_have;               /* by machine: its copy is here as a mirror */
    /* (a guest) picking a team */
    int claim;                          /* the team asked for (-1 none); OB_SEASON_TEAMS + team: given */
    int roster_have;                    /* the host's list came */
    char roster_league[OB_SEASON_TEXT + 1];         /* the host's league */
    int roster_kind[OB_SEASON_TEAMS];   /* OB_SEASON_FREE, _HOSTS, _OWNED, _WAITING */
    char roster[OB_SEASON_TEAMS][OB_NET_NAME + 1];  /* (_OWNED, _WAITING) its player */
    unsigned disk;                      /* (a guest, give_as_disk) by team: its first copy is here as a disk */
    int disk_team;                      /* the last team given as a disk (-1 none) */
    char why[160];                      /* the last failure ("" none) */
    unsigned changes;                   /* bumped when the roster, a claim or a disk changes (screens redraw) */
    unsigned asked, answered;           /* (a guest) league lists asked for; (the host) lists sent */
    unsigned send_failed;               /* messages the client's send could not pass on (it counts them) */
} ob_season;

/* base: Overbar's season folder (made if missing). files: the room's (its hooks become the season's). send:
 * a message of type OB_NET_TYPE_SEASON to a machine. me: this machine's player name */
void ob_season_init(ob_season *s, const ob_season_ops *ops, const char *base, const char *me, ob_files *files,
                    void (*send)(void *ud, int to, const void *data, size_t size), void *ud);
void ob_season_free(ob_season *s);
/* (a guest) syncs its league with the master on machine host: 0, or -1 (no league, the master is here) */
int ob_season_sync(ob_season *s, int host);
/* a message of type OB_NET_TYPE_SEASON */
void ob_season_message(ob_season *s, int from, const void *data, size_t size);
void ob_season_machine_gone(ob_season *s, int machine);
/* one line for the room page ("Syncing league with Ben"; "" nothing to say) */
void ob_season_status(const ob_season *s, char *line, int size);
/* syncs going on (the master: with any guest; a guest: its own) */
int ob_season_busy(const ob_season *s);

/* (the master) a team becomes human on a guest's machine: its first copy (the game's copy for a human team) goes
 * to machine, whose player (name) owns the team from now on, and its game imports it (making the league there): 0, -1 */
int ob_season_give(ob_season *s, int machine, const char *name, int team);

/* (a guest) asks the host on machine host who has which team (roster_have, roster_kind[], roster[] when it comes) */
int ob_season_teams(ob_season *s, int host);
/* (a guest) asks the host for team as this player's: the host's game makes it human and its first copy comes and
 * is imported (claim becomes OB_SEASON_TEAMS + team; on a refusal claim is -1 and why says so): 0, or -1 */
int ob_season_claim(ob_season *s, int host, int team);

/* (the master) who has which team, as a guest's roster has it: kind[] OB_SEASON_*, who[] the player: 0, or -1 */
int ob_season_roster(ob_season *s, int kind[OB_SEASON_TEAMS], char who[OB_SEASON_TEAMS][OB_NET_NAME + 1]);
/* (a guest, give_as_disk) the folder holding team's disk (team -1: the last given): the team, or -1 none */
int ob_season_disk(const ob_season *s, int team, char *dir, int size);

/* (a guest) team's export, with the games played away, into the file path: 0, or -1 */
int ob_season_pack(ob_season *s, int team, const char *path);
/* (the master) a player's file (ob_season_pack): merged as a sync does it, and a fresh copy for the team into the
 * file back: 0, or -1 (why says why: another league, a team that is someone else's, a damaged file) */
int ob_season_take_pack(ob_season *s, const char *path, const char *back);
/* (the master) team's first copy, for player name, into the file path (ob_season_give without the room): 0, -1 */
int ob_season_pack_copy(ob_season *s, const char *name, int team, const char *path);
/* (a guest) the host's file (ob_season_take_pack's back, or ob_season_pack_copy): imported: 0, or -1 */
int ob_season_import_pack(ob_season *s, const char *path);

/* a league game played on this copy away from the master (each player of such a match), kept till merged */
void ob_season_played(ob_season *s, uint32_t key);
/* (each player of a match away from the master) its league copy to the machines in mask: 0, or -1 */
int ob_season_mirror_send(ob_season *s, unsigned machines);
/* (a player) 1 every machine has taken its copy, 0 not yet, -1 one failed */
int ob_season_mirror_sent(const ob_season *s);
/* machine from's copy is in dir (a folder under Overbar's mirror folder): 1, 0 not (yet) */
int ob_season_mirror(const ob_season *s, int from, char *dir, int size);
/* after the match: every mirror goes, its writes with it */
void ob_season_mirror_drop(ob_season *s);

#ifdef __cplusplus
}
#endif

#endif
