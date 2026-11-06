// gamedll_vtable.h - patching one slot of a game class's vtable, and putting
// it back. Copied from TFC Monsters / Weapon Changer Plus (which also explain
// how the slot numbers were confirmed); only the slot this plugin uses is kept.

#ifndef TFC_FIXES_GAMEDLL_VTABLE_H
#define TFC_FIXES_GAMEDLL_VTABLE_H

#ifdef _WIN32
	// CBaseEntity::TakeHealth(float flHealth, int bitsDamageType). tfc.dll's
	// medikit calls it through vtable+0x34 with 200.0 (slot 13); tfc.so through
	// slot 14 - GCC puts two destructors where MSVC has one.
	#define TFX_VT_TAKEHEALTH_SLOT     13
	// CBasePlayer::TeamFortress_TakeConcussionBlast(entvars_s *, float) - the
	// knockback the conc grenade gives the player. tfc.so puts it at slot 63
	// (read with scratchpad/vtslot.py); tfc.dll is slot 62, one lower because
	// GCC emits two CBasePlayer destructor slots where MSVC emits one, the same
	// shift that makes TakeHealth slot 14 on Linux and 13 here. Confirmed from
	// tfc.dll, not assumed: the function (at 0x10046c90) is the only user of the
	// blast's 0.03 `double` constant, and its address sits at the vtable slot
	// (0x1009d224) that is base+62 (base 0x1009d12c, right after the RTTI
	// complete-object-locator pointer; the same base puts TakeHealth at slot 13
	// / offset 0x34, as fix #7 already uses). The function itself calls Concuss
	// through vtable+0xfc (slot 63), matching tfc.so's slot-64 Concuss one lower.
	#define TFX_VT_CONCBLAST_SLOT      62
	#define WC_THISCALL __thiscall
	#include <windows.h>
#else
	#define TFX_VT_TAKEHEALTH_SLOT     14
	#define TFX_VT_CONCBLAST_SLOT      63
	#define WC_THISCALL
	#include <sys/mman.h>
	#include <unistd.h>
	#include <stdint.h>
#endif

// CBaseEntity's `entvars_t *pev` member sits right after the vtable
// pointer, at offset 4 - confirmed directly from tfc.so's DWARF (member
// list for the concrete CBaseEntity class definition: `_vptr.CBaseEntity`
// at offset 0, `pev` at offset 4). This has also been true of every
// public HL SDK-derived mod's cbase.h, so it's about as safe an
// assumption as anything in this file, but it's independently confirmed
// here rather than just assumed.
inline entvars_t *WC_GetPev(void *pThis)
{
	return *(entvars_t **)((char *)pThis + 4);
}

// Read slot `slot` out of `pThis`'s vtable.
inline void *WC_GetVFunc(void *pThis, int slot)
{
	void **vtable = *(void ***)pThis;
	return vtable[slot];
}

// Overwrite slot `slot` in `pThis`'s vtable with `pNewFunc`, returning the
// previous value (the real function, to chain to). NOTE: this patches the
// class's *shared* vtable (there is exactly one "vtable for CBasePlayer"
// blob, referenced by every CBasePlayer instance), so a single call here
// affects every player entity, present and future, for as long as the
// plugin is attached.
//
// IMPORTANT: the vtable itself lives in a read-only section of the game
// DLL's image (.rdata under MSVC, .rodata/.data.rel.ro under GCC/ELF) -
// the OS maps that page(s) PAGE_READONLY. A plain write to it raises
// STATUS_ACCESS_VIOLATION (Windows) / SIGSEGV (Linux) immediately - this
// is not optional, and is the actual cause of the hard crash seen when
// this was first tried without the protection flip below. We have to
// temporarily make the containing page(s) writable, do the write, then
// restore the original protection.
inline void *WC_SetVFunc(void *pThis, int slot, void *pNewFunc)
{
	void **vtable = *(void ***)pThis;
	void **slotPtr = vtable + slot;

#ifdef _WIN32
	DWORD oldProtect;
	if (!VirtualProtect((LPVOID)slotPtr, sizeof(void *), PAGE_READWRITE, &oldProtect))
	{
		ALERT(at_console, "[TFC Fixes] VirtualProtect failed (err=%lu) - refusing to patch vtable slot %d\n",
			GetLastError(), slot);
		return NULL;
	}

	void *old = *slotPtr;
	*slotPtr = pNewFunc;

	DWORD dummy;
	VirtualProtect((LPVOID)slotPtr, sizeof(void *), oldProtect, &dummy);
	return old;
#else
	long pageSize = sysconf(_SC_PAGESIZE);
	uintptr_t start = (uintptr_t)slotPtr & ~(uintptr_t)(pageSize - 1);
	// Cover the case where the pointer straddles a page boundary.
	uintptr_t end = ((uintptr_t)slotPtr + sizeof(void *) + pageSize - 1) & ~(uintptr_t)(pageSize - 1);
	size_t len = (size_t)(end - start);

	if (mprotect((void *)start, len, PROT_READ | PROT_WRITE) != 0)
	{
		ALERT(at_console, "[TFC Fixes] mprotect(RW) failed - refusing to patch vtable slot %d\n", slot);
		return NULL;
	}

	void *old = *slotPtr;
	*slotPtr = pNewFunc;

	mprotect((void *)start, len, PROT_READ);
	return old;
#endif
}

// Put a vtable slot back: write `fn` at `slotPtr` (an address inside a
// vtable, as saved when the slot was patched), with the same page
// protection dance as WC_SetVFunc. Used when the plugin unloads, so the game
// is never left calling into a DLL that is gone.
inline bool WC_WriteVSlot(void **slotPtr, void *fn)
{
	if (!slotPtr)
		return false;
#ifdef _WIN32
	DWORD oldProtect;
	if (!VirtualProtect((LPVOID)slotPtr, sizeof(void *), PAGE_READWRITE, &oldProtect))
		return false;
	*slotPtr = fn;
	DWORD dummy;
	VirtualProtect((LPVOID)slotPtr, sizeof(void *), oldProtect, &dummy);
	return true;
#else
	long pageSize = sysconf(_SC_PAGESIZE);
	uintptr_t start = (uintptr_t)slotPtr & ~(uintptr_t)(pageSize - 1);
	uintptr_t end = ((uintptr_t)slotPtr + sizeof(void *) + pageSize - 1) & ~(uintptr_t)(pageSize - 1);
	size_t len = (size_t)(end - start);
	if (mprotect((void *)start, len, PROT_READ | PROT_WRITE) != 0)
		return false;
	*slotPtr = fn;
	mprotect((void *)start, len, PROT_READ);
	return true;
#endif
}

#endif // TFC_FIXES_GAMEDLL_VTABLE_H
