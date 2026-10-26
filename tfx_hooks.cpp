// tfx_hooks.cpp - the game-DLL, new-DLL and engine function hooks metamod
// calls, each handing straight on to tfx_fixes.cpp.
//
// EVERY hook here must end in RETURN_META. Metamod reads the result after each
// call to decide whether to run the real function; a hook that leaves it unset
// leaves whatever the previous call put there.

#include "tfx.h"

#include <dllapi.h>

// ---------------------------------------------------------------------------
// Game DLL functions, before the game's own
static void TFX_ServerActivate(edict_t *pEdictList, int edictCount, int clientMax)
{
	TFX_Trace("==== ServerActivate: %s ====", STRING(gpGlobals->mapname));
	TFX_ConfigLoad();
	for (int i = 0; i < FIX_COUNT; i++)
		g_tfxCountMap[i] = 0;
	TFX_GameMapStart();
	TFX_FixesMapStart();
	RETURN_META(MRES_IGNORED);
}

static void TFX_ClientPutInServerHook(edict_t *pEntity)
{
	TFX_ClientPutInServer(pEntity);
	RETURN_META(MRES_IGNORED);
}

static void TFX_ClientDisconnectHook(edict_t *pEntity)
{
	TFX_ClientDisconnect(pEntity);
	RETURN_META(MRES_IGNORED);
}

static void TFX_ClientCommandHook(edict_t *pEntity)
{
	TFX_ClientCommandPre(pEntity, CMD_ARGV(0));
	RETURN_META(MRES_IGNORED);
}

static void TFX_ThinkHook(edict_t *pent)
{
	TFX_ThinkPre(pent);
	RETURN_META(MRES_IGNORED);
}

static void TFX_TouchHook(edict_t *pentTouched, edict_t *pentOther)
{
	if (TFX_TouchPre(pentTouched, pentOther))
		RETURN_META(MRES_SUPERCEDE);   // we did the push ourselves (fix push_fps)
	RETURN_META(MRES_IGNORED);
}

static bool s_warnedAmxx = false;

static void TFX_StartFrameHook(void)
{
	// Running alongside the AMX Mod X plugin these replace means most fixes
	// happen twice, some of them fighting each other. Say so once a map.
	if (!s_warnedAmxx && CVAR_GET_POINTER("bug_fixes_tfc_ver"))
	{
		s_warnedAmxx = true;
		TFX_Trace("WARNING: the AMX Mod X plugin \"Bug Fixes TFC\" is also running - disable one of the two");
		SERVER_PRINT("[TFC Fixes] WARNING: the AMX Mod X \"Bug Fixes TFC\" plugin is also loaded - remove it from amxmodx/configs/plugins.ini\n");
	}
	TFX_FixesFrame();
	RETURN_META(MRES_IGNORED);
}

static void TFX_ServerDeactivateHook(void)
{
	s_warnedAmxx = false;
	RETURN_META(MRES_IGNORED);
}

static DLL_FUNCTIONS gFunctionTable;

C_DLLEXPORT FORCE_STACK_ALIGN int GetEntityAPI2(DLL_FUNCTIONS *pFunctionTable, int *interfaceVersion)
{
	if (!pFunctionTable)
		return FALSE;
	if (*interfaceVersion != INTERFACE_VERSION)
	{
		*interfaceVersion = INTERFACE_VERSION;
		return FALSE;
	}
	// By name - a positional initialiser that miscounts hooks the wrong
	// function without a word.
	memset(&gFunctionTable, 0, sizeof(gFunctionTable));
	gFunctionTable.pfnServerActivate    = TFX_ServerActivate;
	gFunctionTable.pfnServerDeactivate  = TFX_ServerDeactivateHook;
	gFunctionTable.pfnClientPutInServer = TFX_ClientPutInServerHook;
	gFunctionTable.pfnClientDisconnect  = TFX_ClientDisconnectHook;
	gFunctionTable.pfnClientCommand     = TFX_ClientCommandHook;
	gFunctionTable.pfnThink             = TFX_ThinkHook;
	gFunctionTable.pfnTouch             = TFX_TouchHook;
	gFunctionTable.pfnStartFrame        = TFX_StartFrameHook;
	memcpy(pFunctionTable, &gFunctionTable, sizeof(DLL_FUNCTIONS));
	return TRUE;
}

