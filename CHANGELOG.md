# Changelog

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
