// tfx_game.cpp - finding the game DLL's own think functions by name.
//
// Several fixes need to know what an entity is about to do on its next think:
// a grenade still in someone's hand (GrenadePrimed), a grenade about to go off
// (Detonate), a teleporter (TeleporterThink). TFC keeps that in the entity's
// m_pfnThink, and both tfc.dll and tfc.so export these functions by name (the
// engine's save/restore needs them), so instead of guessing from appearances
// we ask the game DLL where they are and compare.

#include "tfx.h"
#include "tfx_offsets.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

struct TfxThinkName
{
	const char *label;
	const char *win;     // MSVC decorated name
	const char *linux_;  // Itanium mangled name
};

static const TfxThinkName s_names[THINK_COUNT] =
{
	{ "CTFPrimeGrenade::GrenadePrimed",
	  "?GrenadePrimed@CTFPrimeGrenade@@QAEXXZ",
	  "_ZN15CTFPrimeGrenade13GrenadePrimedEv" },
	{ "CTFCaltropGrenade::CaltropGrenadePrimed",
	  "?CaltropGrenadePrimed@CTFCaltropGrenade@@QAEXXZ",
	  "_ZN17CTFCaltropGrenade20CaltropGrenadePrimedEv" },
	{ "CGrenade::Detonate",
	  "?Detonate@CGrenade@@QAEXXZ",
	  "_ZN8CGrenade8DetonateEv" },
	{ "CTFTeleporter::TeleporterThink",
	  "?TeleporterThink@CTFTeleporter@@QAEXXZ",
	  "_ZN13CTFTeleporter15TeleporterThinkEv" },
};

static void *s_addr[THINK_COUNT];
static bool s_resolved = false;

// Any function that lives in the game DLL tells us which module it is.
static void *TFX_GameFunctionAddress(void)
{
	if (gpGamedllFuncs && gpGamedllFuncs->dllapi_table && gpGamedllFuncs->dllapi_table->pfnThink)
		return (void *)gpGamedllFuncs->dllapi_table->pfnThink;
	if (gpGamedllFuncs && gpGamedllFuncs->dllapi_table && gpGamedllFuncs->dllapi_table->pfnSpawn)
		return (void *)gpGamedllFuncs->dllapi_table->pfnSpawn;
	return NULL;
}

bool TFX_GameResolve(void)
{
	if (s_resolved)
		return true;
	void *inGame = TFX_GameFunctionAddress();
	if (!inGame)
	{
		TFX_Trace("Game: no game DLL function to start from - fixes that need think functions are off");
		return false;
	}
	int found = 0;
#ifdef _WIN32
	HMODULE h = NULL;
	if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
			(LPCSTR)inGame, &h) || !h)
	{
		TFX_Trace("Game: could not identify the game DLL module");
		return false;
	}
	char path[MAX_PATH];
	path[0] = 0;
	GetModuleFileNameA(h, path, sizeof(path));
	TFX_Trace("Game: game DLL is %s", path);
	for (int i = 0; i < THINK_COUNT; i++)
	{
		s_addr[i] = (void *)GetProcAddress(h, s_names[i].win);
		if (s_addr[i])
			found++;
		TFX_Trace("Game:   %-40s %p", s_names[i].label, s_addr[i]);
	}
#else
	Dl_info info;
	if (!dladdr(inGame, &info) || !info.dli_fname)
	{
		TFX_Trace("Game: could not identify the game library");
		return false;
	}
	TFX_Trace("Game: game library is %s", info.dli_fname);
	void *h = dlopen(info.dli_fname, RTLD_NOW | RTLD_NOLOAD);
	if (!h)
	{
		TFX_Trace("Game: dlopen(RTLD_NOLOAD) failed");
		return false;
	}
	for (int i = 0; i < THINK_COUNT; i++)
	{
		s_addr[i] = dlsym(h, s_names[i].linux_);
		if (s_addr[i])
			found++;
		TFX_Trace("Game:   %-40s %p", s_names[i].label, s_addr[i]);
	}
	dlclose(h);   // balances our dlopen; the library stays loaded
#endif
	s_resolved = true;
	if (found != THINK_COUNT)
		TFX_Trace("Game: WARNING - %d of %d think functions not found; the fixes that need them stay off",
			THINK_COUNT - found, THINK_COUNT);
	return found == THINK_COUNT;
}

void TFX_GameMapStart(void)
{
	TFX_GameResolve();
}

bool TFX_ThinkKnown(int which)
{
	return which >= 0 && which < THINK_COUNT && s_addr[which] != NULL;
}

bool TFX_IsThinking(edict_t *e, int which)
{
	if (!TFX_ThinkKnown(which) || !TFX_ValidEnt(e))
		return false;
	// MSVC stores a pointer to a non-virtual member function as the bare
	// address; GCC as {address, this-adjustment}. The address comes first in
	// both, which is all we compare.
	void *fn = TFX_Field<void *>(e->pvPrivateData, OFS_PFN_THINK);
	return fn == s_addr[which];
}
