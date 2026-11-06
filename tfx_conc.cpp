// tfx_conc.cpp - new fix (conc_edge): a concussion grenade that lifts a
// grounded player only a little gets swallowed by the ground and does nothing.
//
// CBasePlayer::TeamFortress_TakeConcussionBlast(entvars_s *pevGrenade, float)
// sets the player's velocity to a push away from the grenade, with the upward
// part multiplied by 1.5 (confirmed from tfc.so: it writes pev->velocity.x/y/z
// around 0xbb002.., then sends the screen-shake / Concuss effect). It does NOT
// touch pev->flags - FL_ONGROUND is left exactly as it was.
//
// The engine's movement code decides, on the next player frame, whether that
// push actually lifts the player. PM_CatagorizePosition keeps you airborne only
// when velocity[2] > 180; at or below that it traces 2 units down, finds the
// floor you are standing on, and drops you straight back onto the ground
// (PM_WalkMove then zeroes velocity[2]). So a conc that gives you, say, 160 u/s
// up - plenty to feel like it should pop you - is erased before you move an
// inch. Whether a given conc clears 180 depends on your exact distance and
// height relative to the grenade, which is why close concs feel inconsistent:
// some launch you, some do nothing at all.
//
// The fix: after the game applies its knockback, if a grounded player's upward
// velocity landed in the dead band (conc_rescue_min .. 180), nudge it just over
// 180 (181) and clear FL_ONGROUND so CatagorizePosition can't re-ground it. The
// horizontal push is left exactly as the game set it. conc_rescue_min (default
// 150) keeps us from "rescuing" a far-off conc that was only ever a gentle
// shove - below it, the player is left on the ground as the game intended.
//
// Same vtable-patch mechanism as fix #7 (tfx_takehealth.cpp): one patch to the
// shared CBasePlayer vtable covers every player, chained to the original and
// put back on unload. The slot was read out of each shipped game DLL - tfc.so
// slot 63, tfc.dll slot 62 (gamedll_vtable.h records how each was confirmed).
// TFX_HookConc still refuses to patch if the slot is ever -1 (an unknown build).

#include "tfx.h"
#include "gamedll_vtable.h"

typedef void (WC_THISCALL *ConcBlastFn)(void *pThis, entvars_t *pevInflictor, float flAmplitude);

static ConcBlastFn g_origConc   = NULL;
static void      **g_concSlot   = NULL;
static void       *g_concHookFn = NULL;

static void TFX_TakeConcussionBlast(void *pThis, entvars_t *pevInflictor, float flAmplitude)
{
	// Let the game apply its full knockback and send the effect first.
	if (g_origConc)
		g_origConc(pThis, pevInflictor, flAmplitude);

	if (!TFX_On(FIX_CONC_EDGE))
		return;

	entvars_t *pev = WC_GetPev(pThis);
	if (!pev || !(pev->flags & FL_CLIENT))
		return;

	// Only someone standing on the ground can be re-grounded out of a conc; an
	// airborne player keeps whatever the conc gave them, so leave them alone.
	if (!(pev->flags & FL_ONGROUND))
		return;

	float vz = pev->velocity.z;
	if (vz >= g_tfx.concRescueMin && vz <= 180.0f)
	{
		pev->velocity.z = 181.0f;       // just over PM_CatagorizePosition's 180 cutoff
		pev->flags &= ~FL_ONGROUND;     // and don't let the down-trace put us back on the floor
		TFX_Hit(FIX_CONC_EDGE,
			"%s: conc lifted %.0f u/s (<=180, would've been re-grounded) -> 181 u/s",
			STRING(pev->netname), vz);
	}
}

#ifdef _WIN32
// Same thunk trick as tfx_takehealth.cpp: MSVC won't make a free function
// __thiscall, so a member function forwards its own `this`. (Unused until the
// Windows slot is known, but kept so the file builds there.)
class TFX_ConcThunk
{
public:
	void TakeConc(entvars_t *pevInflictor, float flAmplitude)
	{
		TFX_TakeConcussionBlast(this, pevInflictor, flAmplitude);
	}
};

static void *TFX_ConcHookFuncPtr()
{
	void (TFX_ConcThunk::*pMemberFn)(entvars_t *, float) = &TFX_ConcThunk::TakeConc;
	return (void *)*reinterpret_cast<ConcBlastFn *>(&pMemberFn);
}
#else
static void *TFX_ConcHookFuncPtr()
{
	return (void *)TFX_TakeConcussionBlast;
}
#endif

void TFX_HookConc(edict_t *pPlayer)
{
	if (TFX_VT_CONCBLAST_SLOT < 0)
	{
		// No known vtable slot for this build (the slot is -1): nothing safe to
		// patch. Both shipped game DLLs have a known slot; this guards the rest.
		static bool warned = false;
		if (!warned)
		{
			TFX_Trace("conc_edge: no known vtable slot on this platform - fix unavailable");
			warned = true;
		}
		return;
	}
	if (g_concSlot || FNullEnt(pPlayer))
		return;
	void *pThis = GET_PRIVATE(pPlayer);
	if (!pThis)
		return;
	void **vtable = *(void ***)pThis;
	void *hook = TFX_ConcHookFuncPtr();
	if (vtable[TFX_VT_CONCBLAST_SLOT] == hook)
		return;
	g_origConc = (ConcBlastFn)WC_SetVFunc(pThis, TFX_VT_CONCBLAST_SLOT, hook);
	if (g_origConc)
	{
		g_concSlot   = vtable + TFX_VT_CONCBLAST_SLOT;
		g_concHookFn = hook;
		TFX_Trace("conc_edge: player hook installed at vtable slot %d (was %p)",
			TFX_VT_CONCBLAST_SLOT, (void *)g_origConc);
	}
	else
		TFX_Trace("conc_edge: could not patch the player vtable - the fix is unavailable");
}

void TFX_UnhookConc(void)
{
	if (!g_concSlot || !g_origConc)
		return;
	// Only if the slot still holds ours: anything patched over us since is
	// chaining to us, and pulling us out from under it is no better.
	if (*g_concSlot == g_concHookFn)
	{
		WC_WriteVSlot(g_concSlot, (void *)g_origConc);
		TFX_Trace("conc_edge: player hook removed");
	}
	else
		TFX_Trace("conc_edge: hook NOT removed - something else patched the slot since");
	g_concSlot   = NULL;
	g_concHookFn = NULL;
	g_origConc   = NULL;
}
