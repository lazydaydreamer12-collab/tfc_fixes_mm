// fxsim.cpp - fake engine + fake metamod around the real tfc.so and plugin.
//
// The engine side is just enough of GoldSrc for TFC's own code to run: edicts,
// strings, private data, cvars, messages (recorded), and traces against a list
// of world boxes and the entities' bounding boxes. There is no physics: things
// move only when the game sets their origin. The metamod side calls the
// plugin's hooks before and after the game's functions and in front of the
// engine functions the plugin hooks, with metamod's SUPERCEDE rules.
#include "fxsim.h"
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <math.h>
#include <map>

globalvars_t G;
edict_t ED[MAXE];
DLL_FUNCTIONS GAME;
NEW_DLL_FUNCTIONS GAME_NEW;
void *g_game = NULL;
bool g_pluginOn = true;
std::vector<WorldBox> g_world;
int g_lastSetViewTo[64];
std::vector<std::string> g_msgLog, g_printLog;
int g_quiet = 1;
int g_fail = 0, g_pass = 0;

// tfc.so (with its debug symbols - nm reads a few non-exported globals) and
// the plugin under test: $TFC_SO and $TFX_PLUGIN, or ./tfc.so and the plugin
// built one folder up.
static const char *GamePath(void) { const char *p = getenv("TFC_SO"); return p ? p : "./tfc.so"; }
static const char *PluginPath(void) { const char *p = getenv("TFX_PLUGIN"); return p ? p : "../tfc_fixes_mm_mm_i386.so"; }

static enginefuncs_t ENG;        // the real (fake) engine
static enginefuncs_t GAME_ENG;   // what tfc.so is given: plugin hooks in front
static enginefuncs_t P_ENG, P_ENGP;
static DLL_FUNCTIONS P_DLL, P_DLLP;
static NEW_DLL_FUNCTIONS P_NEW;
static META_FUNCTIONS MF;
static meta_globals_t MG;
static gamedll_funcs_t GF;
static mutil_funcs_t MU;
static plugin_info_t *PINFO;

int Check(bool ok, const char *what)
{
	printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
	if (ok) g_pass++; else g_fail++;
	return ok;
}

static std::map<int, int> s_stubSeen;
static void StubHit(int n)
{
	if (!s_stubSeen[n]++)
		printf("  (engine stub %d called)\n", n);
}
#include "stubs.inc"

// ---------------------------------------------------------------------------
static int IdxOf(const edict_t *e) { return e ? (int)(e - ED) : -1; }
static char s_strbase[4] = "";
static const char *Str(int s) { return G.pStringBase + s; }
static int MK(const char *s) { return (int)(s - G.pStringBase); }

static int ePrecache(char *s) { static int n = 10; return n++; }
static std::map<std::string, int> s_models;
static int eModelIndex(const char *m) { auto it = s_models.find(m); if (it != s_models.end()) return it->second; int n = (int)s_models.size() + 1; s_models[m] = n; return n; }
static void eSetModel(edict_t *e, const char *m) { e->v.model = MK(m); e->v.modelindex = eModelIndex(m); }
static int eModelFrames(int) { return 1; }
static void Relink(edict_t *e) { e->v.absmin = e->v.origin + e->v.mins - Vector(1, 1, 1); e->v.absmax = e->v.origin + e->v.maxs + Vector(1, 1, 1); }
static void eSetSize(edict_t *e, const float *mn, const float *mx) { e->v.mins = Vector(mn); e->v.maxs = Vector(mx); e->v.size = e->v.maxs - e->v.mins; Relink(e); }
static void eSetOrigin(edict_t *e, const float *o) { e->v.origin = Vector(o); Relink(e); }
static float eVecToYaw(const float *v) { if (v[0] == 0 && v[1] == 0) return 0; float y = atan2f(v[1], v[0]) * 180 / M_PI; if (y < 0) y += 360; return y; }
static void eVecToAngles(const float *in, float *out)
{
	float yaw, pitch;
	if (in[1] == 0 && in[0] == 0) { yaw = 0; pitch = in[2] > 0 ? 90 : 270; }
	else { yaw = atan2f(in[1], in[0]) * 180 / M_PI; if (yaw < 0) yaw += 360; float f = sqrtf(in[0] * in[0] + in[1] * in[1]); pitch = atan2f(in[2], f) * 180 / M_PI; if (pitch < 0) pitch += 360; }
	out[0] = pitch; out[1] = yaw; out[2] = 0;
}
static void AngleVectors(const float *a, float *f, float *r, float *u)
{
	float sy = sinf(a[1] * M_PI / 180), cy = cosf(a[1] * M_PI / 180), sp = sinf(a[0] * M_PI / 180), cp = cosf(a[0] * M_PI / 180), sr = sinf(a[2] * M_PI / 180), cr = cosf(a[2] * M_PI / 180);
	if (f) { f[0] = cp * cy; f[1] = cp * sy; f[2] = -sp; }
	if (r) { r[0] = -sr * sp * cy + cr * sy; r[1] = -sr * sp * sy - cr * cy; r[2] = -sr * cp; }
	if (u) { u[0] = cr * sp * cy + sr * sy; u[1] = cr * sp * sy - sr * cy; u[2] = cr * cp; }
}
static void eMakeVectors(const float *a) { AngleVectors(a, G.v_forward, G.v_right, G.v_up); }
static void eAngleVectors(const float *a, float *f, float *r, float *u) { AngleVectors(a, f, r, u); }

