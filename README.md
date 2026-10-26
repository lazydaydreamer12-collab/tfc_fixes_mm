# TFC Fixes (metamod-p)

Fixes for bugs in Team Fortress Classic's own game code, as a native metamod-p
plugin. It replaces the AMX Mod X plugin
[**Bug Fixes TFC**](https://forums.alliedmods.net/showthread.php?t=333746)
(hlstriker/se7en, v1.4). Do not run both: TFC Fixes warns in the server console
if it sees the other one.

Every fix was checked against the code of the TFC build it runs on. The Linux
`tfc.so` ships with full debug symbols, and `tfc.dll` was decompiled alongside
it. Each fix was then run against the real `tfc.so` game code in a test harness
(see **Tests**), with and without the plugin. Without it, the bug happens;
with it, it does not.

## Install

1. Put this folder at `tfc\addons\metamod\metamod_sdk\tfc_fixes_mm\`, beside
   `tfc_teams_mm`, `metamod\` and `hlsdk\`.
2. Open `tfc_fixes_mm.sln` and build **Release | Win32**. The build copies
   `tfc_fixes_mm_mm.dll` to `tfc\addons\tfc_fixes_mm\`, and `tfc_fixes.ini`
   beside it the first time. It never overwrites an ini you have edited. The
   build fails if the server is running and holding the DLL open.
3. In `tfc\addons\metamod\plugins.ini`:
   `win32 addons\tfc_fixes_mm\tfc_fixes_mm_mm.dll`
4. Change map. `meta list` shows **TFC Fixes** as RUN. Then type `tfx` in the
   server console. Its last line should say "think functions found: all".

Linux: `make -f Makefile.linux` builds `tfc_fixes_mm_mm_i386.so` and copies it to
`tfc/addons/tfc_fixes_mm/` (and `tfc_fixes.ini` beside it the first time), the
same as the Windows build. The plugins.ini line is
`linux addons/tfc_fixes_mm/tfc_fixes_mm_mm_i386.so`. A running server must be
stopped to replace the `.so`.

## Commands (server console / rcon)

| Command | What it does |
|---|---|
| `tfx` | Every fix, on or off, and how many times it stepped in this map and since load |
| `tfx <fix> 0` / `tfx <fix> 1` | Switch one fix off or on until the next map |
| `tfx push_scale <v>` / `tfx push_fan_scale <v>` / `tfx push_max <v>` | Tune jump-pad launch / fan lift / the u/s cap live (until the next map or `tfx_reload`) |
| `tfx_reload` | Read `tfc_fixes.ini` again |

`addons\tfc_fixes_mm\tfc_fixes.ini` has one line per fix (`1` on, `0` off), plus
`log_each` and the `push_fps` knobs `push_scale` / `push_fan_scale` / `push_max`. It is read
at every map start. `fixes_trace.log` in the same folder records the first few
times each fix stepped in, every map.

## The fixes

The numbers are the AMX plugin's. For the TFC bug behind each fix and the
mechanism that corrects it, plus a tour of the source, see
[`docs/FIXES.md`](docs/FIXES.md).

| # | Setting | What TFC does wrong | What TFC Fixes does |
|---|---|---|---|
| 1 | `ghost_nails` | A nail that hits another nail is not removed (`NailTouch` skips anything with the same model), so both stop dead and hang there, invisible and solid | Nails pass through each other (the engine's `ShouldCollide`), so they never stop |
| 2 | `flag_in_wall` | A dropped item starts 8 units above the dropper's origin (26 when crouched) without checking. Crouched in a vent, or killed there, its box starts inside the ceiling and it never moves again | The item's own box is checked where the drop sizes it; if stuck, it goes as far towards the drop point as it fits, starting from the dropper |
| 7 | `door_caltrops` | Every `func_door` heals whoever opens it by its health value, almost always 0, and TFC's heal code reduces caltrop leg damage whatever the amount | A heal of 0 is skipped |
| 8 | `teleport_death` | Die in the 0.1 s between stepping on the entrance and being sent, and your body (or your respawned self) is still delivered to the exit, telefrag and all | The send is cancelled. Death after the send was already handled by TFC itself |
| 9 | `teleport_flag` | The "can't teleport" check is only made on the way in; pick up a flag in between and it goes through | The teleport is cancelled at either pad, and the player is unfrozen where they stand |
| — | `teleport_leaver` | Leave the server mid-teleport and the exit still "receives" you, telefrag included, and both pads keep pointing at you | Both pads let go of the player when they leave |
| 11 | `grenade_in_entity` | Radius damage treats a trace that *starts* inside something as distance 0, so a grenade stuck inside a player or building hits everyone in range at full damage | Such traces count as normal, so damage falls off with distance as usual |
| 12 | `nailgren_ceiling` | A nail grenade rises 32 units to the first surface above it, so under a low ceiling it sits half inside it, and so do its nails | It stops 8 units short of the ceiling |
| 13 | `mirv_walls` | Bomblets appear 32 units straight above the MIRV without looking, which puts them on the other side of a thin floor or ceiling | They stop below whatever is in the way |
| 14 | `grenade_spawn` | A primed grenade first checks on its owner 0.8 s after the pin is pulled. Die and respawn inside that window and it is still "in hand" and goes off at your new spawn. Caltrops never check at all | At death, a grenade still in hand is let go there and then, and drops at the body |
| 15 | `dropitems_grenade` | Losing a flag - dropping it, **capturing it**, or a goal taking it away - deletes the grenade you are priming if *either* grenade count is 0, even if the flag gave you no grenades | Only removes it when the flag took away grenades of the type you are priming (TFC's own rule when an item is handed over) |
| 16 | `zero_health` | Health is sent to the client rounded down, and 0 means dead: on 0.5 health you see the death view while still alive and moving | The client is told 1 while health is above 0 and below 1. The server's real value is untouched |
| 17 | `grenade_jump_fps` | A grenade going off in your hand pushes you away from where it was. At 100 fps you have moved since then, so you go forwards. At 1000 fps (`sys_ticrate 1000`) you usually have not, and you go straight up | The push is measured from where you would have been one 100-fps frame earlier, so it points the way you are moving. Nothing else about the blast changes, and the server tick rate is never touched |
| 18 | `flag_toss` | "Not enough room to drop items here": dropping checks a player-sized box from your eyes 32 units forward, so a teammate or a doorframe in front of you stops it | The check is skipped (only inside `dropitems`); #2 still keeps the item out of the wall |
| 19 | `stuck_menus` | A menu open at a map change stays on the next map, eating number keys | A blank menu goes out with each player's message of the day, once per connection |
| — | `push_fps` | A `trigger_push` *field* (no "Push Once" flag) adds `speed` to your velocity **every frame** you are sampled inside the brush (`tfc.so` sets `basevelocity = speed·movedir / frametime`, which the engine's `× frametime` cancels). So the launch is the sum over however many frames catch you inside — and that grows with the frame rate: a thin pad is ~2 frames at 100 fps but ~20 at 1000 fps, several times higher | On entry the player is pushed **once** — `velocity += speed·movedir`, exactly what a "Push Once" pad does — and the per-frame push is superseded while they stay on it. The launch is the pad's own `speed`, the same at any fps/tick. Only vertical-dominant pads are handled; conveyor/wind fields are left to the engine — see the caveat below |

### Already fixed by Valve

These AMX plugin fixes are not included. The current TFC does them itself, and
the AMX plugin had already dropped most of their code:

- **#3** Infecting a feigning spy: the medikit treats feigning as alive.
- **#4** Quick disguise: disguising is limited to once a second. (It is one
  timer for the whole server, not one per spy.)
- **#5** Camera view after a disconnect: every spawn resets the view.
- **#6** Concussion after respawn: respawning clears it, and the client is told.
- **#10** Uncovering dead spies: the medikit ignores anyone at 0 health.
- **#8**, the part after the send: dying once sent resets both pads.

### Better than the AMX plugin how?

- **It works on this TFC build.** The AMX plugin's teleporter offsets are 4
  bytes short of where the current `tfc.dll` keeps those fields, so its #8
  and #9 read the wrong memory.
- **No server-wide side effects.**
  - #17 never changes `sys_ticrate`. The AMX plugin drops the whole server to
    100 fps for 0.1 s every time a hand grenade is thrown or goes off in a
    hand.
  - #18 only touches the one trace in `dropitems`, not every player-sized
    trace on the server.
  - #16 does not change anyone's real health.
- **Fixes the cause instead of covering it up.**
  - #1 nails never collide, rather than deleting whichever one was hit.
  - #14 the grenade drops where you died, rather than being deleted when you
    respawn.
  - #15 restores exactly what was wrongly removed, with no timer. The AMX
    version also undid grenade changes the flag legitimately made.
- **New: players leaving mid-teleport.**
- **New: push pads that launch the same at any fps/tick** (`push_fps`), rather
  than a height that moves with the server rate and the player's own fps.
- **Knows what an entity is about to do.** TFC exports its think functions by
  name, and the plugin compares against them instead of guessing from
  appearances.
- **Switchable and visible.** Every fix can be switched off in the ini. `tfx`
  shows how often each one actually happens on your server.

### `push_fps`: the client-prediction caveat

Unlike the other fixes, this one changes player movement, which clients predict
locally. The server applies the one-shot push on contact; your client, still
running the stock per-frame push, predicts a slightly different path for a
moment and then snaps to the server's value when it arrives — one small
corrective hitch on the pad, not continuous rubber-banding. The push **sets**
your upward velocity to the pad's launch (it does not add), so **`speed` is the
launch velocity**: every hit gives the same height regardless of how fast you
were already rising (chaining pads does not stack), and a faster riser keeps
their speed. Tune a pad by its speed, and keep it under `sv_maxvelocity`
(default 2000) or the launch is clamped. A player is launched at most once per
0.5 s (a time-based cooldown), so overlapping brushes or a high tick rate still
give a single launch rather than stacking to the cap.

A **fan / updraft** — a tall (>256u) or START_OFF (toggled) push field — is
handled differently: instead of a one-shot it gives **sustained lift**, holding
your upward velocity at the fan's speed every frame you are inside it, so you
ride up steadily (the same at any tick rate) and coast up on leaving. Tune every
map without editing it: `push_scale` (jump-pad launch, default 1),
`push_fan_scale` (fan lift, default 1 = full map strength) and `push_max` (a u/s
cap on either, default 0 = off) — e.g. `push_scale 0.25` makes pads a quarter as
strong while fans stay full. To turn the fix off for a
map, `tfx push_fps 0` (or set it in the ini); the clean, no-plugin alternative
is to build launch pads with the **Push Once** spawnflag, which is already
rate-independent and which this fix leaves untouched. Conveyor/wind fields
(mostly horizontal) are left to the engine.

## Working with the other plugins

- **Weapon Changer Plus and TFC Monsters:** fine together. Only the player's
  `TakeHealth` vtable slot is patched (#7). WC+ and Monsters patch
  `TakeDamage`, a different slot.
- **Unloading:** `meta unload` puts the slot back, as long as nothing has
  patched over it since.
- **Grenade names:** the grenade fixes look at TFC's own grenade entities,
  identified by their think function, so custom weapons are not affected.

## Tests

`tests/` holds a harness that loads the **real `tfc.so`** with a stand-in
engine and a stand-in metamod in front of it, the way a server runs it. It
then plays each bug out: building teleporters, priming grenades, carrying
flags, dying, respawning.

Each scenario runs twice:

- **plugin on:** the fix must work;
- **plugin off:** the bug must show.

To run it:

```
cd tests && ./build.sh && TFC_SO=/path/to/tfc.so ./run.sh
```

It needs 32-bit g++, and runs on Linux only. The Windows offsets in
`tfx_offsets.h` were read from `tfc.dll`'s machine code, where the game itself
uses each field.

## Source

| File | What it owns |
|---|---|
| `tfx_fixes.cpp` | The fixes, each with the TFC code behind it |
| `tfx_hooks.cpp` | The metamod hooks, which hand on to the fixes |
| `tfx_game.cpp` | Finding TFC's think functions by name |
| `tfx_takehealth.cpp` | The player `TakeHealth` hook (#7) |
| `tfx_offsets.h` | Field offsets, Windows and Linux, and where each was confirmed |
| `tfx_config.cpp` | `tfc_fixes.ini`, `tfx`, `tfx_reload`, the counters |
| `tfx_trace.cpp` | `fixes_trace.log` |
| `gamedll_vtable.h` | Patching and restoring a vtable slot |

Credit to hlstriker, se7en, teh ORiON and azul for finding and documenting
these bugs in the AMX plugin.
