# TFC Fixes — technical reference

How the plugin is put together, what each source file and function does, and, for
every fix, the Team Fortress Classic bug behind it and the mechanism that corrects
it. For the player-facing summary and the config, see [`../README.md`](../README.md).

Each fix was checked against the game build it runs on: the Linux `tfc.so` (which
ships with debug symbols) and the `tfc.dll` decompiled beside it. The `tests/`
harness loads the real `tfc.so` behind a stand-in engine and plays each bug out
with the plugin on (the fix must work) and off (the bug must show).

---

## 1. How it hooks the game

The plugin is a native **metamod-p** plugin. Metamod hands it the same function
tables the engine hands the game DLL, so the plugin can run code *before* or
*after* each engine/game function and, when it wants, replace the result.

`meta_api.cpp` publishes the plugin identity and the metamod entry points.
`tfx_hooks.cpp` registers the individual hooks and forwards each one straight to a
function in `tfx_fixes.cpp`. The tables used:

| Metamod table | Functions hooked | Used by |
|---|---|---|
| `GetEntityAPI2` (pre) | `ServerActivate`, `StartFrame`, `Think`, `Touch`, `ClientPutInServer`, `ClientDisconnect`, `ClientCommand` | most fixes |
| `GetEntityAPI2_Post` | `Think`, `Touch`, `Use`, `ClientCommand`, `UpdateClientData` | #2, #15, #16, #17 |
| `GetNewDLLFunctions` | `ShouldCollide` | #1 |
| `GetEngineFunctions` (pre) | `SetOrigin`, `MessageBegin` | #2, #12, #13, #19 |
| `GetEngineFunctions_Post` | `SetOrigin`, `SetSize`, `TraceLine`, `TraceHull`, `MessageEnd` | #2, #11, #18, #19 |

A hook returns `MRES_IGNORED` to let the real function run, or `MRES_SUPERCEDE`
to replace it (used by #1 `ShouldCollide`, by the nail-grenade / MIRV / dropped-flag
`SetOrigin`, and by `push_fps`'s `Touch`).

### Knowing what an entity is about to do — `tfx_game.cpp`

Several fixes must know what an entity's *next think* is: a grenade still in a
hand (`GrenadePrimed` / `CaltropGrenadePrimed`), a grenade about to detonate
(`Detonate`), a teleporter (`TeleporterThink`). TFC keeps that in the entity's
`m_pfnThink`, and both `tfc.dll` and `tfc.so` export these methods by name (the
engine's save/restore needs them). `TFX_GameResolve()` finds the game DLL module
(from a known game function pointer), looks each method up by its decorated
(MSVC) or mangled (Itanium) name, and stores the address. `TFX_IsThinking(e, which)`
then compares `e`'s `m_pfnThink` against it — exact identification, no guessing
from classnames. If any method is missing, the fixes that need it stay off and
`tfx` reports "think functions found: NOT ALL".

### Patching a virtual with no engine hook — `tfx_takehealth.cpp` / `gamedll_vtable.h`

Fix #7 needs the player's `TakeHealth`, a C++ virtual with no engine-level hook.
`TFX_HookTakeHealth()` patches that one slot in `CBasePlayer`'s vtable (one patch
covers every player), chaining to the original, and `TFX_UnhookTakeHealth()`
restores it on unload — but only if nothing else has patched over it since.
`gamedll_vtable.h` has the read/patch/restore helpers; on Windows the slot gets a
member-function thunk because MSVC will not make a free function `__thiscall`.

### Config, logging, offsets

* `tfx_config.cpp` — reads `tfc_fixes.ini` at every map start, owns the per-fix
  on/off flags, the per-map and total hit counters, and the `tfx` / `tfx_reload`
  server commands (including the live `tfx push_scale|push_fan_scale|push_max <v>`
  setters).
* `tfx_trace.cpp` — `fixes_trace.log` beside the DLL; rolls over past 1 MB.
* `tfx_offsets.h` — the byte offsets of the game's private fields (`tfstate`,
  grenade counts, leg damage, teleporter state, the think pointer, …), one set for
  Windows and one for Linux, each with a note on where it was confirmed.
* `h_export.cpp`, `sdk_util.cpp`, `sdk_compat.h`, `linux_compat.*` — SDK and
  cross-compiler glue (entry points, Linux `new`/`delete`, a pre-GLIBC_2.4 floor).

