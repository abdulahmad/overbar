#!/bin/sh
# Overbar port smoke run: start a built port headless, let it play by its own test hooks, and judge it.
# Part of the smoke-test plan (project files smoke-test/plan.md). No keystrokes or clicks are sent from outside, so it is safe on a
# machine someone is using; every run has its own scratch folder and exe copy, and it only ever stops the process it started.
#
#   tools/smoke/run.sh PORT EXE GAMEDIR [SECONDS] [MODE]
#     PORT     95 | 96 | 97 | 94 (picks the env prefix NHL<PORT>_ and the skip hook)
#     EXE      the built port (hockey-win32.exe, hockey97-win32.exe, hockey94.exe, or a Linux build)
#     GAMEDIR  a COPY of the game's data folder (the game writes league and settings files there)
#     SECONDS  how long to let it run (default 60)
#     env NHL95_CD: 95 PC needs the folder holding its CD files (a scratch copy of the game folder usually leaves them out)
#     MODE     boot (reach the front end) | game (also start an exhibition game, 95 only for now). Default boot.
#
# Passes when the process was still running at the end (it did not exit or crash), there is no crash.log, the screen changed at
# least MIN_FRAMES times (NHL<PORT>_DUMP writes a picture each time it does) and the last picture is not blank.
# Exit 0 pass, 1 fail. A report line goes to stdout, the details to $SMOKE_OUT (default ./smoke-out/PORT).
set -u
port=${1:?usage: run.sh PORT EXE GAMEDIR [SECONDS] [boot|game]}
exe=${2:?exe}
gamedir=${3:?gamedir}
secs=${4:-60}
mode=${5:-boot}
# a 95 PC front end sits still without input, so a boot run only has to show the screen changed (it did, twice, in the first seconds)
def=8; [ "$port" = 95 ] && [ "${5:-boot}" = boot ] && def=2
min_frames=${MIN_FRAMES:-$def}
out=${SMOKE_OUT:-./smoke-out}/$port
rm -rf "$out"; mkdir -p "$out/frames"
out=$(cd "$out" && pwd)
case $port in 94|95|96|97) ;; *) echo "FAIL port $port: unknown"; exit 1;; esac
P=NHL$port
# a separate exe name so a running game of the same name is never touched or clashed with
ext=${exe##*.}; [ "$ext" = "$exe" ] && ext=
runexe="smoke-$port${ext:+.$ext}"
cp "$exe" "$gamedir/$runexe" || { echo "FAIL port $port: cannot copy exe"; exit 1; }
[ -f "$(dirname "$exe")/SDL2.dll" ] && cp "$(dirname "$exe")/SDL2.dll" "$gamedir/" 2>/dev/null
rm -f "$gamedir/crash.log"
export SDL_VIDEODRIVER=${SDL_VIDEODRIVER:-dummy} SDL_AUDIODRIVER=${SDL_AUDIODRIVER:-dummy}
# no sound card at all: a Gravis UltraSound (ULTRASND, ULTRADIR) set in the user's environment makes the game look for patch files
# (C:\ULTRASND\MIDI) that a scratch copy of the game folder does not have, and it reports them missing
unset ULTRASND ULTRADIR BLASTER
# 95 PC: the game folder's SND.CFG picks the card (Ultrasound, 0x02, on a normal install) and with NHL95_SOUND=none the port emulates no
# card at all, so the game stops at "Could not find a ULTRASOUND card". Let 95 have its emulated cards; the audio driver is dummy anyway.
[ "$port" = 95 ] || export ${P}_SOUND=none
export ${P}_ROOT="$gamedir" ${P}_DUMP="$out/frames" ${P}_NOGL=1 OVERBAR_TEST_LOG="$out/overbar.log"
case $port in
  95) [ "$mode" = game ] && export NHL95_AUTOSKIP=1 || export NHL95_KEYS="";;
  96|97) export ${P}_SKIPINTRO=1;;
esac
( cd "$gamedir" && exec "./$runexe" ) >"$out/stdout.txt" 2>"$out/stderr.txt" &
pid=$!
i=0; alive=1
while [ $i -lt "$secs" ]; do
  sleep 1; i=$((i+1))
  kill -0 $pid 2>/dev/null || { alive=0; break; }
done
if [ $alive = 1 ]; then kill $pid 2>/dev/null; sleep 1; kill -9 $pid 2>/dev/null; fi
wait $pid 2>/dev/null
rm -f "$gamedir/$runexe"
fails=""
[ $alive = 1 ] || fails="$fails exited-early(after ${i}s)"
if [ -f "$gamedir/crash.log" ]; then mv "$gamedir/crash.log" "$out/crash.log"; fails="$fails crash.log"; fi
frames=$(ls "$out/frames" 2>/dev/null | wc -l | tr -d ' ')
[ "$frames" -ge "$min_frames" ] || fails="$fails only-$frames-frames(need $min_frames)"
last=$(ls "$out/frames" 2>/dev/null | tail -1)
if [ -n "$last" ]; then
  distinct=$(tail -c +16 "$out/frames/$last" | od -An -tu1 -w1 | sort -u | wc -l | tr -d ' ')
  [ "$distinct" -gt 3 ] || fails="$fails last-frame-blank"
fi
if [ -z "$fails" ]; then echo "PASS port=$port mode=$mode frames=$frames seconds=$i"; exit 0; fi
echo "FAIL port=$port mode=$mode frames=$frames:$fails (details in $out)"
exit 1