// ---------------------------------------------------------------------------
// Game DLL functions, after the game's own
static void TFX_ClientCommandPostHook(edict_t *pEntity)
{
	TFX_ClientCommandPost(pEntity);
	RETURN_META(MRES_IGNORED);
}

static void TFX_ThinkPostHook(edict_t *pent)
{
	TFX_ThinkPost(pent);
	RETURN_META(MRES_IGNORED);
}

static void TFX_TouchPostHook(edict_t *pentTouched, edict_t *pentOther)
{
	TFX_TouchPost(pentTouched, pentOther);
	RETURN_META(MRES_IGNORED);
}

static void TFX_UsePostHook(edict_t *pentUsed, edict_t *pentOther)
{
	TFX_UsePost(pentUsed);
	RETURN_META(MRES_IGNORED);
}

static void TFX_UpdateClientDataPostHook(const struct edict_s *ent, int sendweapons, struct clientdata_s *cd)
{
	TFX_UpdateClientDataPost(ent, cd);
	RETURN_META(MRES_IGNORED);
}

static DLL_FUNCTIONS gFunctionTablePost;

C_DLLEXPORT FORCE_STACK_ALIGN int GetEntityAPI2_Post(DLL_FUNCTIONS *pFunctionTable, int *interfaceVersion)
{
	if (!pFunctionTable)
		return FALSE;
	if (*interfaceVersion != INTERFACE_VERSION)
	{
		*interfaceVersion = INTERFACE_VERSION;
		return FALSE;
	}
	memset(&gFunctionTablePost, 0, sizeof(gFunctionTablePost));
	gFunctionTablePost.pfnClientCommand    = TFX_ClientCommandPostHook;
	gFunctionTablePost.pfnThink            = TFX_ThinkPostHook;
	gFunctionTablePost.pfnTouch            = TFX_TouchPostHook;
	gFunctionTablePost.pfnUse              = TFX_UsePostHook;
	gFunctionTablePost.pfnUpdateClientData = TFX_UpdateClientDataPostHook;
	memcpy(pFunctionTable, &gFunctionTablePost, sizeof(DLL_FUNCTIONS));
	return TRUE;
}

// ---------------------------------------------------------------------------
// The newer game DLL functions: ShouldCollide (fix #1)
static int TFX_ShouldCollideHook(edict_t *pentTouched, edict_t *pentOther)
{
	if (!TFX_ShouldCollide(pentTouched, pentOther))
		RETURN_META_VALUE(MRES_SUPERCEDE, 0);
	RETURN_META_VALUE(MRES_IGNORED, 1);
}

static NEW_DLL_FUNCTIONS gNewFunctionTable;

C_DLLEXPORT FORCE_STACK_ALIGN int GetNewDLLFunctions(NEW_DLL_FUNCTIONS *pNewFunctionTable, int *interfaceVersion)
{
	if (!pNewFunctionTable)
		return FALSE;
	if (*interfaceVersion != NEW_DLL_FUNCTIONS_VERSION)
	{
		*interfaceVersion = NEW_DLL_FUNCTIONS_VERSION;
		return FALSE;
	}
	memset(&gNewFunctionTable, 0, sizeof(gNewFunctionTable));
	gNewFunctionTable.pfnShouldCollide = TFX_ShouldCollideHook;
	memcpy(pNewFunctionTable, &gNewFunctionTable, sizeof(NEW_DLL_FUNCTIONS));
	return TRUE;
}

