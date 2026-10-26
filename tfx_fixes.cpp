// tfx_fixes.cpp - the fixes themselves. tfx_hooks.cpp calls in here from the
// engine and game hooks; each function below says which fix it is and what
// in TFC's own code makes the bug happen.

#include "tfx.h"
#include "tfx_offsets.h"

#include <math.h>

#define TFX_MAXP 33

// ---------------------------------------------------------------------------
// Per-player state
struct TfxPlayer
{
	bool connected;
	bool wasAlive;        // alive with a class, last StartFrame
	int  life;            // +1 at every death we see
	int  teleLife;        // `life` when a teleporter started sending them (-1 = not seen)
	bool zeroHealth;      // health is between 0 and 1 (fix #16), for counting once
	bool menuCleared;     // fix #19 sent this connection
	bool known;           // seen connect or disconnect since load
	bool setThrowBit;     // fix #14 set the "let go" bit on this dead player
};
static TfxPlayer s_pl[TFX_MAXP];

static int s_frame = 0;                // StartFrame counter

// Fix #17: a grenade whose knockback reference was moved for one think.
static edict_t *s_jumpGren = NULL;
static Vector s_jumpMin, s_jumpMax, s_jumpMinShifted;

// Fix #8/#9: a teleport cancelled on its exit pad this think.
static edict_t *s_teleCancelled = NULL;
static edict_t *s_teleCancelledPlayer = NULL;

// Fix #2: an item_tfgoal being dropped right now (tfgoalitem_drop calls
// SetOrigin, then SetSize a few lines later).
static edict_t *s_flagDrop = NULL;
static int s_flagDropFrame = -1;
static Vector s_flagDropper;

// Fix #18: inside a "dropitems" command.
static edict_t *s_dropPlayer = NULL;

// Fix #15: what each player priming a grenade had, before anything that can
// take a goal item away from them runs.
struct TfxGrenSnap
{
	bool primed;
	int  tfstate, removeGren, gren1, gren2, primedType;
};
static TfxGrenSnap s_gren[TFX_MAXP];
static int s_grenPriming = 0;   // how many snapshots have primed set
static void TFX_GrenCheck(const char *how, edict_t *by);

// Fix #19
static edict_t *s_motdTo = NULL;
static int s_msgMOTD = 0, s_msgShowMenu = 0;

static bool s_guard = false;   // re-entry guard for our own SET_ORIGIN calls

// Push fix: time (gpGlobals->time) until which a player won't be launched off a
// pad again. A jump pad can be several brushes, and the engine touches a player
// many times while they stand on it - at 1000 tick, dozens of times - so the
// launch is given once, then suppressed for a short window. Time-based so it
// behaves the same at any tick rate (a frame counter does not).
static float s_pushCooldown[TFX_MAXP];

// Alive, with a class: spectators and players still choosing a class are not.
static bool TFX_InGame(edict_t *e)
{
	return TFX_ValidEnt(e) && TFX_Alive(e) && e->v.playerclass != 0 && !(e->v.flags & FL_SPECTATOR);
}

// Is someone in this player slot? Known from connect/disconnect - or, for a
// slot that has not done either since the plugin loaded (loaded mid-map), from
// the engine's flags.
static bool TFX_SlotConnected(int i)
{
	if (i < 1 || i >= TFX_MAXP || i > gpGlobals->maxClients)
		return false;
	if (s_pl[i].known)
		return s_pl[i].connected;
	edict_t *e = INDEXENT(i);
	return TFX_ValidEnt(e) && (e->v.flags & FL_CLIENT) && e->v.netname && STRING(e->v.netname)[0];
}

// The connected player whose C++ object this is, or NULL.
static edict_t *TFX_EdictOfPlayerObj(void *obj)
{
	if (!obj)
		return NULL;
	for (int i = 1; i <= gpGlobals->maxClients && i < TFX_MAXP; i++)
	{
		edict_t *e = INDEXENT(i);
		if (TFX_SlotConnected(i) && TFX_ValidEnt(e) && e->pvPrivateData == obj)
			return e;
	}
	return NULL;
}

static const char *TFX_Name(const edict_t *e)
{
	return (e && e->v.netname) ? STRING(e->v.netname) : "?";
}

// ---------------------------------------------------------------------------
void TFX_FixesMapStart(void)
{
	for (int i = 0; i < TFX_MAXP; i++)
	{
		s_pl[i].wasAlive = false;
		s_pl[i].teleLife = -1;
		s_pl[i].zeroHealth = false;
		s_pushCooldown[i] = 0.0f;
	}
	s_jumpGren = NULL;
	s_teleCancelled = s_teleCancelledPlayer = NULL;
	s_flagDrop = NULL;
	s_dropPlayer = NULL;
	memset(s_gren, 0, sizeof(s_gren));
	s_grenPriming = 0;
	s_motdTo = NULL;
	s_msgMOTD = s_msgShowMenu = 0;
}