static edict_t *eFindByString(edict_t *start, const char *field, const char *val)
{
	int from = start ? IdxOf(start) + 1 : 0;
	for (int i = from; i < MAXE; i++)
	{
		edict_t *e = &ED[i];
		if (e->free) continue;
		int s = 0;
		if (!strcmp(field, "classname")) s = e->v.classname;
		else if (!strcmp(field, "targetname")) s = e->v.targetname;
		else if (!strcmp(field, "target")) s = e->v.target;
		else if (!strcmp(field, "netname")) s = e->v.netname;
		if (s && !strcmp(Str(s), val)) return e;
	}
	return &ED[0];
}
static edict_t *eFindInSphere(edict_t *start, const float *org, float rad)
{
	int from = start ? IdxOf(start) + 1 : 1;
	for (int i = from; i < MAXE; i++)
	{
		edict_t *e = &ED[i];
		if (e->free || !e->v.classname) continue;
		Vector d = e->v.origin - Vector(org);
		if (d.Length() <= rad) return e;
	}
	return &ED[0];
}

static int s_serial = 1;
static edict_t *eCreateEntity(void)
{
	for (int i = G.maxClients + 1; i < MAXE; i++)
		if (ED[i].free)
		{
			edict_t *e = &ED[i];
			int ser = e->serialnumber;
			memset(e, 0, sizeof(*e));
			e->serialnumber = ser + 1;
			e->v.pContainingEntity = e;
			return e;
		}
	printf("OUT OF EDICTS\n"); exit(2);
}
static void eFreePriv(edict_t *e) { if (e->pvPrivateData) { free(e->pvPrivateData); e->pvPrivateData = NULL; } }
static void eRemoveEntity(edict_t *e)
{
	if (!e || e == &ED[0]) return;
	if (GAME_NEW.pfnOnFreeEntPrivateData) GAME_NEW.pfnOnFreeEntPrivateData(e);
	eFreePriv(e);
	e->free = 1; e->v.classname = 0;
}
typedef void (*Factory)(entvars_t *);
static edict_t *eCreateNamed(int cls)
{
	edict_t *e = eCreateEntity();
	e->v.classname = cls;
	Factory f = (Factory)dlsym(g_game, Str(cls));
	if (!f) { printf("  no factory for %s\n", Str(cls)); eRemoveEntity(e); return NULL; }
	f(&e->v);
	return e;
}
static void *eAllocPriv(edict_t *e, int32 cb) { eFreePriv(e); e->pvPrivateData = calloc(1, cb); return e->pvPrivateData; }
static void *ePvPriv(edict_t *e) { return e ? e->pvPrivateData : NULL; }
static const char *eSzFromIndex(int i) { return Str(i); }
static int eAllocString(const char *s) { return MK(strdup(s)); }
static entvars_t *eGetVars(edict_t *e) { return &e->v; }
static edict_t *ePEntOfs(int ofs) { return (edict_t *)((char *)ED + ofs); }
static int eEntOfs(const edict_t *e) { return (int)((char *)e - (char *)ED); }
static int eIndexOf(const edict_t *e) { return IdxOf(e); }
static edict_t *ePEntIdx(int i) { return (i >= 0 && i < MAXE) ? &ED[i] : NULL; }
static edict_t *eFindByVars(entvars_t *v) { return v ? v->pContainingEntity : NULL; }
static int eDropToFloor(edict_t *) { return 1; }

