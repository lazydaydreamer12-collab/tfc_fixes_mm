// tfx_config.cpp - tfc_fixes.ini (beside the DLL), the counters, and the
// server commands:
//
//   tfx                  every fix, on or off, and how often it stepped in
//   tfx <fix> 0|1        switch one off or on until the next map (or tfx_reload)
//   tfx_reload           read tfc_fixes.ini again

#include "tfx.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

#define TFX_CONFIG_FILE "tfc_fixes.ini"

const TfxFixInfo g_tfxFixInfo[FIX_COUNT] =
{
	{ "ghost_nails",         "#1  Nails that hit each other no longer hang in the air" },
	{ "flag_in_wall",        "#2  Dropped flags cannot start inside a wall or ceiling" },
	{ "door_caltrops",       "#7  Doors no longer heal caltrop leg damage" },
	{ "teleport_death",      "#8  Dying mid-teleport cancels the teleport" },
	{ "teleport_flag",       "#9  Picking up a flag mid-teleport cancels it" },
	{ "teleport_leaver",     "    Leaving mid-teleport clears the teleporters" },
	{ "grenade_in_entity",   "#11 Grenades inside something do not hit everyone" },
	{ "nailgren_ceiling",    "#12 Nail grenades stop short of the ceiling" },
	{ "mirv_walls",          "#13 MIRV bomblets stay on the MIRV's side" },
	{ "grenade_spawn",       "#14 Grenades held at death drop at the body" },
	{ "dropitems_grenade",   "#15 Dropping a flag keeps your primed grenade" },
	{ "zero_health",         "#16 Death bug: alive under 1 health" },
	{ "grenade_jump_fps",    "#17 Hand-grenade jumps push forward at any fps" },
	{ "flag_toss",           "#18 Items can be dropped with something in front" },
	{ "stuck_menus",         "#19 Menus from last map are closed" },
	{ "push_fps",            "    Push pads launch the same at any fps/tick" },
};

TfxConfig g_tfx;
int g_tfxCountMap[FIX_COUNT];
int g_tfxCountTotal[FIX_COUNT];

void TFX_ConfigDefaults(void)
{
	for (int i = 0; i < FIX_COUNT; i++)
		g_tfx.on[i] = 1;
	g_tfx.logEach = 5;
	g_tfx.pushScale = 1.0f;     // push_fps: jump pads unscaled by default
	g_tfx.pushFanScale = 1.0f;  // push_fps: fans at full map strength by default
	g_tfx.pushMax = 0.0f;       // push_fps: no launch/lift cap by default
}

void TFX_Hit(int fix, const char *fmt, ...)
{
	if (fix < 0 || fix >= FIX_COUNT)
		return;
	g_tfxCountMap[fix]++;
	g_tfxCountTotal[fix]++;
	if (g_tfxCountMap[fix] > g_tfx.logEach)
		return;
	char msg[200];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(msg, sizeof(msg), fmt, ap);
	va_end(ap);
	msg[sizeof(msg) - 1] = 0;
	TFX_Trace("Fix %s: %s%s", g_tfxFixInfo[fix].key, msg,
		g_tfxCountMap[fix] == g_tfx.logEach ? "  (no more of these this map - see `tfx` for the count)" : "");
}

static int TFX_FixByKey(const char *key)
{
	for (int i = 0; i < FIX_COUNT; i++)
		if (!strcasecmp(key, g_tfxFixInfo[i].key))
			return i;
	return -1;
}

void TFX_ConfigLoad(void)
{
	TFX_ConfigDefaults();
	char path[300];
	if (!TFX_PathBesideModule(TFX_CONFIG_FILE, path, sizeof(path)))
		return;
	FILE *f = fopen(path, "r");
	if (!f)
	{
		TFX_Trace("Config: %s not found - every fix is on", path);
		return;
	}
	char line[256];
	int lineNo = 0, offCount = 0;
	while (fgets(line, sizeof(line), f))
	{
		lineNo++;
		char *s = TrimInPlace(line);
		if (!s[0] || s[0] == ';' || s[0] == '#' || (s[0] == '/' && s[1] == '/'))
			continue;
		for (char *c = s; *c; c++)
			if (*c == '=')
				*c = ' ';   // "key = 1" and "key=1" read the same as "key 1"
		char key[64];
		char *rest = SplitFirstToken(s, key, sizeof(key));
		int val = atoi(rest);
		if (!strcasecmp(key, "log_each"))
		{
			g_tfx.logEach = val < 0 ? 0 : val;
			continue;
		}
		if (!strcasecmp(key, "push_scale"))
		{
			float s = (float)atof(rest);
			g_tfx.pushScale = s > 0.0f ? s : 0.0f;
			continue;
		}
		if (!strcasecmp(key, "push_fan_scale"))
		{
			float s = (float)atof(rest);
			g_tfx.pushFanScale = s > 0.0f ? s : 0.0f;
			continue;
		}
		if (!strcasecmp(key, "push_max"))
		{
			float m = (float)atof(rest);
			g_tfx.pushMax = m > 0.0f ? m : 0.0f;
			continue;
		}
		int fix = TFX_FixByKey(key);
		if (fix < 0)
		{
			TFX_Trace("Config: line %d: unknown setting \"%s\" - ignored", lineNo, key);
			continue;
		}
		g_tfx.on[fix] = val ? 1 : 0;
		if (!val)
			offCount++;
	}
	fclose(f);
	TFX_Trace("Config: %s read - %d of %d fixes on", path, FIX_COUNT - offCount, FIX_COUNT);
}