// ---------------------------------------------------------------------------
// Fix #14: a grenade primed just before death follows you to your spawn.
//
// A primed grenade first thinks 0.8 s after the pin is pulled (getPinTime),
// then every 0.01 s. Killed() clears the "priming" bits on the player, but the
// grenade only notices its owner is dead at its next think - where it is then
// dropped at the corpse (DeadThrow). Die in that first 0.8 s and respawn before
// it runs out, and the grenade sees a live owner, carries on "in hand", and
// goes off at the new spawn. Caltrops never check whether the owner is alive at
// all: they let go only when thrown or timed out.
//
// So at the moment of death, any grenade still in the dead player's hand is
// told it has been let go (the same bit +gren release sets) and to think now.
// It is dropped at the corpse, which is what TFC does with every grenade held
// at death once that first 0.8 s is over.
static void TFX_OnDeath(edict_t *pl)
{
	if (!TFX_On(FIX_GRENADE_SPAWN)
		|| !TFX_ThinkKnown(THINK_GRENADE_PRIMED) || !TFX_ThinkKnown(THINK_CALTROP_PRIMED))
		return;
	for (int k = gpGlobals->maxClients + 1; k < gpGlobals->maxEntities; k++)
	{
		edict_t *g = INDEXENT(k);
		if (!TFX_ValidEnt(g) || g->v.owner != pl)
			continue;
		if (!TFX_IsThinking(g, THINK_GRENADE_PRIMED) && !TFX_IsThinking(g, THINK_CALTROP_PRIMED))
			continue;
		float wait = g->v.nextthink - gpGlobals->time;
		if (wait <= 0.0f)
			continue;   // thinking this frame anyway
		TFX_Field<int>(pl->pvPrivateData, OFS_TFSTATE) |= TFSTATE_GRENTHROWING;
		g->v.nextthink = gpGlobals->time;
		int i = ENTINDEX(pl);
		if (i >= 1 && i < TFX_MAXP)
			s_pl[i].setThrowBit = true;
		TFX_Hit(FIX_GRENADE_SPAWN, "%s died holding a %s %.2fs before its next check - dropped at the body",
			TFX_Name(pl), STRING(g->v.classname), wait);
	}
}

// ---------------------------------------------------------------------------
void TFX_FixesFrame(void)
{
	s_frame++;
	TFX_GrenSnapAll();
	for (int i = 1; i <= gpGlobals->maxClients && i < TFX_MAXP; i++)
	{
		if (!TFX_SlotConnected(i))
		{
			s_pl[i].wasAlive = false;
			continue;
		}
		edict_t *e = INDEXENT(i);
		if (s_pl[i].setThrowBit)
		{
			// The grenade has had its think and cleared this itself; make sure
			// a dead player is never left with it (it blocks priming).
			s_pl[i].setThrowBit = false;
			if (TFX_ValidEnt(e) && !TFX_Alive(e))
				TFX_Field<int>(e->pvPrivateData, OFS_TFSTATE) &= ~TFSTATE_GRENTHROWING;
		}
		TFX_HookTakeHealth(e);   // loaded mid-map: no connect to do it at (does nothing once done)
		bool alive = TFX_InGame(e);
		if (s_pl[i].wasAlive && !alive)
		{
			s_pl[i].life++;
			if (TFX_ValidEnt(e) && e->v.deadflag != DEAD_NO)
				TFX_OnDeath(e);
		}
		s_pl[i].wasAlive = alive;
	}
	// A flag drop SetSize never followed (should not happen): check it now.
	if (s_flagDrop && s_flagDropFrame != s_frame)
		s_flagDrop = NULL;
}

void TFX_ClientPutInServer(edict_t *e)
{
	int i = ENTINDEX(e);
	if (i < 1 || i >= TFX_MAXP)
		return;
	s_pl[i].connected = true;
	s_pl[i].known = true;
	s_pl[i].wasAlive = false;
	s_pl[i].teleLife = -1;
	s_pl[i].zeroHealth = false;
	s_pl[i].menuCleared = false;
	s_pushCooldown[i] = 0.0f;
	TFX_HookTakeHealth(e);
}

// ---------------------------------------------------------------------------
// Teleporters (fixes #8, #9 and the leaver).
//
// Stepping on an entrance pad stores the player in the pad (m_pPlayer, state
// 3). 0.1 s later the entrance freezes them and hands them to the exit (state
// 4, with the same pointer); a moment later the exit moves them onto itself
// with UTIL_SetOrigin and a telefrag, and then (state 5) unfreezes them and
// clears both pads. Nothing in between checks whether that player is still
// alive, still the same life, or now carrying a flag - TeleporterTouch checks
// is_unableto_spy_or_teleport once, on the way in.
//
//   #8  died in between: the exit still moves them (dead, or freshly respawned)
//       onto the exit pad, telefragging whoever stands there.
//   #9  picked up a flag in between: it goes through with them.
//   leaver: left the server in between: both pads keep a pointer to a player
//       object that is no longer anyone, and TFC goes on using it.

static void TFX_TeleSetState(void *t, int state)
{
	TFX_Field<int>(t, OFS_TELE_STATE) = state;
	TFX_Field<float>(t, OFS_TELE_NEXTTHINK) = 0.0f;   // run it this think
}