// ---------------------------------------------------------------------------
// Traces: world = axis-aligned boxes; entities = their abs boxes.
static void HullSize(int hull, Vector &mn, Vector &mx)
{
	switch (hull)
	{
	case 1: mn = Vector(-16, -16, -36); mx = Vector(16, 16, 36); break;
	case 2: mn = Vector(-32, -32, -32); mx = Vector(32, 32, 32); break;
	case 3: mn = Vector(-16, -16, -18); mx = Vector(16, 16, 18); break;
	default: mn = mx = Vector(0, 0, 0);
	}
}
// Point segment vs box [bmn,bmx]; returns entry fraction (<0 = start inside), or 2 = miss.
static float SegBox(const Vector &a, const Vector &b, const Vector &bmn, const Vector &bmx, Vector &normal)
{
	bool inside = a.x > bmn.x && a.x < bmx.x && a.y > bmn.y && a.y < bmx.y && a.z > bmn.z && a.z < bmx.z;
	if (inside) { normal = Vector(0, 0, 0); return -1; }
	float t0 = 0, t1 = 1; int axis = -1; float sign = 0;
	for (int k = 0; k < 3; k++)
	{
		float s = a[k], d = b[k] - a[k], lo = bmn[k], hi = bmx[k];
		if (fabsf(d) < 1e-6f) { if (s <= lo || s >= hi) return 2; continue; }
		float ta = (lo - s) / d, tb = (hi - s) / d;
		float n = -1;
		if (ta > tb) { float t = ta; ta = tb; tb = t; n = 1; }
		if (ta > t0) { t0 = ta; axis = k; sign = (d > 0) ? -1 : 1; }
		if (tb < t1) t1 = tb;
		if (t0 > t1) return 2;
		(void)n;
	}
	if (axis < 0) return 2;
	normal = Vector(0, 0, 0); normal[axis] = sign;
	return t0;
}
static int ShouldCollide(edict_t *touch, edict_t *pass);
static void SweepTrace(const Vector &a, const Vector &b, const Vector &hmn, const Vector &hmx, int noMonsters, edict_t *skip, TraceResult *tr)
{
	// GoldSrc semantics: a trace that starts inside an ENTITY's box reports
	// startsolid with that entity as pHit, and carries on (fraction from what
	// else it hits, or 1). Starting inside the WORLD is allsolid, fraction 0.
	memset(tr, 0, sizeof(*tr));
	tr->flFraction = 1; tr->vecEndPos = b; tr->fInOpen = 1;
	float best = 1; Vector bestN(0, 0, 0); edict_t *hit = NULL, *startIn = NULL; bool worldStart = false;
	for (auto &w : g_world)
	{
		Vector n; float f = SegBox(a, b, w.mn - hmx, w.mx - hmn, n);
		if (f < 0) { worldStart = true; continue; }
		if (f <= 1 && f < best) { best = f; bestN = n; hit = &ED[0]; }
	}
	if (noMonsters != 1)
		for (int i = 1; i < MAXE; i++)
		{
			edict_t *e = &ED[i];
			if (e->free || e == skip || (e->v.solid != SOLID_BBOX && e->v.solid != SOLID_SLIDEBOX)) continue;
			if (skip && (e->v.owner == skip || skip->v.owner == e)) continue;
			if (skip && !ShouldCollide(e, skip)) continue;
			Vector n; float f = SegBox(a, b, e->v.absmin - hmx, e->v.absmax - hmn, n);
			if (f < 0) { if (!startIn) startIn = e; continue; }
			if (f <= 1 && f < best) { best = f; bestN = n; hit = e; }
		}
	if (worldStart)
	{
		tr->fStartSolid = tr->fAllSolid = 1; tr->flFraction = 0; tr->vecEndPos = a; tr->pHit = &ED[0];
		return;
	}
	tr->flFraction = best;
	tr->vecEndPos = a + (b - a) * best;
	tr->vecPlaneNormal = bestN;
	tr->pHit = hit;
	if (startIn) { tr->fStartSolid = 1; if (!hit) tr->pHit = startIn; }   // later, closer hits win (SV_ClipToLinks)
	if (getenv("FX_TRACEDBG")) printf("  trace (%.0f %.0f %.0f)->(%.0f %.0f %.0f) nm=%d skip=%d: frac %.2f start %d hit %d\n", a.x, a.y, a.z, b.x, b.y, b.z, noMonsters, IdxOf(skip), tr->flFraction, tr->fStartSolid, IdxOf(tr->pHit));
}
static void eTraceLine(const float *v1, const float *v2, int nm, edict_t *skip, TraceResult *tr) { SweepTrace(Vector(v1), Vector(v2), Vector(0, 0, 0), Vector(0, 0, 0), nm, skip, tr); }
static void eTraceHull(const float *v1, const float *v2, int nm, int hull, edict_t *skip, TraceResult *tr) { Vector mn, mx; HullSize(hull, mn, mx); SweepTrace(Vector(v1), Vector(v2), mn, mx, nm, skip, tr); }
static int eTraceMonsterHull(edict_t *e, const float *v1, const float *v2, int nm, edict_t *skip, TraceResult *tr) { SweepTrace(Vector(v1), Vector(v2), e->v.mins, e->v.maxs, nm, skip, tr); return tr->fStartSolid; }
static int ePointContents(const float *p)
{
	for (auto &w : g_world)
		if (p[0] > w.mn.x && p[0] < w.mx.x && p[1] > w.mn.y && p[1] < w.mx.y && p[2] > w.mn.z && p[2] < w.mx.z) return CONTENTS_SOLID;
	return CONTENTS_EMPTY;
}

// ---------------------------------------------------------------------------
// Messages
static std::map<std::string, int> s_msgIds;
static std::map<int, std::string> s_msgNames;
static int eRegUserMsg(const char *n, int) { int id = 64 + (int)s_msgIds.size(); s_msgIds[n] = id; s_msgNames[id] = n; return id; }
static int s_curMsg; static edict_t *s_curTo; static std::string s_curArgs;
static void eMessageBegin(int dest, int type, const float *, edict_t *ed) { s_curMsg = type; s_curTo = ed; s_curArgs.clear(); }
static void eMessageEnd(void)
{
	std::string n = s_msgNames.count(s_curMsg) ? s_msgNames[s_curMsg] : std::to_string(s_curMsg);
	g_msgLog.push_back(n + "->" + std::to_string(IdxOf(s_curTo)) + " " + s_curArgs);
}
static void eWriteByte(int v) { s_curArgs += "b" + std::to_string(v) + " "; }
static void eWriteChar(int v) { s_curArgs += "c" + std::to_string(v) + " "; }
static void eWriteShort(int v) { s_curArgs += "s" + std::to_string(v) + " "; }
static void eWriteLong(int v) { s_curArgs += "l" + std::to_string(v) + " "; }
static void eWriteAngle(float v) {}
static void eWriteCoord(float v) {}
static void eWriteString(const char *s) { s_curArgs += std::string("\"") + (s ? s : "") + "\" "; }
static void eWriteEntity(int v) {}

