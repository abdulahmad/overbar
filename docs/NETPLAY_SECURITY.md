# Netplay security rules

Online play runs other people's data inside your game: packets, the host's settings, a saved state a joining player takes, files, chat
and names. Treat all of it as hostile, even from a friend (their machine may not be theirs). These rules apply to Overbar and to every
port that uses it (94, 95, 96, 97 PC, NHL94Genesis-C-Port, NHL94SNES-C-Port). `tests/netsec.sh` checks what a program can check; the
rest is for whoever writes or reviews the change.

What went wrong once (2026-11-22): a saved state taken from the host carried the address the game jumps back to, so a malicious host
could have chosen where the guest's game runs code. In 94 PC the host's whole emulated machine is the state, so its code and every index
in it were the host's, and the emulated DOS could have written program files on the guest's disk. See the fixes in Overbar
`src/rollback.c` (rb_take), `src/files.c` (safe_name) and 94 PC `port/netplay.c` / `port/machine_state.c` (machine_sanitize).

## The rules

1. **Never run, load or write code from network data.** No `system`, `popen`, `exec*`, `dlopen`, `LoadLibrary`, `ShellExecute` in
   networked code. A file made from a peer's data never has a program or script name (exe, dll, bat, sh, ...). `netsec.sh` fails on these.
2. **A peer never chooses an address.** Anything put back by a jump, a function pointer, a return address, a stack pointer or a base
   address comes from this machine, not from a state or packet. `rb_take` takes none of the host's jump buffer except stack positions it
   then checks lie inside the stack put back.
3. **Check every length, count and offset before it sizes or indexes anything.** Compare as `len > size - at` (never `at + len > size`,
   which wraps on 32-bit builds). Use the bounds-checked readers in `net.c` (`g8`, `gbytes`) and the files module's (`get8`, `getstr`);
   don't read a packet with a pointer cast. Cap every size a peer states (OB_NET_MAX_MESSAGE, OB_NET_MAX_SETTINGS) before allocating.
4. **Validate a state before it is applied, and clean it after.** A state from another machine is checked for size, layout and
   addresses first (see `rb_take`), then every index, count, string and pointer-like value inside it is made safe to use
   (`machine_sanitize` in 94 PC). A new variable that goes into a state and is used as an index needs a line in that sanitize.
5. **A peer's names are text, not paths.** File, folder, team and player names from a peer are cut to printable ASCII; a file name
   goes through `safe_name` (no `/ \ :`, no leading dot, no control characters, no device names like CON or NUL, no trailing dot or space,
   no program extension). Folders come from this machine's own tables (the tag is a number checked against a range), never from the packet.
6. **Only the host may do what only the host may do.** Host-only messages (the match, the state, console reset/load) are accepted
   from machine 0 only; host-only pad bits are cleared on a guest's buttons (`host_bits`). A guest's claimed sender is checked against
   the address it came from.
7. **A peer can waste its own time, not yours.** No unbounded allocation, loop or queue per packet; acknowledgements beyond what was
   sent are ignored; one machine cannot hold more than its share of memory.
8. **Emulated machines are sandboxes: keep them.** Where the other player's code runs in an emulator (94 PC), every host-side effect
   of that code is bounded: memory pages inside their pool, file writes refused for program files (`write_ok`), nothing that starts a
   process.
9. **A new network path gets a hostile test.** A new message type, state field or file route comes with a test that feeds it a
   too-long, too-short, out-of-range and tampered value (see `tests/test_snap.c` for the jump point, `tests/test_net.c` for packets).

## Review checklist (copy into the PR)

- [ ] Everything new that came from the network is length-checked before use (rule 3) and capped (rule 3).
- [ ] No new address, pointer or code path is taken from the network (rules 1, 2).
- [ ] New state fields that index something are in the sanitize (rule 4).
- [ ] Names and paths from a peer go through the checks (rule 5).
- [ ] `make test` (Overbar) or `sh port/overbar/tests/netsec.sh .` (a port) passes.

## Known limits

- The Genesis port's game-state loader and zip reader live in PulseMan (`ext/pulseman`, never changed here). Joining an unknown host
  is a trust decision there.
- A shared rollback state still holds the host's stack contents; its return addresses are the host's. Rule 2 limits where a jump
  lands, not what the host's stack holds. Only join hosts you trust.