// Forget a player on every teleporter that still points at them.
static int TFX_TeleForget(void *playerObj)
{
	int n = 0;
	edict_t *t = NULL;
	while (!FNullEnt(t = FIND_ENTITY_BY_STRING(t, "classname", "building_teleporter")))
	{
		void *obj = t->pvPrivateData;
		if (!obj || TFX_Field<void *>(obj, OFS_TELE_PLAYER) != playerObj)
			continue;
		TFX_Field<void *>(obj, OFS_TELE_PLAYER) = NULL;
		int st = TFX_Field<int>(obj, OFS_TELE_STATE);
		if (st == TELE_STATE_SENDING || st == TELE_STATE_RECEIVING || st == TELE_STATE_ARRIVED)
			TFX_TeleSetState(obj, TELE_STATE_RECHARGE);   // -> ready again, nobody on it
		n++;
	}
	return n;
}

void TFX_ClientDisconnect(edict_t *e)
{
	int i = ENTINDEX(e);
	if (i >= 1 && i < TFX_MAXP)
	{
		s_pl[i].connected = false;
		s_pl[i].known = true;
	}
	if (TFX_On(FIX_TELEPORT_LEAVER) && TFX_ValidEnt(e))
	{
		int n = TFX_TeleForget(e->pvPrivateData);
		if (n)
			TFX_Hit(FIX_TELEPORT_LEAVER, "%s left mid-teleport - cleared from %d teleporter(s)", TFX_Name(e), n);
	}
}

static void TFX_TeleThink(edict_t *te)
{
	void *t = te->pvPrivateData;
	void *pobj = TFX_Field<void *>(t, OFS_TELE_PLAYER);
	if (!pobj)
		return;
	int state = TFX_Field<int>(t, OFS_TELE_STATE);
	int type = TFX_Field<int>(t, OFS_TELE_TYPE);

	edict_t *pl = TFX_EdictOfPlayerObj(pobj);
	if (!pl)
	{
		// Not a connected player any more. Every think starts by reading
		// m_pPlayer->pev, so this cannot wait for the pad to get round to it.
		if (TFX_On(FIX_TELEPORT_LEAVER))
		{
			int n = TFX_TeleForget(pobj);
			TFX_Hit(FIX_TELEPORT_LEAVER, "teleporter held a player who is gone - cleared from %d pad(s)", n);
		}
		return;
	}

	float nt = TFX_Field<float>(t, OFS_TELE_NEXTTHINK);
	if (nt != 0.0f && gpGlobals->time < nt)
		return;   // TeleporterThink returns early this time

	int idx = ENTINDEX(pl);
	bool unable = TFX_Field<int>(pl->pvPrivateData, OFS_UNABLE_SPY_TELEPORT) != 0;

	if (state == TELE_STATE_SENDING && type == TELE_TYPE_ENTRANCE)
	{
		const char *why = NULL;
		int fix = FIX_TELEPORT_DEATH;
		if (TFX_On(FIX_TELEPORT_DEATH) && !TFX_InGame(pl))
			why = "died on the entrance pad";
		else if (TFX_On(FIX_TELEPORT_FLAG) && unable)
			why = "picked up an item that forbids teleporting", fix = FIX_TELEPORT_FLAG;
		if (why)
		{
			// Nothing has been done to the player yet: just let go of them.
			TFX_Field<void *>(t, OFS_TELE_PLAYER) = NULL;
			TFX_TeleSetState(t, TELE_STATE_RECHARGE);
			TFX_Hit(fix, "teleport of %s cancelled at the entrance: %s", TFX_Name(pl), why);
			return;
		}
		if (idx >= 1 && idx < TFX_MAXP)
			s_pl[idx].teleLife = s_pl[idx].life;
		return;
	}

	if (state == TELE_STATE_RECEIVING && type == TELE_TYPE_EXIT)
	{
		const char *why = NULL;
		int fix = FIX_TELEPORT_DEATH;
		if (TFX_On(FIX_TELEPORT_DEATH))
		{
			if (!TFX_InGame(pl))
				why = "died while being teleported";
			else if (idx >= 1 && idx < TFX_MAXP && s_pl[idx].teleLife >= 0 && s_pl[idx].teleLife != s_pl[idx].life)
				why = "died and respawned while being teleported";
		}
		if (!why && TFX_On(FIX_TELEPORT_FLAG) && unable)
			why = "picked up an item that forbids teleporting", fix = FIX_TELEPORT_FLAG;
		if (why)
		{
			// Skip the move and the telefrag: go straight to the exit's
			// "arrived" step, which unfreezes the player and clears both pads.
			TFX_TeleSetState(t, TELE_STATE_ARRIVED);
			s_teleCancelled = te;
			s_teleCancelledPlayer = pl;
			TFX_Hit(fix, "teleport of %s cancelled at the exit: %s", TFX_Name(pl), why);
		}
	}
}