// Cvars
static std::map<std::string, cvar_t *> s_cvars;
static std::map<std::string, std::string> s_cvarDefaults = { {"sv_gravity", "800"}, {"mp_teamplay", "21"}, {"sv_maxspeed", "500"}, {"sys_ticrate", "100"} };
static void eCvarRegister(cvar_t *c) { s_cvars[c->name] = c; }
static float eCvarGetFloat(const char *n) { auto it = s_cvars.find(n); if (it != s_cvars.end()) return it->second->value; auto d = s_cvarDefaults.find(n); return d != s_cvarDefaults.end() ? atof(d->second.c_str()) : 0.0f; }
static const char *eCvarGetString(const char *n) { auto it = s_cvars.find(n); if (it != s_cvars.end()) return it->second->string; auto d = s_cvarDefaults.find(n); return d != s_cvarDefaults.end() ? d->second.c_str() : ""; }
static void eCvarSetFloat(const char *n, float v) { auto it = s_cvars.find(n); if (it != s_cvars.end()) { it->second->value = v; } }
static void eCvarSetString(const char *n, const char *v) { auto it = s_cvars.find(n); if (it != s_cvars.end()) { it->second->string = strdup(v); it->second->value = atof(v); } }
static cvar_t *eCvarGetPointer(const char *n) { auto it = s_cvars.find(n); return it != s_cvars.end() ? it->second : NULL; }

static void eAlert(ALERT_TYPE t, char *fmt, ...) { if (g_quiet) return; va_list a; va_start(a, fmt); printf("  ALERT: "); vprintf(fmt, a); va_end(a); }
static void eServerPrint(const char *s) { printf("  SERVER: %s", s); }
static void eClientPrintf(edict_t *e, PRINT_TYPE, const char *s) { g_printLog.push_back(s); }
static void eServerCommand(char *s) {}
static void eServerExecute(void) {}
static void eClientCommand(edict_t *e, char *fmt, ...) {}

static const char *s_argv[4]; static int s_argc;
static char s_args[256];
static const char *eArgv(int i) { return (i >= 0 && i < s_argc) ? s_argv[i] : ""; }
static int eArgc(void) { return s_argc; }
static const char *eArgs(void) { return s_args; }

static std::map<std::string, void (*)(void)> s_cmds;
static void eAddServerCommand(char *n, void (*f)(void)) { s_cmds[n] = f; }

static uint32 s_rand = 12345;
static int32 eRandomLong(int32 lo, int32 hi) { s_rand = s_rand * 1103515245 + 12345; if (hi <= lo) return lo; return lo + (int32)((s_rand >> 8) % (uint32)(hi - lo + 1)); }
static float eRandomFloat(float lo, float hi) { s_rand = s_rand * 1103515245 + 12345; return lo + (hi - lo) * ((s_rand >> 8) & 0xffff) / 65535.0f; }
static void eSetView(const edict_t *c, const edict_t *v) { int i = IdxOf(c); if (i >= 0 && i < 64) g_lastSetViewTo[i] = IdxOf(v); }
static float eTime(void) { return G.time; }
static void eGetGameDir(char *d) { strcpy(d, "."); }
static byte *eLoadFile(char *, int *len) { if (len) *len = 0; return NULL; }
static void eFreeFile(void *) {}
static char s_info[64] = "";
static char *eGetInfoKeyBuffer(edict_t *) { return s_info; }
static char *eInfoKeyValue(char *, char *) { return (char *)""; }
static void eSetClientKeyValue(int, char *, char *, char *) {}
static int eGetPlayerUserId(edict_t *e) { return 100 + IdxOf(e); }
static const char *eGetPlayerAuthId(edict_t *e) { return "STEAM_0:0:1"; }
static int eIsDedicated(void) { return 1; }
static unsigned short ePrecacheEvent(int, const char *) { static int n = 1; return n++; }
static void ePlaybackEvent(int, const edict_t *, unsigned short, float, float *, float *, float, float, int, int, int, int) {}
static int eNumberOfEntities(void) { int n = 0; for (int i = 0; i < MAXE; i++) if (!ED[i].free) n++; return n; }
static int eDecalIndex(const char *) { return 1; }
static void eEmitSound(edict_t *, int, const char *, float, float, int, int) {}
static void eEmitAmbient(edict_t *, float *, const char *, float, float, int, int) {}
static void *eGetModelPtr(edict_t *) { return NULL; }
static const char *eGetPhysicsKeyValue(const edict_t *, const char *) { return ""; }
static void eSetPhysicsKeyValue(const edict_t *, const char *, const char *) {}
static const char *eGetPhysicsInfoString(const edict_t *) { return ""; }
static int eEngCheckParm(const char *, char **) { return 0; }
static void eCrosshair(const edict_t *, float, float) {}
static void eSetClientMaxspeed(const edict_t *e, float s) { ((edict_t *)e)->v.maxspeed = s; }
static void eLightStyle(int, char *) {}
static void eGetSpawnParms(edict_t *) {}
static void eChangeYaw(edict_t *) {}
static int eWalkMove(edict_t *, float, float, int) { return 1; }
static void eTraceToss(edict_t *, edict_t *, TraceResult *tr) { memset(tr, 0, sizeof(*tr)); tr->flFraction = 1; }
static void eGetAimVector(edict_t *e, float, float *r) { AngleVectors(e->v.v_angle, r, NULL, NULL); }
static void eGetAttachment(const edict_t *e, int, float *o, float *a) { if (o) { o[0] = e->v.origin.x; o[1] = e->v.origin.y; o[2] = e->v.origin.z; } }
static void eGetBonePosition(const edict_t *e, int, float *o, float *a) { if (o) { o[0] = e->v.origin.x; o[1] = e->v.origin.y; o[2] = e->v.origin.z; } }
static const char *eTraceTexture(edict_t *, const float *, const float *) { return NULL; }
static int eGetEntityIllum(edict_t *) { return 128; }
static void eMakeStatic(edict_t *) {}
static int eIsMapValid(char *) { return 1; }
static void eParticle(const float *, const float *, float, float) {}
static void eStaticDecal(const float *, int, int, int) {}
static int ePrecacheGeneric(char *) { return 1; }
static int eCheckVisibility(const edict_t *, unsigned char *) { return 1; }
static unsigned char *eSetFatPVS(float *) { static unsigned char b[4096]; memset(b, 0xff, sizeof(b)); return b; }
static edict_t *eFindClientInPVS(edict_t *) { return &ED[0]; }
static edict_t *eEntitiesInPVS(edict_t *) { return &ED[0]; }
static void eFadeVolume(const edict_t *, int, int, int, int) {}
static void eCvarDirectSet(cvar_t *c, char *v) { c->string = strdup(v); c->value = atof(v); }