static void TFX_Cmd_Status(void)
{
	int argc = CMD_ARGC();
	if (argc >= 3)
	{
		const char *key = CMD_ARGV(1);
		// Live push tuning knobs (until the next map or tfx_reload), like toggles.
		if (!strcasecmp(key, "push_scale") || !strcasecmp(key, "push_fan_scale") || !strcasecmp(key, "push_max"))
		{
			float v = (float)atof(CMD_ARGV(2));
			if (v < 0.0f) v = 0.0f;
			if (!strcasecmp(key, "push_scale")) g_tfx.pushScale = v;
			else if (!strcasecmp(key, "push_fan_scale")) g_tfx.pushFanScale = v;
			else g_tfx.pushMax = v;
			char tbuf[160];
			_snprintf_wc(tbuf, sizeof(tbuf) - 1, "[TFC Fixes] %s is now %.2f (until the next map or tfx_reload).\n", key, v);
			tbuf[sizeof(tbuf) - 1] = 0;
			SERVER_PRINT(tbuf);
			TFX_Trace("Command: %s set to %.2f", key, v);
			return;
		}
		int fix = TFX_FixByKey(key);
		if (fix < 0)
		{
			SERVER_PRINT("[TFC Fixes] No fix or setting by that name - type tfx for the list.\n");
			return;
		}
		g_tfx.on[fix] = atoi(CMD_ARGV(2)) ? 1 : 0;
		char buf[160];
		_snprintf_wc(buf, sizeof(buf) - 1, "[TFC Fixes] %s is now %s (until the next map or tfx_reload).\n",
			g_tfxFixInfo[fix].key, g_tfx.on[fix] ? "ON" : "OFF");
		buf[sizeof(buf) - 1] = 0;
		SERVER_PRINT(buf);
		TFX_Trace("Command: %s set %s", g_tfxFixInfo[fix].key, g_tfx.on[fix] ? "on" : "off");
		return;
	}
	char buf[256];
	SERVER_PRINT("[TFC Fixes] fix                 on   map  total  what it does\n");
	for (int i = 0; i < FIX_COUNT; i++)
	{
		_snprintf_wc(buf, sizeof(buf) - 1, "  %-19s %-3s %5d %6d  %s\n", g_tfxFixInfo[i].key,
			g_tfx.on[i] ? "on" : "OFF", g_tfxCountMap[i], g_tfxCountTotal[i], g_tfxFixInfo[i].title);
		buf[sizeof(buf) - 1] = 0;
		SERVER_PRINT(buf);
	}
	_snprintf_wc(buf, sizeof(buf) - 1, "  think functions found: %s   (tfx <fix> 0|1 to switch one; tfx_reload to re-read %s)\n",
		(TFX_ThinkKnown(THINK_GRENADE_PRIMED) && TFX_ThinkKnown(THINK_CALTROP_PRIMED)
		 && TFX_ThinkKnown(THINK_DETONATE) && TFX_ThinkKnown(THINK_TELEPORTER)) ? "all" : "NOT ALL - see fixes_trace.log",
		TFX_CONFIG_FILE);
	buf[sizeof(buf) - 1] = 0;
	SERVER_PRINT(buf);
	if (TFX_On(FIX_PUSH_FPS))
	{
		char cap[32];
		if (g_tfx.pushMax > 0.0f) _snprintf_wc(cap, sizeof(cap) - 1, "%.0f u/s", g_tfx.pushMax); else strcpy(cap, "none");
		cap[sizeof(cap) - 1] = 0;
		_snprintf_wc(buf, sizeof(buf) - 1, "  push_fps: pad launch x%.2f, fan lift x%.2f, cap %s   (set live: tfx push_scale|push_fan_scale|push_max <v>)\n",
			g_tfx.pushScale, g_tfx.pushFanScale, cap);
		buf[sizeof(buf) - 1] = 0;
		SERVER_PRINT(buf);
	}
}

static void TFX_Cmd_Reload(void)
{
	TFX_ConfigLoad();
	SERVER_PRINT("[TFC Fixes] tfc_fixes.ini read again.\n");
}

void TFX_RegisterCommands(void)
{
	REG_SVR_COMMAND("tfx", TFX_Cmd_Status);
	REG_SVR_COMMAND("tfx_reload", TFX_Cmd_Reload);
}