// ---------------------------------------------------------------------------
// Fix #17: hand-grenade jumps at 1000 fps.
//
// A grenade held too long goes off in the hand: ExplodeInHand puts it at the
// player's origin, and it detonates on the next server frame. The push a
// player gets from an explosion points from the centre of the grenade to the
// player (CBasePlayer::TakeDamage: origin - (absmin+absmax)/2). At 100 fps the
// player has moved by the next frame, so the push points the way they were
// running. At 1000 fps there is usually no movement command between the two
// frames, the vector is zero, and TFC pushes straight up.
//
// Fix: when an in-hand grenade (still exactly at its owner's origin) is about
// to detonate, move its bounding box - only the box, which is what the push is
// measured from - to where the player was one 100 fps frame ago. The origin
// stays put, so everything else about the explosion is TFC's own.
static void TFX_GrenadeJumpPre(edict_t *g)
{
	edict_t *owner = g->v.owner;
	if (FNullEnt(owner) || !(owner->v.flags & FL_CLIENT))
		return;
	if (g->v.origin != owner->v.origin || g->v.velocity.Length() != 0.0f)
		return;   // not in hand, or the player moved anyway (low fps)
	Vector vel = owner->v.velocity;
	if (vel.Length() < 1.0f)
		return;
	Vector shift = vel * 0.01f;
	s_jumpGren = g;
	s_jumpMin = g->v.absmin;
	s_jumpMax = g->v.absmax;
	g->v.absmin = g->v.absmin - shift;
	g->v.absmax = g->v.absmax - shift;
	s_jumpMinShifted = g->v.absmin;
	TFX_Hit(FIX_GRENADE_JUMP_FPS, "%s's %s went off in hand at %.0f u/s - push set along their movement",
		TFX_Name(owner), STRING(g->v.classname), vel.Length());
}

bool TFX_ThinkPre(edict_t *e)
{
	if (!TFX_ValidEnt(e))
		return false;
	if (TFX_IsThinking(e, THINK_TELEPORTER))
	{
		TFX_TeleThink(e);
		return true;
	}
	if (TFX_On(FIX_GRENADE_JUMP_FPS) && TFX_IsThinking(e, THINK_DETONATE))
	{
		TFX_GrenadeJumpPre(e);
		return true;
	}
	return false;
}

void TFX_ThinkPost(edict_t *e)
{
	if (s_grenPriming)
	{
		TFX_GrenCheck("timer", e);
		TFX_GrenSnapAll();
	}
	if (s_jumpGren && s_jumpGren == e)
	{
		// Put the box back if nothing has relinked it since.
		if (!e->free && e->v.absmin == s_jumpMinShifted)
		{
			e->v.absmin = s_jumpMin;
			e->v.absmax = s_jumpMax;
		}
		s_jumpGren = NULL;
	}
	if (s_teleCancelled && s_teleCancelled == e)
	{
		// The "arrived" step leaves the player glowing in team colour, the
		// welcome for someone who just came through. They did not.
		edict_t *pl = s_teleCancelledPlayer;
		if (TFX_ValidEnt(pl))
		{
			pl->v.renderfx = kRenderFxNone;
			pl->v.rendermode = kRenderNormal;
			pl->v.renderamt = 0;
			pl->v.rendercolor = Vector(0, 0, 0);
		}
		s_teleCancelled = s_teleCancelledPlayer = NULL;
	}
}

// ---------------------------------------------------------------------------
// Fix #1: ghost nails.
//
// CTFNailgunNail::NailTouch removes the nail after it hits something - unless
// the something is another nail (same model), in which case it does nothing.
// Two nails that touch therefore stop dead and stay: invisible to the client,
// still solid, still there. Nails just pass through each other now; they never
// touch, so neither is left behind.
static bool TFX_IsNail(const edict_t *e)
{
	return e->v.movetype == MOVETYPE_FLYMISSILE && TFX_ClassIs(e, "tf_nailgun_nail");
}

int TFX_ShouldCollide(edict_t *touched, edict_t *other)
{
	if (!TFX_On(FIX_GHOST_NAILS) || !touched || !other)
		return 1;
	if (touched->v.modelindex != other->v.modelindex
		|| touched->v.movetype != MOVETYPE_FLYMISSILE || other->v.movetype != MOVETYPE_FLYMISSILE)
		return 1;
	if (!TFX_IsNail(touched) || !TFX_IsNail(other))
		return 1;
	return 0;
}

