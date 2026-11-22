#!/bin/sh
# Netplay security checks (docs/NETPLAY_SECURITY.md). A rule that is only written down comes back, so these fail the build.
#   sh tests/netsec.sh            in Overbar: the library's own checks
#   sh port/overbar/tests/netsec.sh .   in a port: the port's networked sources
# A line that really needs one of the banned calls says "netsec: allow" and why, on the same line.
set -e
here=$(cd "$(dirname "$0")/.." && pwd)
root=${1:-$here}
fail=0
bad() { echo "netsec: $*" >&2; fail=1; }

# 1. Networked code never starts programs, loads code or runs a shell. (Data from a peer reaches these files.)
if [ "$root" = "$here" ]; then files=$(ls "$root"/src/*.c); else
    files=$(ls "$root"/port/netplay.c "$root"/port/season.c "$root"/port/modem.c "$root"/port/sides.c "$root"/port/dos.c \
               "$root"/native/net.c "$root"/native/frontend.c "$root"/platform/netplay.c 2>/dev/null || true); fi
for f in $files; do
    hit=$(grep -nE '\b(system|popen|execl|execlp|execle|execv|execvp|execve|dlopen|LoadLibrary[AW]?|CreateProcess[AW]?|ShellExecute[AW]?|WinExec)[[:space:]]*\(' "$f" | grep -v 'netsec: allow' || true)
    [ -z "$hit" ] || bad "$f starts a program or loads code:
$hit"
done

# 2. Files created from peer data: no path or program names get through (files.c's safe_name), and the test that proves it stays.
if [ "$root" = "$here" ]; then
    grep -q 'safe_name(x->item\[i\].name)' src/files.c || bad "src/files.c: a peer's file names must go through safe_name"
    grep -q '"exe"' src/files.c || bad "src/files.c: safe_name must refuse program extensions"
    # 3. A shared state is put back by a jump: the peer must not choose the landing point (rollback.c) and the test must tamper with it.
    grep -q "not this game's resumable point" src/rollback.c || bad "src/rollback.c: rb_take must check the state's jump point against this machine's own"
    grep -q 'reaches outside the game thread' src/rollback.c || bad "src/rollback.c: rb_take must keep the state's stack inside the game thread's"
    grep -q 'jump point, or stack, is refused' tests/test_snap.c || bad "tests/test_snap.c: the tampered shared state test is missing"
    # 4. Every length from a packet is checked before it sizes or indexes anything: the readers stay the bounds-checked ones.
    grep -q 'i->bad = 1' src/net.c || bad "src/net.c: the packet readers must set bad when a read runs past the end"
fi

# 5. Ports that run the other player's whole emulated machine (94 PC: port/machine_state.c): the emulated DOS' writes for it: program and script files stay refused.
[ ! -f "$root/port/machine_state.c" ] || grep -q 'write_ok(' "$root/port/dos.c" || bad "port/dos.c: the emulated DOS must keep refusing to write program files (write_ok)"

[ "$fail" = 0 ] && echo "netsec ok"
exit $fail