---

## 2. Source files at a glance

| File | What it owns |
|---|---|
| `tfx.h` | Shared declarations: the `TfxFix` enum, `TfxConfig`, the hit counters, and every hook/fix function prototype. |
| `tfx_fixes.cpp` | The fixes themselves — one function (or pair) per fix, each with the TFC code behind it in a comment. |
| `tfx_hooks.cpp` | The metamod pre/post hooks; each forwards to `tfx_fixes.cpp` and returns the right `MRES_*`. |
| `tfx_game.cpp` | Resolving the game's think methods by symbol; `TFX_IsThinking`. |
| `tfx_takehealth.cpp` | The player `TakeHealth` vtable hook (fix #7). |
| `tfx_config.cpp` | `tfc_fixes.ini`, the `tfx`/`tfx_reload` commands, the counters. |
| `tfx_trace.cpp` | `fixes_trace.log`. |
| `tfx_offsets.h` | Field offsets, Windows and Linux. |
| `gamedll_vtable.h` | vtable slot patch/restore. |
| `meta_api.cpp` | Plugin identity, `Meta_Attach`/`Meta_Detach`. |
| `tests/` | A fake engine + fake metamod around the real `tfc.so` (`fxsim.*`), one scenario per fix (`t_fixes.cpp`). |

### Key functions in `tfx_fixes.cpp`

| Function | Role |
|---|---|
| `TFX_FixesMapStart` | Reset per-player state at `ServerActivate`. |
| `TFX_FixesFrame` | `StartFrame`: per-player alive/death bookkeeping, grenade snapshots, death handling (#14), deferred flag-drop check (#2). |
| `TFX_ClientPutInServer` / `TFX_ClientDisconnect` | Per-player connect/disconnect state; disconnect clears teleporters (leaver). |
| `TFX_ClientCommandPre` / `TFX_ClientCommandPost` | `dropitems` bracketing (#18) and the primed-grenade check (#15). |
| `TFX_ThinkPre` / `TFX_ThinkPost` | Teleporter think (#8/#9/leaver) and the in-hand grenade-jump box shift (#17). |
| `TFX_TouchPre` / `TFX_TouchPost` | Push pads/fans (`push_fps`) and ghost-nail removal (#1); grenade check after a touch (#15). |
| `TFX_UsePost` | Grenade check after a `+use` (#15). |
| `TFX_ShouldCollide` | Nails pass through each other (#1). |
| `TFX_UpdateClientDataPost` | The "death bug" health clamp (#16). |
| `TFX_SetOriginPre` | Nail-grenade ceiling (#12) and MIRV bomblet placement (#13). |
| `TFX_SetOriginPost` / `TFX_SetSizePost` | Dropped flag out of a wall (#2). |
| `TFX_TraceLinePost` | Grenade-inside-entity radius damage (#11). |
| `TFX_TraceHullPost` | "Not enough room to drop items" (#18). |
| `TFX_MessageBegin` / `TFX_MessageEndPost` | Blank menu with the MOTD (#19). |
| `TFX_PushTouch` | The `push_fps` pad/fan logic (see §4). |

Every fix is counted and logged through `TFX_Hit(fix, fmt, …)` and gated by
`TFX_On(fix)`; all state is reset in `TFX_FixesMapStart`.

---

## 3. The fixes

Numbers are the AMX Mod X "Bug Fixes TFC" plugin's, for comparison. Each line of
`tfc_fixes.ini` turns one on or off.

### #1 `ghost_nails` — nails that hang in the air
A nailgun nail that hits **another nail** is not removed (`CTFNailgunNail::NailTouch`
skips anything with the same model), so both stop dead and stay: invisible to the
client, still solid. **Fix:** `TFX_ShouldCollide` returns "do not collide" for two
nails (via the engine's `ShouldCollide`), so they never touch. A belt-and-braces
`TFX_TouchPre` also removes the hit nail if an engine ignores `ShouldCollide`.

### #2 `flag_in_wall` — a dropped flag starts inside a wall/ceiling
`tfgoalitem_drop` places the item 8 units above the dropper (26 crouched) without a
check; crouched in a vent, or killed there, the item's box starts inside the brush
and never moves again. **Fix:** `TFX_SetOriginPost` notes the drop, and
`TFX_SetSizePost` (called right after, when the item gets its size) traces the
item's own hull; if it starts solid, the item is moved as far toward the drop point
as it fits, starting from the dropper.

### #7 `door_caltrops` — opening a door cures caltrops
Every `func_door` calls `TakeHealth(m_bHealthValue)` on whoever opens it, almost
always 0, and `CBasePlayer::TakeHealth` does its leg-damage bookkeeping whatever the
amount. **Fix:** the patched `TakeHealth` (see §1) skips a heal of exactly 0 — it
healed nothing anyway.

### #8 `teleport_death` — dying mid-teleport still teleports you
Stepping on the entrance stores you in the pad (state 3); 0.1 s later the exit moves
you onto itself with a telefrag. Nothing in between checks whether you are still
alive. **Fix:** `TFX_ThinkPre` watches the teleporter think; at the entrance's send
step it cancels if the player is no longer in-game, and at the exit's receive step
it skips the move/telefrag (jumping to the "arrived" step that clears both pads) if
the player died — or died and respawned (tracked by a per-player death counter). The
"arrived" glow is then cleared in `TFX_ThinkPost`.

### #9 `teleport_flag` — a flag rides through a teleport
The "can't teleport" check runs only on the way in; pick up a flag in between and it
goes through. **Fix:** same teleporter-think watcher; if the player now carries an
item that forbids teleporting, the teleport is cancelled at either pad and the player
is unfrozen where they stand.

### — `teleport_leaver` — leaving mid-teleport (new; not in the AMX plugin)
Leave the server mid-teleport and both pads keep a pointer to a player object that is
no longer anyone, and TFC goes on using it. **Fix:** `TFX_ClientDisconnect` (and the
teleporter think, which reads `m_pPlayer->pev` first) forget the departed player on
every pad and reset the pads to ready.

### #11 `grenade_in_entity` — a stuck grenade hits everyone at full damage
`RadiusDamage` measures each victim by a trace from the explosion, and a trace that
*starts* inside something is given distance 0 — full damage. A grenade lodged in a
player/sentry/door therefore hits the whole radius at full strength. **Fix:**
`TFX_TraceLinePost` drops the `fStartSolid` flag when such a trace started inside an
entity and the thing exploding is one of TFC's grenades, so damage falls off with
distance as normal.

### #12 `nailgren_ceiling` — nail grenades rise into the ceiling
`CTFNailGrenade::Explode` traces 32 units straight up and `SetOrigin`s there; under a
low ceiling that puts the grenade (and its nails) half inside the brush. **Fix:**
`TFX_SetOriginPre` re-traces and keeps the grenade ~8 units below whatever is above
it (its own `SET_ORIGIN`, guarded against re-entry).

### #13 `mirv_walls` — MIRV bomblets on the wrong side of a surface
`CTFBomblet::LaunchFromMirv` puts each bomblet 32 units above the MIRV without a
trace, through a thin floor/ceiling. **Fix:** `TFX_SetOriginPre` finds the MIRV below
the requested spot, traces from it, and keeps the bomblet on the MIRV's side.

### #14 `grenade_spawn` — a primed grenade follows you to spawn
A primed grenade first checks its owner 0.8 s after the pin is pulled; die and
respawn inside that window and it is still "in hand" and goes off at your new spawn.
Caltrops never check at all. **Fix:** `TFX_OnDeath` (from `TFX_FixesFrame` the frame
a player dies) finds any grenade still in the dead player's hand, sets the "let go"
`tfstate` bit and makes it think immediately, so it drops at the body — what TFC does
with any grenade held at death once that first 0.8 s is up.

### #15 `dropitems_grenade` — losing a flag deletes a primed grenade
`RemoveResults` removes a primed grenade if *either* grenade count drops below 1,
even when the item gave no grenades of that type. It runs on every way an item
leaves: dropping, **capturing**, or a goal taking it. **Fix:** a per-player snapshot
of the grenade state is taken each frame and after every client command
(`TFX_GrenSnapAll`); after any touch/use/command (`TFX_GrenCheck`) a removal the
proper rule would not have made is undone before the grenade's next think sees it.

### #16 `zero_health` — the "death bug"
The engine sends health rounded **down** to the client, and the client treats 0 as
dead: on 0.5 health you get the death view while alive and moving, and a medic/pack
"revives" you. **Fix:** `TFX_UpdateClientDataPost` sends the client 1 while the real
health is between 0 and 1 and the player is not dead. The server's value is untouched.

### #17 `grenade_jump_fps` — hand-grenade jumps go straight up at high fps
An explosion pushes a player away from the grenade's centre
(`origin − (absmin+absmax)/2`). For a grenade that goes off *in hand* (at the
player's origin) the vector is zero unless the player moved since the last frame — at
100 fps they have, at 1000 fps they usually have not, so the push is straight up.
**Fix:** `TFX_ThinkPre` (on the detonate think) shifts the in-hand grenade's *bounding
box only* — not its origin — to where the player was one 100-fps frame ago, so the
push points along their movement. `TFX_ThinkPost` puts the box back. The server tick
rate is never touched.

### #18 `flag_toss` — "Not enough room to drop items here"
`DropGoalItems` traces a player-sized box 32 units ahead of your eyes and refuses the
drop if anything is in the way (a teammate, a doorframe). **Fix:** inside a
`dropitems` command (bracketed by `TFX_ClientCommandPre`/`Post`), `TFX_TraceHullPost`
reports that one trace as clear. Fix #2 still keeps the item out of the wall.

### #19 `stuck_menus` — a menu left over from the last map
A text menu open at a map change never gets its "close"; the client keeps it up on
the next map, eating number keys. **Fix:** `TFX_MessageBegin` spots the message of
the day (the first thing every client is sent), and `TFX_MessageEndPost` follows it
with a blank `ShowMenu` (no keys, no text) once per connection, which closes it.

---

## 4. `push_fps` — jump pads and fans, consistent at any fps/tick

A `trigger_push` without the "Push Once" spawnflag is a "push field". In TFC's build
its `Touch` sets `basevelocity = speed·movedir / frametime`, and the engine then does
`velocity += basevelocity·frametime` each frame — the `frametime` cancels, so a
player gains `speed·movedir` in velocity **every frame** they are sampled inside the
brush (confirmed against `tfc.so`). The launch is the sum over however many frames
catch the player inside, so it grows with the frame rate: at 1000 tick it fills you
to `sv_maxvelocity`. `SF_TRIG_PUSH_ONCE` pads do not have this — they add
`speed·movedir` to velocity once.

`TFX_PushTouch` supersedes `trigger_push`'s `Touch` for predominantly-upward fields
(a mostly-horizontal conveyor/wind is a transient velocity, left to the engine) and
splits them into two kinds:

* **Jump PAD** — a thin, always-on floor brush. One scaled one-shot per entry: it
  **sets** the player's upward velocity to `speed · push_scale` (set, not add, so
  hitting a pad while already rising does not stack toward the cap; a faster riser
  keeps their speed), at most once per 0.5 s per player. The cooldown is **time-based**
  so overlapping brushes and a 1000-tick storm of touches still give a single launch;
  a frame counter was tried first and was not robust at 1000 tick.
* **FAN / updraft** — a tall (> 256 u) or `START_OFF` (toggled) push field. Sustained
  lift: it **holds** the player's upward velocity at `speed · push_fan_scale` every
  frame inside the field (gravity is countered each frame), so they ride up at a
  steady rate — the same at any tick rate — and coast up on leaving.

The 256 u split keeps thin jump pads (≈ 56–164 u in the test maps) as pads while tall
fan columns are fans; a toggled fan is caught by its spawnflag regardless of height.
`push_max` clamps either. Because this changes player movement, which the client
predicts, a pad gives one small corrective step on contact.

**Tuning** (ini or live via `tfx …`): `push_scale` (jump-pad launch multiplier,
default 1), `push_fan_scale` (fan-lift multiplier, default 1 = full map strength),
`push_max` (u/s cap on either, default 0 = off). The clean no-plugin alternative for
a single pad is the map's **Push Once** spawnflag, which is already rate-independent
and which this fix leaves untouched.

---

## 5. Not included (TFC fixes these itself)

The AMX plugin's #3 (feigning-spy medikit), #4 (quick disguise), #5 (post-disconnect
camera), #6 (concussion after respawn), #10 (uncovering dead spies), and the
"after the send" half of #8 are not here — the current TFC build handles them, and
the `camera` / `conc_respawn` test scenarios confirm those still work.