// ---------------------------------------------------------------------------
// Push fix (new): jump pads and fans behave the same at any fps/tick.
//
// A trigger_push without SF_TRIG_PUSH_ONCE is a "push field". tfc.so's Touch
// sets basevelocity = speed*movedir / frametime, and the engine then does
// velocity += basevelocity*frametime each frame (pm_shared.c), so the player
// gains speed*movedir in velocity EVERY frame sampled inside it (verified
// against tfc.so). The result grows with the frame rate - at 1000 tick it fills
// you to sv_maxvelocity - which is the bug.
//
// We split push fields into jump PADS and FANS/updrafts and supersede the
// engine's per-frame push for both:
//   - PAD (thin, always-on floor brush): one scaled one-shot per entry. SET the
//     upward velocity to speed*push_scale (set, not add, so chaining does not
//     stack), once per TFX_PUSH_COOLDOWN (so overlapping brushes / a 1000-tick
//     storm of touches still give a single launch).
//   - FAN (tall, or START_OFF = toggled): sustained lift. Hold the upward
//     velocity at speed*push_fan_scale every frame inside the field, so the
//     player rides up at a steady speed - the same at any tick rate - and coasts
//     up on leaving. A separate scale so fans can lift hard while pads stay tamed.
// push_max caps either; only predominantly-upward fields are handled (a mostly-
// horizontal conveyor/wind is a transient velocity, left to the engine).
#define TFX_PUSH_COOLDOWN     0.5f    // pad: seconds between launches; fan: log throttle
#define TFX_SF_PUSH_START_OFF 2       // trigger_push spawnflag: starts off (a toggled fan/trap)
#define TFX_PUSH_FAN_HEIGHT   256.0f  // a push brush taller than this is a fan (jump pads are shorter)
static bool TFX_PushTouch(edict_t *touched, edict_t *other)
{
	if (!TFX_On(FIX_PUSH_FPS) || !TFX_ClassIs(touched, "trigger_push"))
		return false;
	if ((int)touched->v.spawnflags & SF_TRIG_PUSH_ONCE)
		return false;   // already a clean one-shot
	// Only players, and only when trigger_push's own Touch would push them:
	// its guard skips these movetypes and these two solids.
	if (!(other->v.flags & FL_CLIENT))
		return false;
	int mt = other->v.movetype;
	if (mt == MOVETYPE_NONE || mt == MOVETYPE_PUSH || mt == MOVETYPE_NOCLIP || mt == MOVETYPE_FOLLOW)
		return false;
	if (other->v.solid == SOLID_NOT || other->v.solid == SOLID_BSP)
		return false;

	Vector push = touched->v.movedir * touched->v.speed;
	float horiz2 = push.x * push.x + push.y * push.y;
	// A launch pad: upward, and at least as steep as 45 degrees. A flatter push
	// field is a conveyor or wind tunnel - leave those to the engine.
	if (push.z <= 0.0f || push.z * push.z < horiz2)
		return false;

	int i = ENTINDEX(other);
	if (i < 1 || i >= TFX_MAXP)
		return false;

	// Fan or pad? A fan is tall or toggled (START_OFF); a pad is a thin always-on
	// floor brush. (bounce's jump pads are ~164u, so the fan cutoff sits above
	// them; alchimy's fans are START_OFF and are caught regardless of height.)
	float height = touched->v.size.z > 0.0f ? touched->v.size.z
	                                        : touched->v.absmax.z - touched->v.absmin.z;
	bool isFan = ((int)touched->v.spawnflags & TFX_SF_PUSH_START_OFF) || height > TFX_PUSH_FAN_HEIGHT;

	if (isFan)
	{
		// Sustained updraft: hold the upward velocity at the fan's lift speed every
		// frame inside the field (gravity is countered each frame), so the player
		// rides up at a steady rate on any tick and coasts up on leaving.
		Vector lift = push * g_tfx.pushFanScale;
		if (g_tfx.pushMax > 0.0f && lift.Length() > g_tfx.pushMax)
			lift = lift * (g_tfx.pushMax / lift.Length());
		if (lift.z > other->v.velocity.z)
			other->v.velocity.z = lift.z;
		other->v.flags &= ~FL_ONGROUND;
		if (gpGlobals->time >= s_pushCooldown[i])   // count/log once per window, not every frame
		{
			TFX_Hit(FIX_PUSH_FPS, "%s lifted by a push fan to %.0f u/s up (speed %.0f x%.2f)",
				TFX_Name(other), lift.z, touched->v.speed, g_tfx.pushFanScale);
			s_pushCooldown[i] = gpGlobals->time + TFX_PUSH_COOLDOWN;
		}
		return true;
	}

	// Jump pad: one scaled launch per cooldown (suppress the storm of touches so
	// the one-shot does not stack toward sv_maxvelocity - time-based, robust at
	// any tick rate; a frame counter was not, and at 1000 tick it stacked).
	if (gpGlobals->time < s_pushCooldown[i])
		return true;
	s_pushCooldown[i] = gpGlobals->time + TFX_PUSH_COOLDOWN;
	Vector launch = push * g_tfx.pushScale;
	if (g_tfx.pushMax > 0.0f && launch.Length() > g_tfx.pushMax)
		launch = launch * (g_tfx.pushMax / launch.Length());
	// SET the upward velocity (do not add), so hitting a pad while already rising
	// gives a consistent launch instead of stacking; never slow a faster riser.
	if (launch.z > other->v.velocity.z)
		other->v.velocity.z = launch.z;
	other->v.flags &= ~FL_ONGROUND;
	TFX_Hit(FIX_PUSH_FPS, "%s launched off a push pad to %.0f u/s up (pad speed %.0f)",
		TFX_Name(other), launch.z, touched->v.speed);
	return true;
}

// Should two nails touch anyway (an engine that ignores ShouldCollide), do
// what NailTouch should have done: the nail that was hit is gone.
bool TFX_TouchPre(edict_t *touched, edict_t *other)
{
	if (FNullEnt(touched) || FNullEnt(other))
		return false;
	if (TFX_PushTouch(touched, other))
		return true;
	if (TFX_On(FIX_GHOST_NAILS)
		&& touched->v.modelindex == other->v.modelindex && TFX_IsNail(touched) && TFX_IsNail(other))
	{
		touched->v.solid = SOLID_NOT;
		touched->v.flags |= FL_KILLME;
		TFX_Hit(FIX_GHOST_NAILS, "two nails touched - removed the one that was hit");
	}
	return false;
}

