// tfx.h - TFC Fixes: shared declarations.
//
// Every fix here was checked against the code of the TFC build it runs on
// (tfc.so with its debug symbols, tfc.dll decompiled alongside it), not
// carried over from the AMX Mod X plugin's notes. See README.md for what each
// fix does and docs/FIXES.md for the game code behind each one.
#ifndef TFC_FIXES_TFX_H
#define TFC_FIXES_TFX_H

#include <extdll.h>
#include <meta_api.h>
#include <entity_state.h>   // clientdata_t (fix #16)

#include "sdk_compat.h"

// ---------------------------------------------------------------------------
// The fixes. The order is the config file's and the `tfx` command's; the
// numbers in the names are the AMX Mod X "Bug Fixes TFC" plugin's, so a fix can
// be compared with the one it replaces.
enum TfxFix
{
	FIX_GHOST_NAILS = 0,     // #1  nails that hit each other stay in the air
	FIX_FLAG_IN_WALL,        // #2  a dropped flag lands inside a wall or ceiling
	FIX_DOOR_CALTROPS,       // #7  opening a door heals caltrop leg damage
	FIX_TELEPORT_DEATH,      // #8  dying mid-teleport still teleports you
	FIX_TELEPORT_FLAG,       // #9  picking up a flag mid-teleport takes it through
	FIX_TELEPORT_LEAVER,     // new: a player who leaves mid-teleport crashes the server
	FIX_GRENADE_IN_ENTITY,   // #11 a grenade inside something does full damage to everyone
	FIX_NAILGREN_CEILING,    // #12 nail grenades rise into the ceiling
	FIX_MIRV_WALLS,          // #13 MIRV bomblets appear on the other side of a ceiling
	FIX_GRENADE_SPAWN,       // #14 a grenade primed just before death follows you to spawn
	FIX_DROPITEMS_GRENADE,   // #15 dropping a flag deletes the grenade you are priming
	FIX_ZERO_HEALTH,         // #16 the "death bug": alive with under 1 health
	FIX_GRENADE_JUMP_FPS,    // #17 hand-grenade jumps go straight up on a 1000 fps server
	FIX_FLAG_TOSS,           // #18 "Not enough room to drop items here"
	FIX_STUCK_MENUS,         // #19 a menu from last map stays on screen
	FIX_PUSH_FPS,            // new: jump-pad (push field) launch height moves with fps/tick
	FIX_COUNT
};

struct TfxFixInfo
{
	const char *key;         // config key
	const char *title;       // shown by `tfx`
};
extern const TfxFixInfo g_tfxFixInfo[FIX_COUNT];

struct TfxConfig
{
	int   on[FIX_COUNT];
	int   logEach;           // log_each: how many triggers of each fix per map go to the log
	float pushScale;         // push_fps: multiply a jump pad's launch (push_scale, default 1)
	float pushFanScale;      // push_fps: multiply a fan/updraft's sustained lift (push_fan_scale, default 1)
	float pushMax;           // push_fps: clamp a launch/lift to this many u/s (push_max, 0 = no cap)
};
extern TfxConfig g_tfx;

// How often each fix stepped in - this map, and since the plugin loaded.
extern int g_tfxCountMap[FIX_COUNT];
extern int g_tfxCountTotal[FIX_COUNT];

inline bool TFX_On(int fix) { return g_tfx.on[fix] != 0; }

// A fix stepped in: count it, and log the first few each map.
void TFX_Hit(int fix, const char *fmt, ...);

// tfx_config.cpp
void TFX_ConfigDefaults(void);
void TFX_ConfigLoad(void);
void TFX_RegisterCommands(void);

// tfx_trace.cpp - fixes_trace.log beside the DLL
void TFX_Trace(const char *fmt, ...);
void TFX_LogModuleIdentity(void);
bool TFX_PathBesideModule(const char *name, char *out, size_t len);
void TFX_TraceRotate(void);

// tfx_game.cpp - what we need from the game DLL itself
bool TFX_GameResolve(void);          // find the think functions by name; true if all found
void TFX_GameMapStart(void);
bool TFX_IsThinking(edict_t *e, int which);   // is e's m_pfnThink the game's function `which`?
enum { THINK_GRENADE_PRIMED = 0, THINK_CALTROP_PRIMED, THINK_DETONATE, THINK_TELEPORTER, THINK_COUNT };
bool TFX_ThinkKnown(int which);

// tfx_takehealth.cpp - the player's TakeHealth (fix #7)
void TFX_HookTakeHealth(edict_t *pPlayer);
void TFX_UnhookTakeHealth(void);

// tfx_fixes.cpp - the fixes, called from the hooks in tfx_hooks.cpp
void TFX_FixesMapStart(void);
void TFX_FixesFrame(void);
void TFX_ClientPutInServer(edict_t *e);
void TFX_ClientDisconnect(edict_t *e);
void TFX_ClientCommandPre(edict_t *e, const char *cmd);
void TFX_ClientCommandPost(edict_t *e);
bool TFX_ThinkPre(edict_t *e);       // true = handled specially, see Post
void TFX_ThinkPost(edict_t *e);
bool TFX_TouchPre(edict_t *touched, edict_t *other);   // true = handled, supersede the game's Touch
void TFX_TouchPost(edict_t *touched, edict_t *other);
void TFX_UsePost(edict_t *used);
void TFX_GrenSnapAll(void);
int  TFX_ShouldCollide(edict_t *touched, edict_t *other);   // 0 = do not collide, 1 = no opinion
void TFX_UpdateClientDataPost(const edict_t *ent, clientdata_t *cd);
bool TFX_SetOriginPre(edict_t *e, const float *org);       // true = we set it, supersede
void TFX_SetOriginPost(edict_t *e, const float *org);
void TFX_SetSizePost(edict_t *e);
void TFX_TraceLinePost(int fNoMonsters, edict_t *skip, TraceResult *tr);
void TFX_TraceHullPost(int hullNumber, edict_t *skip, TraceResult *tr);
void TFX_MessageBegin(int dest, int type, edict_t *ed);
void TFX_MessageEndPost(void);

// ---------------------------------------------------------------------------
// Helpers
inline bool TFX_IsPlayerIndex(int i) { return i >= 1 && gpGlobals && i <= gpGlobals->maxClients; }
inline bool TFX_ValidEnt(const edict_t *e) { return e && !e->free && e->pvPrivateData; }
inline bool TFX_ClassIs(const edict_t *e, const char *name)
{
	return e && e->v.classname && !strcmp(STRING(e->v.classname), name);
}
inline bool TFX_ClassStarts(const edict_t *e, const char *prefix)
{
	return e && e->v.classname && !strncmp(STRING(e->v.classname), prefix, strlen(prefix));
}
// TFC's IsAlive: a feigning spy (deadflag 5) counts as alive.
inline bool TFX_Alive(const edict_t *e)
{
	return e && (e->v.deadflag == DEAD_NO || e->v.deadflag == 5) && e->v.health > 0.0f;
}

#endif