static void BuildEngine(void)
{
	void **slots = (void **)&ENG;
	int n = sizeof(ENG) / sizeof(void *);
	for (int i = 0; i < n && i < 300; i++) slots[i] = g_stubs[i];
	ENG.pfnPrecacheModel = ePrecache; ENG.pfnPrecacheSound = ePrecache; ENG.pfnSetModel = eSetModel;
	ENG.pfnModelIndex = eModelIndex; ENG.pfnModelFrames = eModelFrames; ENG.pfnSetSize = eSetSize;
	ENG.pfnVecToYaw = eVecToYaw; ENG.pfnVecToAngles = eVecToAngles; ENG.pfnFindEntityByString = eFindByString;
	ENG.pfnFindEntityInSphere = eFindInSphere; ENG.pfnMakeVectors = eMakeVectors; ENG.pfnAngleVectors = eAngleVectors;
	ENG.pfnCreateEntity = eCreateEntity; ENG.pfnRemoveEntity = eRemoveEntity; ENG.pfnCreateNamedEntity = eCreateNamed;
	ENG.pfnDropToFloor = eDropToFloor; ENG.pfnSetOrigin = eSetOrigin; ENG.pfnEmitSound = eEmitSound; ENG.pfnEmitAmbientSound = eEmitAmbient;
	ENG.pfnTraceLine = eTraceLine; ENG.pfnTraceHull = eTraceHull; ENG.pfnTraceMonsterHull = eTraceMonsterHull; ENG.pfnTraceToss = eTraceToss;
	ENG.pfnPointContents = ePointContents; ENG.pfnMessageBegin = eMessageBegin; ENG.pfnMessageEnd = eMessageEnd;
	ENG.pfnWriteByte = eWriteByte; ENG.pfnWriteChar = eWriteChar; ENG.pfnWriteShort = eWriteShort; ENG.pfnWriteLong = eWriteLong;
	ENG.pfnWriteAngle = eWriteAngle; ENG.pfnWriteCoord = eWriteCoord; ENG.pfnWriteString = eWriteString; ENG.pfnWriteEntity = eWriteEntity;
	ENG.pfnCVarRegister = eCvarRegister; ENG.pfnCVarGetFloat = eCvarGetFloat; ENG.pfnCVarGetString = eCvarGetString;
	ENG.pfnCVarSetFloat = eCvarSetFloat; ENG.pfnCVarSetString = eCvarSetString; ENG.pfnAlertMessage = eAlert;
	ENG.pfnPvAllocEntPrivateData = eAllocPriv; ENG.pfnPvEntPrivateData = ePvPriv; ENG.pfnFreeEntPrivateData = eFreePriv;
	ENG.pfnSzFromIndex = eSzFromIndex; ENG.pfnAllocString = eAllocString; ENG.pfnGetVarsOfEnt = eGetVars;
	ENG.pfnPEntityOfEntOffset = ePEntOfs; ENG.pfnEntOffsetOfPEntity = eEntOfs; ENG.pfnIndexOfEdict = eIndexOf;
	ENG.pfnPEntityOfEntIndex = ePEntIdx; ENG.pfnFindEntityByVars = eFindByVars; ENG.pfnGetModelPtr = eGetModelPtr;
	ENG.pfnRegUserMsg = eRegUserMsg; ENG.pfnClientPrintf = eClientPrintf; ENG.pfnServerPrint = eServerPrint;
	ENG.pfnCmd_Args = eArgs; ENG.pfnCmd_Argv = eArgv; ENG.pfnCmd_Argc = eArgc; ENG.pfnRandomLong = eRandomLong;
	ENG.pfnRandomFloat = eRandomFloat; ENG.pfnSetView = eSetView; ENG.pfnTime = eTime; ENG.pfnLoadFileForMe = eLoadFile;
	ENG.pfnFreeFile = eFreeFile; ENG.pfnGetGameDir = eGetGameDir; ENG.pfnCvar_RegisterVariable = eCvarRegister;
	ENG.pfnGetInfoKeyBuffer = eGetInfoKeyBuffer; ENG.pfnInfoKeyValue = eInfoKeyValue; ENG.pfnSetClientKeyValue = eSetClientKeyValue;
	ENG.pfnGetPlayerUserId = eGetPlayerUserId; ENG.pfnIsDedicatedServer = eIsDedicated; ENG.pfnCVarGetPointer = eCvarGetPointer;
	ENG.pfnPrecacheEvent = ePrecacheEvent; ENG.pfnPlaybackEvent = ePlaybackEvent; ENG.pfnNumberOfEntities = eNumberOfEntities;
	ENG.pfnGetPlayerAuthId = eGetPlayerAuthId; ENG.pfnAddServerCommand = eAddServerCommand; ENG.pfnServerCommand = eServerCommand;
	ENG.pfnServerExecute = eServerExecute; ENG.pfnClientCommand = eClientCommand; ENG.pfnDecalIndex = eDecalIndex;
	ENG.pfnGetPhysicsKeyValue = eGetPhysicsKeyValue; ENG.pfnSetPhysicsKeyValue = eSetPhysicsKeyValue; ENG.pfnGetPhysicsInfoString = eGetPhysicsInfoString; ENG.pfnEngCheckParm = eEngCheckParm;
	ENG.pfnCrosshairAngle = eCrosshair; ENG.pfnSetClientMaxspeed = eSetClientMaxspeed; ENG.pfnLightStyle = eLightStyle;
	ENG.pfnGetSpawnParms = eGetSpawnParms; ENG.pfnSaveSpawnParms = eGetSpawnParms; ENG.pfnChangeYaw = eChangeYaw; ENG.pfnChangePitch = eChangeYaw;
	ENG.pfnWalkMove = eWalkMove; ENG.pfnGetAimVector = eGetAimVector; ENG.pfnGetAttachment = eGetAttachment; ENG.pfnGetBonePosition = eGetBonePosition;
	ENG.pfnTraceTexture = eTraceTexture; ENG.pfnGetEntityIllum = eGetEntityIllum; ENG.pfnMakeStatic = eMakeStatic; ENG.pfnIsMapValid = eIsMapValid;
	ENG.pfnParticleEffect = eParticle; ENG.pfnStaticDecal = eStaticDecal; ENG.pfnPrecacheGeneric = ePrecacheGeneric;
	ENG.pfnCheckVisibility = eCheckVisibility; ENG.pfnSetFatPVS = eSetFatPVS; ENG.pfnSetFatPAS = eSetFatPVS;
	ENG.pfnFindClientInPVS = eFindClientInPVS; ENG.pfnEntitiesInPVS = eEntitiesInPVS; ENG.pfnFadeClientVolume = eFadeVolume;
	ENG.pfnCvar_DirectSet = eCvarDirectSet;
}