// ---------------------------------------------------------------------------
// Fix #16: the "death bug". The engine sends health to the client rounded DOWN
// to a whole number, and the client treats 0 as dead: a player on 0.5 health
// gets the death view (tilted, on the floor) while the server has them alive
// and moving - and a medic or a health pack "revives" them. The client is now
// told 1 while the real health is above 0 and below 1; the server's own value
// is not touched.
void TFX_UpdateClientDataPost(const edict_t *ent, clientdata_t *cd)
{
	if (!TFX_On(FIX_ZERO_HEALTH) || !ent || !cd)
		return;
	int i = ENTINDEX((edict_t *)ent);
	bool zero = cd->health > 0.0f && cd->health < 1.0f && ent->v.deadflag == DEAD_NO;
	if (zero)
	{
		cd->health = 1.0f;
		if (i >= 1 && i < TFX_MAXP && !s_pl[i].zeroHealth)
			TFX_Hit(FIX_ZERO_HEALTH, "%s alive on %.2f health - client shown 1", TFX_Name(ent), ent->v.health);
	}
	if (i >= 1 && i < TFX_MAXP)
		s_pl[i].zeroHealth = zero;
}

// ---------------------------------------------------------------------------
// SetOrigin - fixes #12 and #13 (before), #2 (after).

#define TFX_NAILGREN_CLEARANCE 8.0f   // a nail grenade's model reaches about this far up

static bool TFX_NailGrenadeRise(edict_t *g, const float *org)
{
	// CTFNailGrenade::Explode: movetype FLY, solid NOT, a trace 32 units
	// straight up, then SetOrigin to wherever that trace stopped - which under
	// a low ceiling is the ceiling's surface, with the grenade's top half (and
	// the nails it fires) inside it.
	if (g->v.movetype != MOVETYPE_FLY || g->v.solid != SOLID_NOT)
		return false;
	if (org[0] != g->v.origin.x || org[1] != g->v.origin.y || org[2] <= g->v.origin.z)
		return false;
	Vector start = g->v.origin;
	Vector probe(org[0], org[1], org[2] + TFX_NAILGREN_CLEARANCE);
	TraceResult tr;
	TRACE_LINE(start, probe, ignore_monsters, g, &tr);
	if (tr.fStartSolid || tr.fAllSolid || tr.flFraction >= 1.0f)
		return false;
	float z = tr.vecEndPos.z - TFX_NAILGREN_CLEARANCE;
	if (z < start.z)
		z = start.z;
	if (z >= org[2])
		return false;
	Vector dest(org[0], org[1], z);
	s_guard = true;
	SET_ORIGIN(g, dest);
	s_guard = false;
	TFX_Hit(FIX_NAILGREN_CEILING, "nail grenade kept %.0f units below the ceiling (rose %.0f instead of %.0f)",
		TFX_NAILGREN_CLEARANCE, z - start.z, org[2] - start.z);
	return true;
}

static bool TFX_BombletPlace(edict_t *b, const float *org)
{
	// CTFBomblet::LaunchFromMirv puts each bomblet 32 units straight above the
	// MIRV, without looking - through a low ceiling, or a floor when the MIRV
	// lies under one. The MIRV is the one exactly below the requested spot.
	edict_t *mirv = NULL, *m = NULL;
	while (!FNullEnt(m = FIND_ENTITY_BY_STRING(m, "classname", "tf_weapon_mirvgrenade")))
	{
		if (fabs(m->v.origin.x - org[0]) < 0.01f && fabs(m->v.origin.y - org[1]) < 0.01f
			&& fabs((org[2] - m->v.origin.z) - 32.0f) < 0.5f)
		{
			mirv = m;
			break;
		}
	}
	if (!mirv)
		return false;
	Vector from = mirv->v.origin, to(org[0], org[1], org[2]);
	TraceResult tr;
	TRACE_LINE(from, to, ignore_monsters, mirv, &tr);
	Vector dest;
	if (tr.fStartSolid || tr.fAllSolid)
		dest = from;
	else if (tr.flFraction < 1.0f)
	{
		dest = tr.vecEndPos;
		dest.z -= 2.0f;
		if (dest.z < from.z)
			dest.z = from.z;
	}
	else
		return false;
	s_guard = true;
	SET_ORIGIN(b, dest);
	s_guard = false;
	TFX_Hit(FIX_MIRV_WALLS, "MIRV bomblet kept on the MIRV's side (%.0f units up instead of 32)", dest.z - from.z);
	return true;
}

bool TFX_SetOriginPre(edict_t *e, const float *org)
{
	if (s_guard || FNullEnt(e) || !org || !e->v.classname)
		return false;
	const char *cls = STRING(e->v.classname);
	if (cls[0] != 't' || strncmp(cls, "tf_weapon_", 10))
		return false;
	if (TFX_On(FIX_NAILGREN_CEILING) && !strcmp(cls + 10, "nailgrenade"))
		return TFX_NailGrenadeRise(e, org);
	if (TFX_On(FIX_MIRV_WALLS) && !strcmp(cls + 10, "mirvbomblet"))
		return TFX_BombletPlace(e, org);
	return false;
}

// Fix #2: a dropped flag lands inside a wall or the ceiling.
//
// tfgoalitem_drop puts the item at the dropper's origin raised by 8 (26 when
// crouched), then tosses it. Crouched under a low ceiling, or pressed against
// a wall, that point already has the item's box half in the brush, and an item
// that starts inside solid never moves again. That is caught where the drop
// gives the item its size (SetSize, right after the SetOrigin), using the
// item's own box: if it is stuck, it goes as far towards the drop point as it
// fits, starting from the dropper.
void TFX_SetOriginPost(edict_t *e, const float *org)
{
	if (s_guard || !TFX_On(FIX_FLAG_IN_WALL) || FNullEnt(e) || !org)
		return;
	if (!TFX_ClassIs(e, "item_tfgoal"))
		return;
	edict_t *owner = e->v.owner;
	if (FNullEnt(owner) || !(owner->v.flags & FL_CLIENT))
		return;
	if (org[0] != owner->v.origin.x || org[1] != owner->v.origin.y)
		return;   // not tfgoalitem_drop's "at the dropper" placement
	s_flagDrop = e;
	s_flagDropFrame = s_frame;
	s_flagDropper = owner->v.origin;
}

