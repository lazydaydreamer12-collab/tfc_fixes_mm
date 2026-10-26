// fxsim.h - a fake GoldSrc engine + a fake metamod, running the REAL tfc.so
// with the REAL tfc_fixes plugin in between, the way metamod does on a server.
#pragma once
#include <extdll.h>
#include <meta_api.h>
#include <entity_state.h>
#include <string>
#include <vector>

#define MAXE 900

extern globalvars_t G;
extern edict_t ED[MAXE];
extern DLL_FUNCTIONS GAME;          // tfc.so's
extern NEW_DLL_FUNCTIONS GAME_NEW;
extern void *g_game;                // dlopen handle of tfc.so
extern bool g_pluginOn;             // route through the plugin?

struct WorldBox { Vector mn, mx; };
extern std::vector<WorldBox> g_world;

void SimInit(bool withPlugin);
edict_t *SimCreateNamed(const char *cls, const Vector &org);
void SimKeyValue(edict_t *e, const char *k, const char *v);
edict_t *SimConnect(int idx, const char *name);
void SimClientCommand(edict_t *e, const char *a0, const char *a1 = NULL, const char *a2 = NULL);
void SimFrame(float dt = 0.01f);      // StartFrame, player think, entity thinks
void SimThink(edict_t *e);            // one entity's think, through metamod
void SimTouch(edict_t *a, edict_t *b);
void SimDisconnect(edict_t *e);
void SimServerCommand(const char *a0, const char *a1 = NULL, const char *a2 = NULL);
void *SimSym(const char *name);       // dlsym in tfc.so
int SimShouldCollide(edict_t *a, edict_t *b);
void SimUpdateClientData(edict_t *e, clientdata_t *cd);

// What the engine saw
extern int g_lastSetViewTo[64];
extern std::vector<std::string> g_msgLog;   // "ShowMenu->2", ...
extern std::vector<std::string> g_printLog; // centre prints / client prints
extern int g_quiet;

// Field access in the C++ objects (Linux layout)
template <typename T> T &F(edict_t *e, int ofs) { return *(T *)((char *)e->pvPrivateData + ofs); }

int Check(bool ok, const char *what);
extern int g_fail, g_pass;