// ---------------------------------------------------------------------------
// Metamod: plugin hooks in front of / behind the engine and the game.
#define MM_PRE(tbl, fn, ...) do { if (g_pluginOn && tbl.fn) { MG.mres = MRES_IGNORED; tbl.fn(__VA_ARGS__); } } while (0)
static bool Superseded(void) { return g_pluginOn && MG.mres == MRES_SUPERCEDE; }

static void wSetOrigin(edict_t *e, const float *o) { MG.mres = MRES_IGNORED; MM_PRE(P_ENG, pfnSetOrigin, e, o); if (!Superseded()) ENG.pfnSetOrigin(e, o); MM_PRE(P_ENGP, pfnSetOrigin, e, o); }
static void wSetSize(edict_t *e, const float *a, const float *b) { MG.mres = MRES_IGNORED; MM_PRE(P_ENG, pfnSetSize, e, a, b); if (!Superseded()) ENG.pfnSetSize(e, a, b); MM_PRE(P_ENGP, pfnSetSize, e, a, b); }
static void wTraceLine(const float *a, const float *b, int nm, edict_t *s, TraceResult *tr) { MG.mres = MRES_IGNORED; MM_PRE(P_ENG, pfnTraceLine, a, b, nm, s, tr); if (!Superseded()) ENG.pfnTraceLine(a, b, nm, s, tr); MM_PRE(P_ENGP, pfnTraceLine, a, b, nm, s, tr); }
static void wTraceHull(const float *a, const float *b, int nm, int h, edict_t *s, TraceResult *tr) { MG.mres = MRES_IGNORED; MM_PRE(P_ENG, pfnTraceHull, a, b, nm, h, s, tr); if (!Superseded()) ENG.pfnTraceHull(a, b, nm, h, s, tr); MM_PRE(P_ENGP, pfnTraceHull, a, b, nm, h, s, tr); }
static void wMessageBegin(int d, int t, const float *o, edict_t *e) { MG.mres = MRES_IGNORED; MM_PRE(P_ENG, pfnMessageBegin, d, t, o, e); if (!Superseded()) ENG.pfnMessageBegin(d, t, o, e); MM_PRE(P_ENGP, pfnMessageBegin, d, t, o, e); }
static void wMessageEnd(void) { MG.mres = MRES_IGNORED; MM_PRE(P_ENG, pfnMessageEnd); if (!Superseded()) ENG.pfnMessageEnd(); MM_PRE(P_ENGP, pfnMessageEnd); }

static int ShouldCollide(edict_t *touch, edict_t *pass)
{
	if (g_pluginOn && P_NEW.pfnShouldCollide)
	{
		MG.mres = MRES_IGNORED;
		int r = P_NEW.pfnShouldCollide(touch, pass);
		if (MG.mres == MRES_SUPERCEDE) return r;
	}
	return GAME_NEW.pfnShouldCollide ? GAME_NEW.pfnShouldCollide(touch, pass) : 1;
}
int SimShouldCollide(edict_t *a, edict_t *b) { return ShouldCollide(a, b); }

