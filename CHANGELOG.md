# Changelog

## 1.2.0

- **Fix #13 `mirv_walls` no longer crashes the server ("Trigger in clipping
  list").** On the current (anniversary) build `CTFBomblet::LaunchFromMirv`
  sets a bomblet's `solid` to `SOLID_TRIGGER` and only relinks it a few
  instructions later; in that gap the bomblet is a trigger still listed in the
  engine's *solid* clipping list. `mirv_walls` traces from the MIRV to each
  bomblet to keep it on the MIRV's side of a thin ceiling/floor, and that trace
  was landing inside the gap - so the engine walked the solid list, met a
  trigger in it, and aborted the whole server (`FATAL ERROR (shutting down):
  Trigger in clipping list`) on essentially every MIRV detonation (a demoman
  MIRV jump being the easy way to hit it). The fix relinks the bomblet to match
  its current `solid` before tracing, so the trace can never meet a trigger in
  the solid list; the bomblet still ends up exactly where `mirv_walls` puts it.
  Stock TFC never tripped this because nothing of its own traces in that gap.
  The test harness now models the engine's clipping-list rule and the `mirv`
  scenario asserts the fatal never fires - it fails without the fix and passes
  with it.
- **New fix `conc_edge` (off by default): edge concussion grenades that barely
  lift you still launch.** A concussion grenade sets your velocity directly, but
  the engine only keeps you airborne when the upward part clears 180 u/s; at or
  below that, `PM_CatagorizePosition` traces 2 units down, finds the floor and
  drops you straight back onto it (`PM_WalkMove` then zeroes your vertical
  velocity). So a close conc that kicks you up by, say, 160 u/s is erased before
  you move an inch — which is why close concs feel inconsistent: some launch you,
  some do nothing. With `conc_edge` on, a conc that leaves a *grounded* player in
  the `conc_rescue_min`..180 band is nudged just over 180 (and `FL_ONGROUND`
  cleared) so it launches; a strong conc (already over 180), a weak one (below
  `conc_rescue_min`, default 150) and an already-airborne player are all left
  exactly as the game set them. It ships **off** because it changes a
  long-standing movement feel — enable it deliberately. It hooks
  `TeamFortress_TakeConcussionBlast` on the shared player vtable (the same
  one-patch-covers-every-player mechanism as #7's `TakeHealth`); the slot was
  read out of each shipped game DLL — `tfc.so` slot 63, `tfc.dll` slot 62 — so
  the fix runs on Windows and Linux servers. Tune the band from the ini
  (`conc_rescue_min`) or live with `tfx conc_rescue_min <v>`.
- **`tools/symbols/`: a tool to read the game DLLs.** A cross-symbolizer that
  names ~80% of `tfc.dll`'s functions by porting `tfc.so`'s debug symbols onto
  it — exact matches on `tfc.dll`'s exported names and on each class vtable, then
  string/constant/call-graph fingerprinting for the rest. It writes a Ghidra
  labelling script, a symbol map, and a per-class **vtable cross-map**
  (`out/VTABLES.md`), which is the table the plugin's Windows vtable slots and
  field offsets are read off. Re-run `tools/symbols/run.sh` after a TFC update to
  re-check every slot and offset against the new binaries.
- **`tests/ci.sh`: a one-command release gate.** It builds the plugin `.so` and
  the test harness, checks that *every* fix has at least one scenario
  (`t_fixes --coverage`), and runs every scenario with the plugin on and off,
  with a clean per-scenario report and a pass/fail exit code. Adding a fix
  without a test now fails the gate.

## 1.1.0

- **New fix `push_fps`: push pads launch the same at any fps/tick.** A
  `trigger_push` field (one without the "Push Once" spawnflag) adds `speed` to
  the player's velocity every frame they are sampled inside the brush — `tfc.so`
  sets `basevelocity = speed·movedir / frametime` and the engine's `× frametime`
  cancels it (confirmed against `tfc.so`) — so the launch is the sum over
  however many frames catch them inside, which grows with the frame rate (~2
  frames at 100 fps vs ~20 at 1000 fps: several times higher). The fix SETS the
  player's upward velocity to the pad's `speed·movedir` once and supersedes the
  per-frame push, so the launch is the pad's own `speed`, the same at any rate.
  It sets rather than adds (chaining pads / rising into a pad does not stack
  toward the cap), and launches at most once per 0.5 s per player via a
  time-based cooldown (so overlapping brushes and a 1000-tick storm of touches
  still give a single launch; a frame counter was not robust at 1000 tick). A
  FAN / updraft — a tall (>256u) or START_OFF (toggled) push field — instead
  gets **sustained lift**: its upward velocity is held at `speed·push_fan_scale`
  every frame inside it, so it rides you up steadily at the same rate on any tick
  and you coast up on leaving, while jump pads stay one-shots. Only
  vertical-dominant fields are handled; mostly-horizontal ones (conveyors, wind)
  are left to the engine. Movement is client-predicted, so a pad gives one
  corrective hitch on contact. `speed` is the launch velocity, so tune a pad by
  its speed and keep it under `sv_maxvelocity` (default 2000). Three ini knobs
  tune every map without editing it: `push_scale` (jump-pad launch multiplier,
  default 1), `push_fan_scale` (fan-lift multiplier, default 1 = full map
  strength), and `push_max` (a u/s cap on either, default 0 = off). `tfx` prints
  them, and `tfx push_scale|push_fan_scale|push_max <v>` sets any of them live
  (until the next map or `tfx_reload`) for tuning by feel.
- **Linux build:** `Makefile.linux` now deploys the built `.so` (and
  `tfc_fixes.ini` the first time) to `tfc/addons/tfc_fixes_mm/`, the same place
  and way the Windows build copies its DLL - previously it left the `.so` in the
  source folder, so the deployed Linux plugin was never updated. It also passes
  `-fno-math-errno`, so `sqrtf` inlines to a hardware instruction instead of a
  libm call; without it, building on a current toolchain (glibc 2.43) pulled in
  a `GLIBC_2.43` symbol and the makefile's "loads on old servers" guard rejected
  the `.so`.

## 1.0.1

- **#15 now covers captures.** Capturing a flag (and any goal that takes an item
  away when it fires) deleted the grenade you were priming, just as dropping it
  did; only the `dropitems` command was covered before. The check now runs after
  every touch, use, timer and client command, for anyone who is priming.

## 1.0.0

First release. Replaces the AMX Mod X plugin "Bug Fixes TFC" v1.4.

- **Fixes:** 15 in all, each with its own on/off line in `tfc_fixes.ini`.
  - 14 of them are the AMX plugin's fixes that the current TFC build still
    needs: #1, #2, #7, #8, #9, #11–#19.
  - New: players leaving mid-teleport are cleared from the teleporters, and
    the exit no longer "receives" them.
- **Not included:** #3, #4, #5, #6 and #10, which TFC now fixes itself.
- **Commands:**
  - `tfx` lists every fix with how often it stepped in.
  - `tfx <fix> 0|1` switches one off or on until the next map.
  - `tfx_reload` reads the ini again.
- **Log:** `fixes_trace.log` beside the DLL. It rolls over to
  `fixes_trace.old.log` past 1 MB.
- **Builds:** Windows (Visual Studio) and Linux (`Makefile.linux`).
- **Tests:** `tests/` runs every fix against the real `tfc.so`, with and
  without the plugin.