void TFX_SetSizePost(edict_t *e)
{
	if (!s_flagDrop || e != s_flagDrop || s_flagDropFrame != s_frame)
		return;
	s_flagDrop = NULL;
	if (e->v.movetype != MOVETYPE_TOSS)
		return;   // not a drop after all
	TraceResult tr;
	Vector here = e->v.origin;
	TRACE_MONSTER_HULL(e, here, here, ignore_monsters, e, &tr);
	if (!tr.fStartSolid && !tr.fAllSolid)
		return;
	TRACE_MONSTER_HULL(e, s_flagDropper, here, ignore_monsters, e, &tr);
	Vector dest = (!tr.fStartSolid && !tr.fAllSolid) ? Vector(tr.vecEndPos) : s_flagDropper;
	s_guard = true;
	SET_ORIGIN(e, dest);
	s_guard = false;
	TFX_Hit(FIX_FLAG_IN_WALL, "dropped item would have started inside a wall - moved %.0f units back towards the dropper",
		(here - dest).Length());
}

// ---------------------------------------------------------------------------
// Fix #11: a grenade inside another entity does full damage to everyone.
//
// RadiusDamage traces from the explosion to each victim and takes the damage
// off by the distance to where that trace ended; a trace that STARTS inside
// something is given distance zero - full damage. A grenade that has ended up
// inside a player, a sentry or a door therefore hits every victim in range at
// full strength, however far away. With the "started inside" flag dropped,
// such a trace is measured like any other and the damage falls off as usual.
// (The flag is only dropped when the trace started inside an entity and the
// thing exploding is one of TFC's grenades.)
void TFX_TraceLinePost(int fNoMonsters, edict_t *skip, TraceResult *tr)
{
	if (!tr || !tr->fStartSolid || fNoMonsters != dont_ignore_monsters || !tr->pHit)
		return;
	if (!TFX_On(FIX_GRENADE_IN_ENTITY) || FNullEnt(skip) || !TFX_ClassStarts(skip, "tf_weapon_"))
		return;
	tr->fStartSolid = 0;
	TFX_Hit(FIX_GRENADE_IN_ENTITY, "%s exploding inside %s: damage measured by distance as usual",
		STRING(skip->v.classname), tr->pHit->v.classname ? STRING(tr->pHit->v.classname) : "?");
}

// Fix #18: "Not enough room to drop items here". DropGoalItems first traces a
// player-sized box from your eyes 32 units forward and refuses if anything is
// in the way - a teammate, a wall, a doorframe. The item is then placed at your
// feet regardless of that trace, so the check only stops you dropping; inside a
// "dropitems" command it is now told the way is clear. (Fix #2 makes sure the
// item still cannot end up inside the wall.)
void TFX_TraceHullPost(int hullNumber, edict_t *skip, TraceResult *tr)
{
	if (!s_dropPlayer || skip != s_dropPlayer || hullNumber != human_hull || !tr)
		return;
	if (tr->flFraction >= 1.0f)
		return;
	tr->flFraction = 1.0f;
	TFX_Hit(FIX_FLAG_TOSS, "%s dropped an item with something in front of them", TFX_Name(skip));
}

// ---------------------------------------------------------------------------
// Fix #15: losing a flag deletes the grenade you are priming.
//
// Taking a goal item away (RemoveResults, tfc.so 0x10cee0) subtracts any
// grenades the item gave, then removes a primed grenade if EITHER grenade
// count is now below 1 - even when the item gave no grenades and the empty
// slot is not the one being primed. (The code that hands an item over does it
// properly: only the primed type, only if the item actually took some.)
//
// It is the same code whichever way the item goes: dropping it (dropitems),
// capturing it (the capture point's return_item_no, a touch), or a goal
// that removes it when it fires (a touch, a use or a timer). So the check is
// made after every one of those, for everyone who was priming before it: a
// removal the proper rule would not have made is undone before the grenade
// notices (it only looks at its next think).
//
// Snapshots are taken every frame and after every client command (so a
// grenade primed this frame is covered), and only players who are priming are
// looked at afterwards - when nobody is, the check is one comparison.
void TFX_GrenSnapAll(void)
{
	s_grenPriming = 0;
	for (int i = 1; i <= gpGlobals->maxClients && i < TFX_MAXP; i++)
	{
		TfxGrenSnap &s = s_gren[i];
		s.primed = false;
		edict_t *e = INDEXENT(i);
		if (!TFX_SlotConnected(i) || !TFX_ValidEnt(e))
			continue;
		void *p = e->pvPrivateData;
		s.tfstate = TFX_Field<int>(p, OFS_TFSTATE);
		if (!(s.tfstate & TFSTATE_GRENPRIMED))
			continue;
		s.primed = true;
		s.removeGren = TFX_Field<int>(p, OFS_REMOVE_GRENADE);
		s.gren1 = TFX_Field<int>(p, OFS_NO_GRENADES_1);
		s.gren2 = TFX_Field<int>(p, OFS_NO_GRENADES_2);
		s.primedType = TFX_Field<int>(p, OFS_PRIMED_GREN_TYPE);
		s_grenPriming++;
	}
}