#define DLL_CALL(fn, ...) do { bool sup = false; if (g_pluginOn && P_DLL.fn) { MG.mres = MRES_IGNORED; P_DLL.fn(__VA_ARGS__); sup = MG.mres == MRES_SUPERCEDE; } \
	if (!sup && GAME.fn) GAME.fn(__VA_ARGS__); if (g_pluginOn && P_DLLP.fn) { MG.mres = MRES_IGNORED; P_DLLP.fn(__VA_ARGS__); } } while (0)

// mutil
static void uLog(plid_t, const char *fmt, ...) { if (g_quiet > 1) return; va_list a; va_start(a, fmt); printf("  PLUGIN: "); vprintf(fmt, a); printf("\n"); va_end(a); }
static int uGetUserMsgID(plid_t, const char *n, int *size) { auto it = s_msgIds.find(n); return it != s_msgIds.end() ? it->second : 0; }
static const char *uGetUserMsgName(plid_t, int id, int *) { return s_msgNames.count(id) ? s_msgNames[id].c_str() : NULL; }

typedef void (*GiveFn)(enginefuncs_t *, globalvars_t *);
typedef int (*GetApi2)(DLL_FUNCTIONS *, int *);
typedef int (*GetNewApi)(NEW_DLL_FUNCTIONS *, int *);

void *SimSym(const char *name)
{
	void *p = dlsym(g_game, name);
	if (p) return p;
	// a local (non-exported) symbol: its offset from nm, plus the load address
	static long giveOfs = -1;
	char cmd[300], line[300];
	if (giveOfs < 0)
	{
		snprintf(cmd, sizeof(cmd), "nm '%s' | grep ' GiveFnptrsToDll$'", GamePath());
		FILE *f = popen(cmd, "r");
		if (f && fgets(line, sizeof(line), f)) giveOfs = strtol(line, NULL, 16);
		if (f) pclose(f);
	}
	snprintf(cmd, sizeof(cmd), "nm '%s' | grep ' %s$'", GamePath(), name);
	FILE *f = popen(cmd, "r");
	long ofs = -1;
	if (f && fgets(line, sizeof(line), f)) ofs = strtol(line, NULL, 16);
	if (f) pclose(f);
	if (ofs < 0 || giveOfs < 0) return NULL;
	return (char *)dlsym(g_game, "GiveFnptrsToDll") - giveOfs + ofs;
}

void SimInit(bool withPlugin)
{
	g_pluginOn = withPlugin;
	memset(ED, 0, sizeof(ED));
	for (int i = 0; i < MAXE; i++) { ED[i].free = 1; ED[i].v.pContainingEntity = &ED[i]; }
	memset(&G, 0, sizeof(G));
	G.maxClients = 8; G.maxEntities = MAXE; G.time = 1.0f; G.frametime = 0.01f;
	G.pStringBase = s_strbase;
	G.mapname = MK("fx_test"); G.deathmatch = 1; G.teamplay = 21;
	BuildEngine();
	GAME_ENG = ENG;
	GAME_ENG.pfnSetOrigin = wSetOrigin; GAME_ENG.pfnSetSize = wSetSize; GAME_ENG.pfnTraceLine = wTraceLine;
	GAME_ENG.pfnTraceHull = wTraceHull; GAME_ENG.pfnMessageBegin = wMessageBegin; GAME_ENG.pfnMessageEnd = wMessageEnd;

	g_game = dlopen(GamePath(), RTLD_NOW | RTLD_LOCAL);
	if (!g_game) { printf("dlopen tfc.so: %s\n", dlerror()); exit(1); }
	((GiveFn)dlsym(g_game, "GiveFnptrsToDll"))(&GAME_ENG, &G);
	int v = INTERFACE_VERSION; ((GetApi2)dlsym(g_game, "GetEntityAPI2"))(&GAME, &v);
	v = NEW_DLL_FUNCTIONS_VERSION; GetNewApi gn = (GetNewApi)dlsym(g_game, "GetNewDLLFunctions"); if (gn) gn(&GAME_NEW, &v);

	void *pl = dlopen(PluginPath(), RTLD_NOW | RTLD_LOCAL);
	if (!pl) { printf("dlopen plugin: %s\n", dlerror()); exit(1); }
	((GiveFn)dlsym(pl, "GiveFnptrsToDll"))(&ENG, &G);
	MU.pfnLogConsole = uLog; MU.pfnLogMessage = uLog; MU.pfnLogError = uLog; MU.pfnLogDeveloper = uLog;
	MU.pfnGetUserMsgID = uGetUserMsgID; MU.pfnGetUserMsgName = uGetUserMsgName;
	((int (*)(char *, plugin_info_t **, mutil_funcs_t *))dlsym(pl, "Meta_Query"))((char *)META_INTERFACE_VERSION, &PINFO, &MU);
	GF.dllapi_table = &GAME; GF.newapi_table = &GAME_NEW;
	((int (*)(PLUG_LOADTIME, META_FUNCTIONS *, meta_globals_t *, gamedll_funcs_t *))dlsym(pl, "Meta_Attach"))(PT_ANYTIME, &MF, &MG, &GF);
	v = INTERFACE_VERSION; MF.pfnGetEntityAPI2(&P_DLL, &v);
	v = INTERFACE_VERSION; MF.pfnGetEntityAPI2_Post(&P_DLLP, &v);
	v = NEW_DLL_FUNCTIONS_VERSION; MF.pfnGetNewDLLFunctions(&P_NEW, &v);
	v = ENGINE_INTERFACE_VERSION; MF.pfnGetEngineFunctions(&P_ENG, &v);
	v = ENGINE_INTERFACE_VERSION; MF.pfnGetEngineFunctions_Post(&P_ENGP, &v);

	GAME.pfnGameInit();
	eCvarSetString("mp_teamplay", "21");
	eCvarSetString("teamplay", "21");
	float *nt = (float *)SimSym("number_of_teams");
	if (nt) *nt = 2;
	// the world
	ED[0].free = 0; ED[0].v.classname = MK("worldspawn"); ED[0].v.solid = SOLID_BSP;
	((Factory)dlsym(g_game, "worldspawn"))(&ED[0].v);
	GAME.pfnSpawn(&ED[0]);
	// two spawn points per team
	for (int t = 1; t <= 2; t++)
		for (int k = 0; k < 2; k++)
		{
			edict_t *s = SimCreateNamed("info_player_teamspawn", Vector(t * 1000.0f, k * 200.0f, 36));
			char b[8]; sprintf(b, "%d", t);
			SimKeyValue(s, "team_no", b);
			GAME.pfnSpawn(s);
		}
	DLL_CALL(pfnServerActivate, ED, MAXE, G.maxClients);
	if (nt) *nt = 2;
	int *tmp = (int *)SimSym("teammaxplayers");
	if (tmp) for (int i = 0; i < 5; i++) tmp[i] = 32;
}

