// meta_api.cpp - plugin identity and metamod attach/detach.

#include "tfx.h"

C_DLLEXPORT int GetEntityAPI2(DLL_FUNCTIONS *pFunctionTable, int *interfaceVersion);
C_DLLEXPORT int GetEntityAPI2_Post(DLL_FUNCTIONS *pFunctionTable, int *interfaceVersion);
C_DLLEXPORT int GetNewDLLFunctions(NEW_DLL_FUNCTIONS *pNewFunctionTable, int *interfaceVersion);
C_DLLEXPORT int GetEngineFunctions(enginefuncs_t *pengfuncsFromEngine, int *interfaceVersion);
C_DLLEXPORT int GetEngineFunctions_Post(enginefuncs_t *pengfuncsFromEngine, int *interfaceVersion);

static META_FUNCTIONS gMetaFunctionTable =
{
	NULL,                    // pfnGetEntityAPI
	NULL,                    // pfnGetEntityAPI_Post
	GetEntityAPI2,           // pfnGetEntityAPI2
	GetEntityAPI2_Post,      // pfnGetEntityAPI2_Post
	GetNewDLLFunctions,      // pfnGetNewDLLFunctions
	NULL,                    // pfnGetNewDLLFunctions_Post
	GetEngineFunctions,      // pfnGetEngineFunctions
	GetEngineFunctions_Post, // pfnGetEngineFunctions_Post
};

plugin_info_t Plugin_info =
{
	META_INTERFACE_VERSION,
	"TFC Fixes",
	"1.1.0",
	"2026/10/26",
	"custom",
	"",
	"TFCFIX",
	PT_ANYTIME,
	PT_ANYPAUSE,
};

meta_globals_t *gpMetaGlobals;
gamedll_funcs_t *gpGamedllFuncs;
mutil_funcs_t *gpMetaUtilFuncs;

static bool s_commandsRegistered = false;

C_DLLEXPORT FORCE_STACK_ALIGN int Meta_Query(char * /*ifvers*/, plugin_info_t **pPlugInfo,
		mutil_funcs_t *pMetaUtilFuncs)
{
	*pPlugInfo = &Plugin_info;
	gpMetaUtilFuncs = pMetaUtilFuncs;
	return TRUE;
}

C_DLLEXPORT FORCE_STACK_ALIGN int Meta_Attach(PLUG_LOADTIME /*now*/,
		META_FUNCTIONS *pFunctionTable, meta_globals_t *pMGlobals,
		gamedll_funcs_t *pGamedllFuncs)
{
	if (!pMGlobals || !pFunctionTable)
		return FALSE;
	gpMetaGlobals = pMGlobals;
	gpGamedllFuncs = pGamedllFuncs;
	memcpy(pFunctionTable, &gMetaFunctionTable, sizeof(META_FUNCTIONS));

	TFX_TraceRotate();
	TFX_Trace("==== Meta_Attach - TFC Fixes %s loading ====", Plugin_info.version);
	TFX_LogModuleIdentity();
	TFX_ConfigLoad();
	TFX_GameResolve();
	if (!s_commandsRegistered)
	{
		TFX_RegisterCommands();
		s_commandsRegistered = true;
	}
	return TRUE;
}

C_DLLEXPORT FORCE_STACK_ALIGN int Meta_Detach(PLUG_LOADTIME /*now*/,
		PL_UNLOAD_REASON /*reason*/)
{
	// The vtable slot goes back to the game's own function: nothing may go on
	// calling into a DLL that is about to be gone.
	TFX_UnhookTakeHealth();
	TFX_Trace("==== Meta_Detach ====");
	return TRUE;
}