static void TFX_GrenCheck(const char *how, edict_t *by)
{
	if (!s_grenPriming || !TFX_On(FIX_DROPITEMS_GRENADE))
		return;
	for (int i = 1; i <= gpGlobals->maxClients && i < TFX_MAXP; i++)
	{
		TfxGrenSnap &s = s_gren[i];
		if (!s.primed)
			continue;
		edict_t *e = INDEXENT(i);
		if (!TFX_SlotConnected(i) || !TFX_ValidEnt(e))
			continue;
		void *p = e->pvPrivateData;
		int tfstate = TFX_Field<int>(p, OFS_TFSTATE);
		int removeGren = TFX_Field<int>(p, OFS_REMOVE_GRENADE);
		if (s.removeGren || !removeGren || (tfstate & TFSTATE_GRENPRIMED))
			continue;   // not taken away (thrown, or still in hand)
		if (!TFX_Alive(e))
			continue;   // dying: TFC drops it at the body, and #14 makes sure
		int g1 = TFX_Field<int>(p, OFS_NO_GRENADES_1);
		int g2 = TFX_Field<int>(p, OFS_NO_GRENADES_2);
		bool proper =
			(s.primedType == 1 && g1 < 1 && g1 < s.gren1) ||
			(s.primedType == 2 && g2 < 1 && g2 < s.gren2);
		if (proper)
			continue;
		TFX_Field<int>(p, OFS_REMOVE_GRENADE) = 0;
		TFX_Field<int>(p, OFS_TFSTATE) = tfstate | (s.tfstate & (TFSTATE_GRENPRIMED | TFSTATE_GRENTHROWING));
		TFX_Hit(FIX_DROPITEMS_GRENADE, "%s lost an item while priming (%s%s%s) - grenade kept (counts %d/%d, priming type %d)",
			TFX_Name(e), how, by ? " " : "", by && by->v.classname ? STRING(by->v.classname) : "", g1, g2, s.primedType);
	}
}

// ---------------------------------------------------------------------------
// "dropitems": fix #18 (above). Fix #15 is checked after every client command.
void TFX_ClientCommandPre(edict_t *e, const char *cmd)
{
	if (!cmd || strcmp(cmd, "dropitems") || !TFX_ValidEnt(e))
		return;
	if (TFX_On(FIX_FLAG_TOSS))
		s_dropPlayer = e;
}

void TFX_ClientCommandPost(edict_t *e)
{
	s_dropPlayer = NULL;
	TFX_GrenCheck("command", NULL);
	TFX_GrenSnapAll();   // +gren1 and +gren2 start priming here
}

void TFX_TouchPost(edict_t *touched, edict_t *other)
{
	(void)other;
	if (!s_grenPriming)
		return;
	TFX_GrenCheck("touched", touched);
	TFX_GrenSnapAll();
}

void TFX_UsePost(edict_t *used)
{
	if (!s_grenPriming)
		return;
	TFX_GrenCheck("used", used);
	TFX_GrenSnapAll();
}

// ---------------------------------------------------------------------------
// Fix #19: a menu left on screen from the last map. A text menu shown just
// before a map change never gets its "close": the client keeps it up, and
// its keys keep being eaten, on the next map. A blank menu sent along with the
// message of the day - the first thing every client is sent - closes it.
void TFX_MessageBegin(int dest, int type, edict_t *ed)
{
	if (!TFX_On(FIX_STUCK_MENUS) || FNullEnt(ed))
		return;
	if (!s_msgMOTD)
		s_msgMOTD = GET_USER_MSG_ID(PLID, "MOTD", NULL);
	if (s_msgMOTD <= 0 || type != s_msgMOTD || (dest != MSG_ONE && dest != MSG_ONE_UNRELIABLE))
		return;
	int i = ENTINDEX(ed);
	if (i < 1 || i >= TFX_MAXP || s_pl[i].menuCleared)
		return;
	s_motdTo = ed;
}

void TFX_MessageEndPost(void)
{
	edict_t *ed = s_motdTo;
	if (!ed)
		return;
	s_motdTo = NULL;
	int i = ENTINDEX(ed);
	if (i < 1 || i >= TFX_MAXP || s_pl[i].menuCleared)
		return;
	if (!s_msgShowMenu)
		s_msgShowMenu = GET_USER_MSG_ID(PLID, "ShowMenu", NULL);
	if (s_msgShowMenu <= 0)
		return;
	s_pl[i].menuCleared = true;
	MESSAGE_BEGIN(MSG_ONE, s_msgShowMenu, NULL, ed);
	WRITE_SHORT(0);     // no keys
	WRITE_CHAR(0);      // display time: until replaced
	WRITE_BYTE(0);      // no more parts
	WRITE_STRING("");
	MESSAGE_END();
	TFX_Hit(FIX_STUCK_MENUS, "sent %s a blank menu with the MOTD", TFX_Name(ed));
}