edict_t *SimCreateNamed(const char *cls, const Vector &org)
{
	edict_t *e = eCreateNamed(MK(strdup(cls)));
	if (e) { e->v.origin = org; Relink(e); }
	return e;
}

void SimKeyValue(edict_t *e, const char *k, const char *v)
{
	KeyValueData kvd; kvd.szClassName = (char *)Str(e->v.classname); kvd.szKeyName = (char *)k; kvd.szValue = (char *)v; kvd.fHandled = 0;
	GAME.pfnKeyValue(e, &kvd);
}

edict_t *SimConnect(int idx, const char *name)
{
	edict_t *e = &ED[idx];
	int ser = e->serialnumber;
	memset(e, 0, sizeof(*e));
	e->serialnumber = ser + 1; e->v.pContainingEntity = e;
	e->v.netname = MK(strdup(name)); e->v.flags = FL_CLIENT;
	char reject[128];
	GAME.pfnClientConnect(e, name, "127.0.0.1", reject);
	DLL_CALL(pfnClientPutInServer, e);
	return e;
}

void SimClientCommand(edict_t *e, const char *a0, const char *a1, const char *a2)
{
	s_argv[0] = a0; s_argv[1] = a1; s_argv[2] = a2; s_argc = a2 ? 3 : a1 ? 2 : 1;
	snprintf(s_args, sizeof(s_args), "%s %s", a1 ? a1 : "", a2 ? a2 : "");
	DLL_CALL(pfnClientCommand, e);
	s_argc = 0;
}

void SimServerCommand(const char *a0, const char *a1, const char *a2)
{
	s_argv[0] = a0; s_argv[1] = a1; s_argv[2] = a2; s_argc = a2 ? 3 : a1 ? 2 : 1;
	auto it = s_cmds.find(a0); if (it != s_cmds.end()) it->second();
	s_argc = 0;
}

void SimDisconnect(edict_t *e)
{
	DLL_CALL(pfnClientDisconnect, e);
	e->v.flags &= ~FL_CLIENT;
	e->v.netname = 0;
}

void SimThink(edict_t *e)
{
	DLL_CALL(pfnThink, e);
}

void SimTouch(edict_t *a, edict_t *b)
{
	DLL_CALL(pfnTouch, a, b);
}

void SimUpdateClientData(edict_t *e, clientdata_t *cd)
{
	DLL_CALL(pfnUpdateClientData, e, 0, cd);
}

void SimFrame(float dt)
{
	G.frametime = dt;
	DLL_CALL(pfnStartFrame);
	for (int i = 1; i <= G.maxClients; i++)
		if (!ED[i].free && (ED[i].v.flags & FL_CLIENT) && ED[i].pvPrivateData)
		{
			DLL_CALL(pfnPlayerPreThink, &ED[i]);
			DLL_CALL(pfnPlayerPostThink, &ED[i]);
		}
	for (int i = G.maxClients + 1; i < MAXE; i++)
	{
		edict_t *e = &ED[i];
		if (e->free || !e->pvPrivateData) continue;
		if (e->v.flags & FL_KILLME) { eRemoveEntity(e); continue; }
		float nt = e->v.nextthink;
		if (nt <= 0 || nt > G.time + dt) continue;
		e->v.nextthink = 0;
		float saved = G.time;
		G.time = nt > G.time ? nt : G.time;
		SimThink(e);
		G.time = saved;
	}
	G.time += dt;
}

void SimSendMotd(edict_t *e)
{
	int id = s_msgIds.count("MOTD") ? s_msgIds["MOTD"] : 0;
	if (!id) { printf("  (game registered no MOTD message)\n"); return; }
	GAME_ENG.pfnMessageBegin(MSG_ONE, id, NULL, e);
	GAME_ENG.pfnWriteByte(1);
	GAME_ENG.pfnWriteString("welcome");
	GAME_ENG.pfnMessageEnd();
}