// ---------------------------------------------------------------------------
// Engine functions the game calls, before the engine
static void TFX_SetOriginHook(edict_t *e, const float *rgflOrigin)
{
	if (TFX_SetOriginPre(e, rgflOrigin))
		RETURN_META(MRES_SUPERCEDE);   // we set it ourselves
	RETURN_META(MRES_IGNORED);
}

static void TFX_MessageBeginHook(int msg_dest, int msg_type, const float *pOrigin, edict_t *ed)
{
	TFX_MessageBegin(msg_dest, msg_type, ed);
	RETURN_META(MRES_IGNORED);
}

static enginefuncs_t gEngineFunctions;

C_DLLEXPORT FORCE_STACK_ALIGN int GetEngineFunctions(enginefuncs_t *pengfuncsFromEngine, int *interfaceVersion)
{
	if (!pengfuncsFromEngine)
		return FALSE;
	if (*interfaceVersion != ENGINE_INTERFACE_VERSION)
	{
		*interfaceVersion = ENGINE_INTERFACE_VERSION;
		return FALSE;
	}
	memset(&gEngineFunctions, 0, sizeof(gEngineFunctions));
	gEngineFunctions.pfnSetOrigin    = TFX_SetOriginHook;
	gEngineFunctions.pfnMessageBegin = TFX_MessageBeginHook;
	memcpy(pengfuncsFromEngine, &gEngineFunctions, sizeof(enginefuncs_t));
	return TRUE;
}

// ---------------------------------------------------------------------------
// Engine functions the game calls, after the engine
static void TFX_SetOriginPostHook(edict_t *e, const float *rgflOrigin)
{
	TFX_SetOriginPost(e, rgflOrigin);
	RETURN_META(MRES_IGNORED);
}

static void TFX_SetSizePostHook(edict_t *e, const float *rgflMin, const float *rgflMax)
{
	TFX_SetSizePost(e);
	RETURN_META(MRES_IGNORED);
}

static void TFX_TraceLinePostHook(const float *v1, const float *v2, int fNoMonsters, edict_t *pentToSkip, TraceResult *ptr)
{
	TFX_TraceLinePost(fNoMonsters, pentToSkip, ptr);
	RETURN_META(MRES_IGNORED);
}

static void TFX_TraceHullPostHook(const float *v1, const float *v2, int fNoMonsters, int hullNumber, edict_t *pentToSkip, TraceResult *ptr)
{
	TFX_TraceHullPost(hullNumber, pentToSkip, ptr);
	RETURN_META(MRES_IGNORED);
}

static void TFX_MessageEndPostHook(void)
{
	TFX_MessageEndPost();
	RETURN_META(MRES_IGNORED);
}

static enginefuncs_t gEngineFunctionsPost;

C_DLLEXPORT FORCE_STACK_ALIGN int GetEngineFunctions_Post(enginefuncs_t *pengfuncsFromEngine, int *interfaceVersion)
{
	if (!pengfuncsFromEngine)
		return FALSE;
	if (*interfaceVersion != ENGINE_INTERFACE_VERSION)
	{
		*interfaceVersion = ENGINE_INTERFACE_VERSION;
		return FALSE;
	}
	memset(&gEngineFunctionsPost, 0, sizeof(gEngineFunctionsPost));
	gEngineFunctionsPost.pfnSetOrigin  = TFX_SetOriginPostHook;
	gEngineFunctionsPost.pfnSetSize    = TFX_SetSizePostHook;
	gEngineFunctionsPost.pfnTraceLine  = TFX_TraceLinePostHook;
	gEngineFunctionsPost.pfnTraceHull  = TFX_TraceHullPostHook;
	gEngineFunctionsPost.pfnMessageEnd = TFX_MessageEndPostHook;
	memcpy(pengfuncsFromEngine, &gEngineFunctionsPost, sizeof(enginefuncs_t));
	return TRUE;
}
