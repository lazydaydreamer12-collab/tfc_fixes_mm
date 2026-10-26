// tfx_takehealth.cpp - fix #7: opening a door heals caltrop leg damage.
//
// Every func_door calls TakeHealth(m_bHealthValue) on whoever opens it, and
// almost every door's health value is 0 (CBaseDoor::DoorActivate). TFC's
// CBasePlayer::TakeHealth does its leg-damage bookkeeping whatever the amount:
// at or near full health it clears leg_damage outright, below that it takes
// health/10 off it. So walking through doors cures caltrops. A heal of 0 heals
// nothing; skipping it entirely is the whole fix.
//
// The player's TakeHealth is a virtual function with no engine-level hook, so
// its slot in CBasePlayer's vtable is patched (one patch covers every player),
// chaining to whatever was there before, and put back when the plugin unloads.

#include "tfx.h"
#include "tfx_offsets.h"
#include "gamedll_vtable.h"

typedef int (WC_THISCALL *TakeHealthFn)(void *pThis, float flHealth, int bitsDamageType);

static TakeHealthFn g_origTakeHealth = NULL;
static void **g_slot = NULL;
static void *g_hookFn = NULL;

static int TFX_TakeHealth(void *pThis, float flHealth, int bitsDamageType)
{
	if (flHealth == 0.0f && TFX_On(FIX_DOOR_CALTROPS))
	{
		// Only counted when it would have changed something: leg damage to lose.
		entvars_t *pev = WC_GetPev(pThis);
		float leg = TFX_Field<float>(pThis, OFS_LEG_DAMAGE);
		if (pev && (pev->flags & FL_CLIENT) && leg > 0.0f)
			TFX_Hit(FIX_DOOR_CALTROPS, "%s kept %.1f leg damage through a 0-health heal (a door)",
				STRING(pev->netname), leg);
		return 0;
	}
	return g_origTakeHealth ? g_origTakeHealth(pThis, flHealth, bitsDamageType) : 0;
}

#ifdef _WIN32
// MSVC will not make a free function __thiscall, so the vtable gets a member
// function that forwards its own `this` - the same thunk WC+ uses.
class TFX_TakeHealthThunk
{
public:
	int TakeHealth(float flHealth, int bitsDamageType)
	{
		return TFX_TakeHealth(this, flHealth, bitsDamageType);
	}
};

static void *TFX_HookFuncPtr()
{
	int (TFX_TakeHealthThunk::*pMemberFn)(float, int) = &TFX_TakeHealthThunk::TakeHealth;
	return (void *)*reinterpret_cast<TakeHealthFn *>(&pMemberFn);
}
#else
static void *TFX_HookFuncPtr()
{
	return (void *)TFX_TakeHealth;
}
#endif

void TFX_HookTakeHealth(edict_t *pPlayer)
{
	if (g_slot || FNullEnt(pPlayer))
		return;
	void *pThis = GET_PRIVATE(pPlayer);
	if (!pThis)
		return;
	void **vtable = *(void ***)pThis;
	void *hook = TFX_HookFuncPtr();
	if (vtable[TFX_VT_TAKEHEALTH_SLOT] == hook)
		return;
	g_origTakeHealth = (TakeHealthFn)WC_SetVFunc(pThis, TFX_VT_TAKEHEALTH_SLOT, hook);
	if (g_origTakeHealth)
	{
		g_slot = vtable + TFX_VT_TAKEHEALTH_SLOT;
		g_hookFn = hook;
		TFX_Trace("TakeHealth: player hook installed at vtable slot %d (was %p)",
			TFX_VT_TAKEHEALTH_SLOT, (void *)g_origTakeHealth);
	}
	else
		TFX_Trace("TakeHealth: could not patch the player vtable - fix #7 (doors and caltrops) is unavailable");
}

void TFX_UnhookTakeHealth(void)
{
	if (!g_slot || !g_origTakeHealth)
		return;
	// Only if the slot still holds ours: anything patched over us since is
	// chaining to us, and pulling us out from under it is no better.
	if (*g_slot == g_hookFn)
	{
		WC_WriteVSlot(g_slot, (void *)g_origTakeHealth);
		TFX_Trace("TakeHealth: player hook removed");
	}
	else
		TFX_Trace("TakeHealth: hook NOT removed - something else patched the slot since");
	g_slot = NULL;
	g_hookFn = NULL;
	g_origTakeHealth = NULL;
}
