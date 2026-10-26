// tfx_offsets.h - where TFC keeps the fields these fixes read and write.
//
// Linux numbers are from tfc.so's own debug information (gdb "ptype /o").
// Windows numbers were read off the current tfc.dll's machine code, at the
// places the game itself uses each field, and agree with AMX Mod X's gamedata
// (data/gamedata/common.games/entities.games/tfc). They are byte offsets from
// the start of the C++ object (GET_PRIVATE), not from pev.
//
// Checked against the TFC build of 2026-05 (tfc.dll 868,192 bytes, tfc.so
// 6,746,012 bytes). The AMX Mod X plugin's teleporter offsets (1700/1708/1712
// on Windows) are 4 bytes short of where this build keeps those fields - its
// teleporter fix reads the wrong memory here.
#ifndef TFC_FIXES_OFFSETS_H
#define TFC_FIXES_OFFSETS_H

#ifdef _WIN32
// CBaseEntity (TFC adds its player state to the base class)
	#define OFS_NO_GRENADES_1          56
	#define OFS_NO_GRENADES_2          60
	#define OFS_UNABLE_SPY_TELEPORT   104    // is_unableto_spy_or_teleport
	#define OFS_REMOVE_GRENADE        116    // bRemoveGrenade
	#define OFS_TFSTATE               140
	#define OFS_PRIMED_GREN_TYPE      152    // m_iPrimedGrenType
	#define OFS_PFN_THINK              16    // m_pfnThink (4 bytes on MSVC)
	#define OFS_LEG_DAMAGE            432    // only read, for the log (AMX Mod X gamedata)
// CTFTeleporter
	#define OFS_TELE_PLAYER          1704    // m_pPlayer
	#define OFS_TELE_TYPE            1712    // m_iType: 4 entrance, 5 exit
	#define OFS_TELE_STATE           1716    // m_iState
	#define OFS_TELE_NEXTTHINK       1728    // m_flMyNextThink
#else
	#define OFS_NO_GRENADES_1          72
	#define OFS_NO_GRENADES_2          76
	#define OFS_UNABLE_SPY_TELEPORT   120
	#define OFS_REMOVE_GRENADE        132
	#define OFS_TFSTATE               156
	#define OFS_PRIMED_GREN_TYPE      168
	#define OFS_PFN_THINK              16    // m_pfnThink {function, this-adjust} (8 bytes on GCC)
	#define OFS_LEG_DAMAGE            448
	#define OFS_TELE_PLAYER          1724
	#define OFS_TELE_TYPE            1732
	#define OFS_TELE_STATE           1736
	#define OFS_TELE_NEXTTHINK       1748
#endif

// tfstate bits
#define TFSTATE_GRENPRIMED     0x0001   // a grenade is primed in hand
#define TFSTATE_GRENTHROWING   0x0400   // the player has let go of it
#define TFSTATE_CANT_MOVE      0x10000  // frozen (being teleported)

// CTFTeleporter states (TeleporterThink's switch)
#define TELE_STATE_READY        2
#define TELE_STATE_SENDING      3       // entrance: next think sends m_pPlayer to the exit
#define TELE_STATE_RECEIVING    4       // exit: next think puts m_pPlayer on the pad
#define TELE_STATE_ARRIVED      5       // exit: next think unfreezes m_pPlayer, clears both pads
#define TELE_STATE_RECHARGE     6
#define TELE_TYPE_ENTRANCE      4
#define TELE_TYPE_EXIT          5

template <typename T>
inline T &TFX_Field(void *obj, int ofs) { return *(T *)((char *)obj + ofs); }

#endif
