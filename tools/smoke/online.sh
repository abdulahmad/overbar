#!/bin/sh
# Overbar port two-copy online smoke run (95 PC for now): a host and a guest on one machine over loopback UDP, each from its own copy of the game
# folder and its own exe copy, both driven by the port's test hooks (no keystrokes). Part of the smoke-test plan (project files smoke-test/plan.md).
# On Windows the first run of a new exe path makes Windows Firewall ask about UDP: tell the person at the machine before running it.
#
#   tools/smoke/online.sh PORT EXE HOSTDIR GUESTDIR [SECONDS] [NETSIM]
#     PORT     95
#     EXE      the built port
#     HOSTDIR, GUESTDIR  two separate COPIES of the game folder (they write league and settings files there)
#     SECONDS  default 90
#     NETSIM   "delay,jitter,loss%" for a bad link, default "40,10,2"; env NHL95_PORT picks the UDP port (default 41957, not the game's own 41950)
#
# Passes when both copies were still running at the end, wrote no crash.log, both reached at least MIN_STEPS (default 600) steps of the
# rolled-back session, and neither log says desync, resync or that the room ended. Exit 0 pass, 1 fail; logs in $SMOKE_OUT/online/.
set -u
port=${1:?usage: online.sh PORT EXE HOSTDIR GUESTDIR [SECONDS] [NETSIM]}
exe=${2:?exe}; hdir=${3:?hostdir}; gdir=${4:?guestdir}
secs=${5:-90}; netsim=${6:-40,10,2}
min_steps=${MIN_STEPS:-600}
[ "$port" = 95 ] || { echo "FAIL online test is only wired for 95 PC so far"; exit 1; }
out=${SMOKE_OUT:-./smoke-out}/online
rm -rf "$out"; mkdir -p "$out"; out=$(cd "$out" && pwd)
ext=${exe##*.}; [ "$ext" = "$exe" ] && ext=
run_one() {   # role dir
  role=$1; dir=$2
  cp "$exe" "$dir/smoke-95-$role${ext:+.$ext}" || exit 1
  [ -f "$(dirname "$exe")/SDL2.dll" ] && cp "$(dirname "$exe")/SDL2.dll" "$dir/" 2>/dev/null
  rm -f "$dir/crash.log"
  ( cd "$dir" && SDL_VIDEODRIVER=${SDL_VIDEODRIVER:-dummy} SDL_AUDIODRIVER=${SDL_AUDIODRIVER:-dummy} NHL95_ROOT="$dir" NHL95_NOGL=1 NHL95_AUTOSKIP=1 \
    NHL95_AUTOPAD=1 NHL95_ONLINE=$role NHL95_PORT=${NHL95_PORT:-41957} NHL95_NETSIM="$netsim" exec "./smoke-95-$role${ext:+.$ext}" ) >"$out/$role.log" 2>&1 &
  echo $!
}
unset ULTRASND ULTRADIR BLASTER
hp=$(run_one host "$hdir"); sleep 3; gp=$(run_one join "$gdir")
i=0; hal=1; gal=1
while [ $i -lt "$secs" ]; do
  sleep 1; i=$((i+1))
  kill -0 "$hp" 2>/dev/null || hal=0; kill -0 "$gp" 2>/dev/null || gal=0
  [ $hal = 1 ] && [ $gal = 1 ] || break
done
for p in "$hp" "$gp"; do kill "$p" 2>/dev/null; done; sleep 1; for p in "$hp" "$gp"; do kill -9 "$p" 2>/dev/null; wait "$p" 2>/dev/null; done
rm -f "$hdir/smoke-95-host${ext:+.$ext}" "$gdir/smoke-95-join${ext:+.$ext}"
fails=""
[ $hal = 1 ] || fails="$fails host-exited(after ${i}s)"; [ $gal = 1 ] || fails="$fails guest-exited(after ${i}s)"
for d in "$hdir" "$gdir"; do [ -f "$d/crash.log" ] && { cp "$d/crash.log" "$out/crash-$(basename "$(dirname "$d")").log"; rm -f "$d/crash.log"; fails="$fails crash.log"; }; done
for role in host join; do
  steps=$(sed -n 's/^rollback: at [0-9.]* s, step \([0-9]*\),.*/\1/p' "$out/$role.log" | tail -1)
  [ "${steps:-0}" -ge "$min_steps" ] || fails="$fails $role-only-${steps:-0}-steps(need $min_steps)"
  grep -qiE 'desync|resync|room ends: [^ ]' "$out/$role.log" && fails="$fails $role-log-says-desync-or-room-ended"
done
if [ -z "$fails" ]; then echo "PASS online port=$port netsim=$netsim seconds=$i"; exit 0; fi
echo "FAIL online port=$port:$fails (details in $out)"; exit 1
