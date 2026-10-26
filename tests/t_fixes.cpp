// t_fixes.cpp - each fix against the REAL tfc.so game code, plugin on and off.
//   ./t_fixes                  list the scenarios
//   ./t_fixes <scenario> on    with the plugin: the fix must work
//   ./t_fixes <scenario> off   without it: the bug must show (the control)
// run.sh runs them all both ways.
#include "fxsim.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

// Linux offsets (gdb ptype /o on tfc.so)
#define L_TFSTATE 156
#define L_REMOVEGREN 132
#define L_GREN1 72
#define L_GREN2 76
#define L_LEG 448
#define L_THINK 16
#define L_TELE_PLAYER 1724
#define L_TELE_TYPE 1732
#define L_TELE_STATE 1736

typedef int (*TakeDamageFn)(void *, entvars_t *, entvars_t *, float, int);
typedef int (*TakeHealthFn)(void *, float, int);
static int TakeDamage(edict_t *v, edict_t *att, float dmg, int bits)
{
	void **vt = *(void ***)v->pvPrivateData;
	return ((TakeDamageFn)vt[13])(v->pvPrivateData, &att->v, &att->v, dmg, bits);
}
static int TakeHealth(edict_t *v, float amt, int bits)
{
	void **vt = *(void ***)v->pvPrivateData;
	return ((TakeHealthFn)vt[14])(v->pvPrivateData, amt, bits);
}
static void Kill(edict_t *v) { TakeDamage(v, &ED[0], 1000, 0); }
static void Respawn(edict_t *v)
{
	typedef void (*RespawnFn)(entvars_t *, int);
	RespawnFn r = (RespawnFn)SimSym("_Z7respawnP9entvars_si");
	r(&v->v, 0);
}
static void Frames(float secs) { int n = (int)(secs / 0.01f + 0.5f); for (int i = 0; i < n; i++) SimFrame(0.01f); }
static void *ThinkFn(edict_t *e) { return *(void **)((char *)e->pvPrivateData + L_THINK); }
static edict_t *FindPrimed(edict_t *owner)
{
	void *gp = SimSym("_ZN15CTFPrimeGrenade13GrenadePrimedEv");
	for (int i = G.maxClients + 1; i < MAXE; i++)
		if (!ED[i].free && ED[i].pvPrivateData && ED[i].v.owner == owner && ThinkFn(&ED[i]) == gp)
			return &ED[i];
	return NULL;
}
static edict_t *Player(int idx, const char *name, const char *team, const char *cls)
{
	edict_t *e = SimConnect(idx, name);
	Frames(0.1f);
	SimClientCommand(e, "jointeam", team);
	Frames(0.1f);
	SimClientCommand(e, cls);
	Frames(0.5f);
	return e;
}
static const char *Cls(edict_t *e) { return G.pStringBase + e->v.classname; }

// ---------------------------------------------------------------------------
static edict_t *g_ent, *g_exit;
static bool BuildTeleporters(edict_t *eng, const Vector &a, const Vector &b)
{
	typedef void *(*CreateTele)(Vector *, Vector *, void *, int);
	CreateTele ct = (CreateTele)SimSym("_ZN13CTFTeleporter16CreateTeleporterE6VectorS0_P11CBaseEntityi");
	Vector ang(0, 0, 0), pa = a, pb = b;
	void *t1 = ct(&pa, &ang, eng->pvPrivateData, 4);
	void *t2 = ct(&pb, &ang, eng->pvPrivateData, 5);
	if (!t1 || !t2) return false;
	g_ent = (*(entvars_t **)((char *)t1 + 4))->pContainingEntity;
	g_exit = (*(entvars_t **)((char *)t2 + 4))->pContainingEntity;
	typedef void (*Fin)(void *);
	Fin fin = (Fin)SimSym("_ZN13CTFTeleporter8FinishedEv");
	fin(t1); fin(t2);
	Frames(3.0f);
	printf("  teleporters: entrance state %d, exit state %d\n", F<int>(g_ent, L_TELE_STATE), F<int>(g_exit, L_TELE_STATE));
	return true;
}
static void StepOn(edict_t *p, edict_t *pad)
{
	p->v.origin = pad->v.origin + Vector(0, 0, 40);
	p->v.velocity = Vector(0, 0, 0);
	SimTouch(pad, p);
}

static void S_TeleDeathEntrance(bool on)
{
	edict_t *eng = Player(2, "Eng", "2", "engineer");
	edict_t *dave = Player(3, "Dave", "2", "soldier");
	BuildTeleporters(eng, Vector(2000, 0, 0), Vector(3000, 0, 0));
	StepOn(dave, g_ent);
	printf("  after touch: entrance state %d player=%p\n", F<int>(g_ent, L_TELE_STATE), F<void *>(g_ent, L_TELE_PLAYER));
	Kill(dave);
	Vector corpse = dave->v.origin;
	Frames(3.0f);
	printf("  Dave dead=%d at %.0f %.0f %.0f (died at %.0f %.0f %.0f); entrance state %d exit state %d\n", dave->v.deadflag,
		dave->v.origin.x, dave->v.origin.y, dave->v.origin.z, corpse.x, corpse.y, corpse.z, F<int>(g_ent, L_TELE_STATE), F<int>(g_exit, L_TELE_STATE));
	bool moved = (dave->v.origin - corpse).Length() > 100;
	if (on) Check(!moved, "#8 entrance: the dead player is not carried to the exit");
	else Check(moved, "(control) without the plugin the dead player IS carried to the exit");
}

static void S_TeleDeathExit(bool on)
{
	// Dying AFTER the entrance has sent you: the current TFC already handles
	// this itself (Killed -> PlayerStoppedTeleporting). Checked both ways.
	edict_t *eng = Player(2, "Eng", "2", "engineer");
	edict_t *dave = Player(3, "Dave", "2", "soldier");
	edict_t *vic = Player(4, "Vic", "2", "soldier");
	BuildTeleporters(eng, Vector(2000, 0, 0), Vector(3000, 0, 0));
	vic->v.origin = g_exit->v.origin + Vector(0, 0, 40);
	StepOn(dave, g_ent);
	for (int i = 0; i < 300 && F<int>(g_exit, L_TELE_STATE) != 4; i++) SimFrame();
	Kill(dave);
	Vector corpse = dave->v.origin;
	Frames(0.3f);
	Respawn(dave);
	Vector spawn = dave->v.origin;
	Frames(3.0f);
	bool moved = (dave->v.origin - spawn).Length() > 100;
	printf("  Dave at %.0f %.0f (respawned at %.0f %.0f), Vic health %.0f, pads %d/%d\n", dave->v.origin.x, dave->v.origin.y,
		spawn.x, spawn.y, vic->v.health, F<int>(g_ent, L_TELE_STATE), F<int>(g_exit, L_TELE_STATE));
	Check(!moved && vic->v.health > 0, "#8 exit side: a player killed after being sent is not delivered (TFC's own fix, still intact)");
}

static void S_TeleFlag(bool on)
{
	edict_t *eng = Player(2, "Eng", "2", "engineer");
	edict_t *dave = Player(3, "Dave", "2", "soldier");
	BuildTeleporters(eng, Vector(2000, 0, 0), Vector(3000, 0, 0));
	StepOn(dave, g_ent);
	for (int i = 0; i < 300 && F<int>(g_exit, L_TELE_STATE) != 4; i++) SimFrame();
	Vector before = dave->v.origin;
	F<int>(dave, 120) = 1;   // is_unableto_spy_or_teleport: just picked up a flag
	Frames(2.0f);
	bool moved = (dave->v.origin - before).Length() > 100;
	printf("  Dave at %.0f %.0f, tfstate 0x%x, maxspeed %.0f\n", dave->v.origin.x, dave->v.origin.y, F<int>(dave, L_TFSTATE), dave->v.maxspeed);
	if (on)
	{
		Check(!moved, "#9: a player who picked up a flag mid-teleport stays at the entrance");
		Check(!(F<int>(dave, L_TFSTATE) & 0x10000) && dave->v.maxspeed > 1, "#9: and is unfrozen");
		Check(dave->v.renderfx == 0, "#9: and gets no 'arrived' glow");
	}
	else Check(moved, "(control) without the plugin the flag goes through");
}

static void S_TeleLeaver(bool on)
{
	edict_t *eng = Player(2, "Eng", "2", "engineer");
	edict_t *dave = Player(3, "Dave", "2", "soldier");
	edict_t *vic = Player(4, "Vic", "2", "soldier");
	BuildTeleporters(eng, Vector(2000, 0, 0), Vector(3000, 0, 0));
	vic->v.origin = g_exit->v.origin + Vector(0, 0, 40);   // standing on the exit
	StepOn(dave, g_ent);
	for (int i = 0; i < 300 && F<int>(g_exit, L_TELE_STATE) != 4; i++) SimFrame();
	void *obj = dave->pvPrivateData;
	SimDisconnect(dave);
	printf("  after leave: pads hold %p / %p (Dave was %p), states %d/%d\n", F<void *>(g_ent, L_TELE_PLAYER), F<void *>(g_exit, L_TELE_PLAYER), obj,
		F<int>(g_ent, L_TELE_STATE), F<int>(g_exit, L_TELE_STATE));
	if (on) Check(F<void *>(g_ent, L_TELE_PLAYER) == NULL && F<void *>(g_exit, L_TELE_PLAYER) == NULL, "leaver: both pads forget the player who left");
	else Check(F<void *>(g_exit, L_TELE_PLAYER) == obj, "(control) without the plugin the pads keep pointing at the player who left");
	Frames(12.0f);
	bool delivered = (dave->v.origin - g_exit->v.origin).Length() < 80;
	printf("  departed Dave's body at %.0f %.0f %.0f (exit at %.0f %.0f)\n", dave->v.origin.x, dave->v.origin.y, dave->v.origin.z, g_exit->v.origin.x, g_exit->v.origin.y);
	if (on) Check(!delivered, "leaver: the exit does not 'receive' (and telefrag for) a player who has left");
	else Check(delivered, "(control) without the plugin the exit still receives the player who left - with a telefrag on the pad");
	Check(F<int>(g_ent, L_TELE_STATE) == 2 && F<int>(g_exit, L_TELE_STATE) == 2, "leaver: both pads are ready again after the recharge");
}

// ---------------------------------------------------------------------------
static void S_GrenadeSpawn(bool on)
{
	edict_t *al = Player(1, "Alice", "1", "soldier");
	F<int>(al, L_GREN1) = 4;
	SimClientCommand(al, "+gren1");
	Frames(0.2f);
	edict_t *g = FindPrimed(al);
	if (!g) { Check(false, "setup: grenade primed"); return; }
	printf("  primed %s, think=%p (GrenadePrimed=%p)\n", g ? Cls(g) : "nothing", g ? ThinkFn(g) : NULL, SimSym("_ZN15CTFPrimeGrenade13GrenadePrimedEv"));
	Vector died = al->v.origin;
	Kill(al);
	Frames(0.1f);
	Respawn(al);
	Frames(0.05f);
	printf("  Alice respawned at %.0f %.0f %.0f (died at %.0f %.0f %.0f)\n", al->v.origin.x, al->v.origin.y, al->v.origin.z, died.x, died.y, died.z);
	bool held = g && ThinkFn(g) == SimSym("_ZN15CTFPrimeGrenade13GrenadePrimedEv");
	Frames(4.0f);
	printf("  grenade: still 'in hand' after respawn = %d; grenade at %.0f %.0f %.0f\n", held, g->v.origin.x, g->v.origin.y, g->v.origin.z);
	bool atSpawn = (g->v.origin - al->v.origin).Length() < 50 && (al->v.origin - died).Length() > 100;
	if (on) { Check(!held, "#14: the grenade left her hand when she died"); Check(!atSpawn, "#14: it did not go off at her new spawn"); }
	else Check(held || atSpawn, "(control) without the plugin the grenade follows her to spawn");
}

static void S_GrenadeJump(bool on)
{
	edict_t *al = Player(1, "Alice", "1", "soldier");
	F<int>(al, L_GREN1) = 4;
	SimClientCommand(al, "+gren1");
	// Hold it, "running" at 300 u/s along +x, with no movement between frames
	// (a 1000 fps server, where most frames carry no movement command).
	Vector before;
	for (int i = 0; i < 600; i++)
	{
		al->v.velocity = Vector(300, 0, 0);
		before = al->v.velocity;
		SimFrame(0.01f);
		Vector dv = al->v.velocity - before;
		if (dv.Length() > 50)
		{
			printf("  explosion push: %.0f %.0f %.0f (health %.0f)\n", dv.x, dv.y, dv.z, al->v.health);
			if (on) Check(dv.x > 50 && dv.x > fabsf(dv.y), "#17: the in-hand blast pushes along the way she was running");
			else Check(fabsf(dv.x) < 1, "(control) without the plugin the push has no forward part (straight up)");
			return;
		}
	}
	Check(false, "the grenade never went off in hand");
}

static void S_DoorCaltrops(bool on)
{
	edict_t *al = Player(1, "Alice", "1", "soldier");
	F<float>(al, L_LEG) = 4.0f;
	al->v.health = al->v.max_health;
	TakeHealth(al, 0.0f, 0);   // what every door does when it opens
	printf("  leg damage after a door: %.1f\n", F<float>(al, L_LEG));
	if (on) Check(F<float>(al, L_LEG) == 4.0f, "#7: a door does not heal caltrop leg damage");
	else Check(F<float>(al, L_LEG) != 4.0f, "(control) without the plugin the door heals it");
	al->v.health = 50;
	TakeHealth(al, 25.0f, 0);
	Check(al->v.health == 75, "#7: real heals still heal");
}

static void S_ZeroHealth(bool on)
{
	edict_t *al = Player(1, "Alice", "1", "soldier");
	al->v.health = 0.5f;
	clientdata_t cd; memset(&cd, 0, sizeof(cd));
	SimUpdateClientData(al, &cd);
	printf("  real health %.2f, sent to client %.2f\n", al->v.health, cd.health);
	if (on) { Check(cd.health == 1.0f, "#16: client is told 1, not 0"); Check(al->v.health == 0.5f, "#16: real health untouched"); }
	else Check(cd.health < 1.0f, "(control) without the plugin the client gets <1 (rounds to 0 = dead view)");
}

static void S_Nails(bool on)
{
	edict_t *al = Player(1, "Alice", "1", "soldier");
	edict_t *n1 = SimCreateNamed("tf_nailgun_nail", Vector(0, 0, 100));
	edict_t *n2 = SimCreateNamed("tf_nailgun_nail", Vector(0, 0, 100));
	// Spawn them through the game (model, movetype, size)
	typedef void (*SpawnFn)(void *);
	SpawnFn sp = (SpawnFn)(*(void ***)n1->pvPrivateData)[2];
	sp(n1->pvPrivateData); sp(n2->pvPrivateData);
	n1->v.owner = n2->v.owner = al;
	int c = SimShouldCollide(n1, n2);
	printf("  nail movetype %d model %d/%d, ShouldCollide=%d\n", n1->v.movetype, n1->v.modelindex, n2->v.modelindex, c);
	SimTouch(n1, n2);
	if (on) { Check(c == 0, "#1: nails do not collide with each other"); Check((n1->v.flags & FL_KILLME) != 0, "#1: a nail that touched one anyway is removed"); }
	else { Check(c == 1, "(control) nails collide"); Check(!(n1->v.flags & FL_KILLME) && !n1->free, "(control) and the touched nail stays (ghost)"); }
	edict_t *r = SimCreateNamed("tf_rpg_rocket", Vector(0, 0, 100));
	if (r) { r->v.movetype = MOVETYPE_FLY; Check(SimShouldCollide(n1, r) == 1, "#1: nails still hit other things"); }
}

static void DetonateNow(edict_t *g)
{
	typedef void (*Fn)(void *);
	((Fn)SimSym("_ZN8CGrenade8DetonateEv"))(g->pvPrivateData);
}

static void S_NailGrenCeiling(bool on)
{
	edict_t *al = Player(1, "Alice", "1", "soldier");
	g_world.push_back({ Vector(-500, -500, 100), Vector(500, 500, 200) });   // a ceiling at z=100
	edict_t *g = SimCreateNamed("tf_weapon_nailgrenade", Vector(0, 0, 80));
	typedef void (*SpawnFn)(void *);
	((SpawnFn)(*(void ***)g->pvPrivateData)[2])(g->pvPrivateData);
	g->v.owner = al; F<int>(g, 312) = 1;   // team_no
	g->v.origin = Vector(0, 0, 80);
	DetonateNow(g);
	printf("  nail grenade rose to z=%.1f (ceiling at 100)\n", g->v.origin.z);
	if (on) Check(g->v.origin.z <= 92.01f && g->v.origin.z >= 80, "#12: nail grenade stops 8 units short of the ceiling");
	else Check(g->v.origin.z >= 99.9f, "(control) without the plugin it rises into the ceiling");
}

static void S_Mirv(bool on)
{
	edict_t *al = Player(1, "Alice", "1", "demoman");
	g_world.push_back({ Vector(-500, -500, 90), Vector(500, 500, 100) });   // a thin floor above the MIRV
	edict_t *m = SimCreateNamed("tf_weapon_mirvgrenade", Vector(0, 0, 80));
	typedef void (*SpawnFn)(void *);
	((SpawnFn)(*(void ***)m->pvPrivateData)[2])(m->pvPrivateData);
	m->v.owner = al; F<int>(m, 312) = 1;
	m->v.origin = Vector(0, 0, 80);
	DetonateNow(m);
	int above = 0, n = 0;
	for (int i = G.maxClients + 1; i < MAXE; i++)
		if (!ED[i].free && ED[i].v.classname && !strcmp(G.pStringBase + ED[i].v.classname, "tf_weapon_mirvbomblet"))
		{
			n++;
			if (ED[i].v.origin.z > 90) above++;
			printf("  bomblet at z=%.1f\n", ED[i].v.origin.z);
		}
	if (on) Check(n == 4 && above == 0, "#13: all 4 bomblets stay below the floor above the MIRV");
	else Check(above > 0, "(control) without the plugin bomblets appear on the other side");
}

static void S_GrenadeInEntity(bool on)
{
	edict_t *al = Player(1, "Alice", "1", "soldier");
	edict_t *bob = Player(2, "Bob", "2", "soldier");
	edict_t *dave = Player(3, "Dave", "2", "soldier");
	bob->v.origin = Vector(0, 0, 36); bob->v.absmin = bob->v.origin + bob->v.mins; bob->v.absmax = bob->v.origin + bob->v.maxs;
	dave->v.origin = Vector(150, 0, 36); dave->v.absmin = dave->v.origin + dave->v.mins; dave->v.absmax = dave->v.origin + dave->v.maxs;
	dave->v.armorvalue = 0; bob->v.armorvalue = 0;
	dave->v.health = dave->v.max_health = 500;   // enough to survive either way, so the damage can be read
	bob->v.health = bob->v.max_health = 500;     // Bob must stay solid for the whole explosion
	edict_t *g = SimCreateNamed("tf_weapon_normalgrenade", Vector(0, 0, 36));
	typedef void (*SpawnFn)(void *);
	((SpawnFn)(*(void ***)g->pvPrivateData)[2])(g->pvPrivateData);
	g->v.owner = al; F<int>(g, 312) = 1;
	g->v.origin = Vector(0, 0, 36);   // inside Bob
	g->v.dmg = 180;
	float hd = dave->v.health;
	DetonateNow(g);
	float lost = hd - dave->v.health;
	printf("  Dave, 150 units from a grenade stuck inside Bob, lost %.0f health; Bob lost %.0f\n", lost, 500 - bob->v.health);
	float bobLost = 500 - bob->v.health;
	if (on) Check(lost > 0 && lost < bobLost - 30, "#11: Dave, 150 units away, takes the normal distance-reduced damage");
	else Check(lost >= bobLost, "(control) without the plugin Dave takes as much as Bob, who has it inside him");
}

static void S_Camera(bool on)
{
	// AMX plugin's #5 - now done by TFC itself (CBasePlayer::Spawn: SET_VIEW(self)).
	edict_t *al = Player(1, "Alice", "1", "soldier");
	g_lastSetViewTo[1] = 777;   // looking through a camera that is gone
	Kill(al); Frames(0.2f); Respawn(al); Frames(0.1f);
	printf("  view after respawn: %d\n", g_lastSetViewTo[1]);
	Check(g_lastSetViewTo[1] == 1, "#5 (TFC's own fix): view handed back to the player on respawn");
}

static void S_ConcRespawn(bool on)
{
	// AMX plugin's #6 - now done by TFC itself (respawn clears the concussion
	// timer, and the next update sends 0).
	edict_t *al = Player(1, "Alice", "1", "soldier");
	typedef void (*ConcFn)(void *, entvars_t *);
	((ConcFn)(*(void ***)al->pvPrivateData)[64])(al->pvPrivateData, &ED[0].v);
	Frames(0.5f);
	Kill(al); Frames(0.3f); Respawn(al);
	g_msgLog.clear();
	Frames(1.0f);
	std::string last;
	for (auto &m : g_msgLog) if (m.find("Concuss->1") == 0) last = m;
	printf("  last Concuss message after respawn: %s\n", last.c_str());
	Check(last.find(" b0 ") != std::string::npos, "#6 (TFC's own fix): concussion ends at respawn");
}

static void S_Menus(bool on)
{
	edict_t *al = Player(1, "Alice", "1", "soldier");
	g_msgLog.clear();
	// The game sends the message of the day (TFC reads motd.txt; the harness has none)
	extern void SimSendMotd(edict_t *);
	SimSendMotd(al);
	bool sent = false;
	for (auto &m : g_msgLog) if (m.find("ShowMenu->1") == 0) { sent = true; printf("  %s\n", m.c_str()); }
	if (on) Check(sent, "#19: a blank menu follows the MOTD");
	else Check(!sent, "(control) no menu without the plugin");
	g_msgLog.clear(); SimSendMotd(al);
	bool again = false; for (auto &m : g_msgLog) if (m.find("ShowMenu") == 0) again = true;
	if (on) Check(!again, "#19: only once per connection");
}

// ---------------------------------------------------------------------------
// Flags: a real item_tfgoal the player can carry and drop.
static edict_t *MakeFlag(const Vector &at)
{
	edict_t *f = SimCreateNamed("item_tfgoal", at);
	SimKeyValue(f, "goal_no", "1");
	SimKeyValue(f, "mdl", "models/flag.mdl");
	SimKeyValue(f, "goal_activation", "4100");   // 4: dropped (not removed) when lost; 4096: dropitems
	SimKeyValue(f, "goal_effects", "1");         // affects whoever carries it
	SimKeyValue(f, "goal_min", "-16 -16 -24");
	SimKeyValue(f, "goal_max", "16 16 24");
	extern DLL_FUNCTIONS GAME;
	GAME.pfnSpawn(f);
	Frames(1.0f);
	return f;
}
static bool HullInWorld(edict_t *e)
{
	Vector mn = e->v.origin + e->v.mins, mx = e->v.origin + e->v.maxs;
	for (auto &w : g_world)
		if (mn.x < w.mx.x && mx.x > w.mn.x && mn.y < w.mx.y && mx.y > w.mn.y && mn.z < w.mx.z && mx.z > w.mn.z) return true;
	return false;
}
static bool Pickup(edict_t *p, edict_t *f)
{
	f->v.origin = p->v.origin;
	SimTouch(f, p);
	Frames(0.1f);
	return f->v.owner == p;
}

static void S_FlagToss(bool on)
{
	edict_t *al = Player(1, "Alice", "1", "soldier");
	edict_t *f = MakeFlag(al->v.origin + Vector(300, 0, 0));
	if (!Check(Pickup(al, f), "setup: Alice picks up the flag")) return;
	// A wall 20 units in front of her face
	al->v.angles = al->v.v_angle = Vector(0, 0, 0);
	g_world.push_back({ al->v.origin + Vector(40, -100, -40), al->v.origin + Vector(60, 100, 100) });
	g_printLog.clear(); g_msgLog.clear();
	SimClientCommand(al, "dropitems");
	bool dropped = f->v.movetype == MOVETYPE_TOSS;
	printf("  flag movetype %d aiment %p owner %d\n", f->v.movetype, f->v.aiment, f->v.owner ? (int)(f->v.owner - ED) : -1);
	if (on) Check(dropped, "#18: the flag is dropped with a wall in front");
	else Check(!dropped, "(control) without the plugin: 'Not enough room to drop items here'");
}

static void S_FlagInWall(bool on)
{
	edict_t *al = Player(1, "Alice", "1", "soldier");
	edict_t *f = MakeFlag(al->v.origin + Vector(300, 0, 0));
	if (!Check(Pickup(al, f), "setup: Alice picks up the flag")) return;
	// Crouched in a low vent, she is killed: the flag is dropped 26 units above
	// her origin, and its box (24 up) reaches into the vent's ceiling.
	al->v.flags |= FL_DUCKING;
	Vector o = al->v.origin;
	g_world.push_back({ o + Vector(-200, -200, 30), o + Vector(200, 200, 200) });
	Kill(al);
	bool stuck = HullInWorld(f);
	printf("  flag at z=%.0f (Alice at %.0f, ceiling from %.0f), movetype %d: box in ceiling = %d\n", f->v.origin.z, o.z, o.z + 30, f->v.movetype, stuck);
	if (on) Check(f->v.movetype == MOVETYPE_TOSS && !stuck, "#2: the dropped flag does not start inside the ceiling");
	else Check(stuck, "(control) without the plugin it starts inside the ceiling");
}

static void S_DropGrenade(bool on)
{
	edict_t *al = Player(1, "Alice", "1", "soldier");
	edict_t *f = MakeFlag(al->v.origin + Vector(300, 0, 0));
	if (!Check(Pickup(al, f), "setup: Alice picks up the flag")) return;
	F<int>(al, L_GREN1) = 4;
	F<int>(al, L_GREN2) = 0;   // no secondary grenades left
	SimClientCommand(al, "+gren1");
	Frames(0.1f);
	edict_t *g = FindPrimed(al);
	SimClientCommand(al, "dropitems");
	printf("  after dropitems: bRemoveGrenade=%d tfstate=0x%x\n", F<int>(al, L_REMOVEGREN), F<int>(al, L_TFSTATE));
	Frames(1.0f);
	bool alive = g && !g->free && g->v.classname;
	if (on) Check(alive && (F<int>(al, L_TFSTATE) & 1), "#15: the primed grenade survives dropping the flag");
	else Check(!alive || !(F<int>(al, L_TFSTATE) & 1), "(control) without the plugin the primed grenade is deleted");
}

// The same rule on a capture: the flag is taken away by the capture point
// (return_item_no), not by dropitems.
static void S_CapGrenade(bool on)
{
	edict_t *al = Player(1, "Alice", "1", "soldier");
	edict_t *f = MakeFlag(al->v.origin + Vector(300, 0, 0));
	extern DLL_FUNCTIONS GAME;
	edict_t *cap = SimCreateNamed("info_tfgoal", al->v.origin + Vector(-300, 0, 0));
	SimKeyValue(cap, "goal_no", "2");
	SimKeyValue(cap, "goal_activation", "1");    // touch
	SimKeyValue(cap, "goal_effects", "1");       // the player who touched it
	SimKeyValue(cap, "items_allowed", "1");      // only while carrying the flag
	SimKeyValue(cap, "return_item_no", "1");     // takes the flag back: the capture
	SimKeyValue(cap, "goal_min", "-16 -16 -24");
	SimKeyValue(cap, "goal_max", "16 16 24");
	GAME.pfnSpawn(cap);
	Frames(1.0f);
	if (!Check(Pickup(al, f), "setup: Alice picks up the flag")) return;
	F<int>(al, L_GREN1) = 4;
	F<int>(al, L_GREN2) = 0;   // no secondary grenades left
	SimClientCommand(al, "+gren1");
	Frames(0.1f);
	edict_t *g = FindPrimed(al);
	cap->v.origin = al->v.origin;
	SimTouch(cap, al);
	printf("  after the capture: flag owner %d, bRemoveGrenade=%d tfstate=0x%x\n", f->v.owner ? (int)(f->v.owner - ED) : 0,
		F<int>(al, L_REMOVEGREN), F<int>(al, L_TFSTATE));
	if (!Check(f->v.owner != al, "setup: the capture took the flag")) return;
	Frames(1.0f);
	bool alive = g && !g->free && g->v.classname;
	if (on) Check(alive && (F<int>(al, L_TFSTATE) & 1), "#15: the primed grenade survives capturing the flag");
	else Check(!alive || !(F<int>(al, L_TFSTATE) & 1), "(control) without the plugin the primed grenade is deleted on the capture");
}

// ---------------------------------------------------------------------------
// push_fps: a trigger_push "field" (no Push Once flag) launches the same height
// at any fps/tick. The fake engine has no player physics, so we model the one
// piece that matters - the engine folding vertical base velocity into velocity
// once per frame (pm_shared.c PM_AddCorrectGravity) - and drive the real
// CTriggerPush::Touch (plugin off) or the plugin's Touch hook (plugin on).
static void EngineApplyBase(edict_t *p, float ft)
{
	p->v.velocity.z += p->v.basevelocity.z * ft;   // velocity.z += basevelocity.z*frametime
	p->v.basevelocity = Vector(0, 0, 0);           // engine consumes/clears it each frame
	p->v.flags &= ~FL_BASEVELOCITY;
}
// Ride the pad for `frames` server frames at frametime `ft`; return the vertical
// velocity the player ends up with. Two empty frames first so the plugin sees a
// fresh entry (not a continuation of the previous ride).
static float RidePush(edict_t *p, edict_t *pad, int frames, float ft)
{
	for (int k = 0; k < 70; k++) SimFrame(0.01f);   // > 0.5s idle: let any prior launch cooldown expire
	p->v.velocity = Vector(0, 0, 0);
	p->v.basevelocity = Vector(0, 0, 0);
	p->v.flags &= ~FL_ONGROUND;
	for (int i = 0; i < frames; i++)
	{
		SimFrame(ft);
		p->v.origin = pad->v.origin;   // held at the foot of the brush, overlapping
		SimTouch(pad, p);              // plugin launch (superseded) OR game sets base velocity
		EngineApplyBase(p, ft);
	}
	return p->v.velocity.z;
}
static void S_PushFps(bool on)
{
	edict_t *al = Player(1, "Alice", "1", "soldier");
	al->v.gravity = 1.0f;
	edict_t *pad = SimCreateNamed("trigger_push", Vector(0, 0, 0));
	if (!Check(pad != NULL, "setup: trigger_push created")) return;
	pad->v.movedir = Vector(0, 0, 1);   // straight up
	pad->v.speed = 2000.0f;             // upward acceleration, u/s^2
	pad->v.spawnflags = 0;              // a push field, not SF_TRIG_PUSH_ONCE
	pad->v.origin = Vector(0, 0, 0);
	pad->v.absmin = Vector(-64, -64, 0);
	pad->v.absmax = Vector(64, 64, 64);   // 64-unit-tall push volume

	// One-shot: the launch is the pad's own speed, applied once on entry.
	float speed = pad->v.speed;
	printf("  pad speed = %.0f u/s (the one-shot launch)\n", speed);

	if (on)
	{
		float l1 = RidePush(al, pad, 1, 0.01f);     // 1 frame at 100 fps
		float l5 = RidePush(al, pad, 5, 0.01f);     // 5 frames at 100 fps
		float lf = RidePush(al, pad, 50, 0.001f);   // 50 frames at 1000 fps
		printf("  launch: 1f@100=%.1f  5f@100=%.1f  50f@1000=%.1f (speed %.0f)\n", l1, l5, lf, speed);
		Check(fabsf(l1 - speed) < 1.0f,
			"push_fps: launch is the pad's speed, applied once");
		Check(fabsf(l1 - l5) < 1.0f && fabsf(l1 - lf) < 1.0f,
			"push_fps: same launch at any fps and any number of frames on the pad (no accumulation)");
		// Overlapping brushes / a 1000-tick storm of touches: many touches in one
		// frame must still be a single launch, not a stack toward sv_maxvelocity.
		for (int k = 0; k < 70; k++) SimFrame(0.01f);   // clear the cooldown
		al->v.velocity = Vector(0, 0, 0);
		SimFrame(0.01f); al->v.origin = pad->v.origin;
		for (int k = 0; k < 8; k++) SimTouch(pad, al);  // 8 touches, same frame
		printf("  8 touches in one frame -> vz=%.1f (one launch is %.0f)\n", al->v.velocity.z, speed);
		Check(fabsf(al->v.velocity.z - speed) < 1.0f,
			"push_fps: many touches in one frame give one launch (no stacking to the cap)");
		// SET not ADD: hitting a pad while already rising gives the pad's launch,
		// not launch + incoming velocity (no chaining stack toward the cap).
		for (int k = 0; k < 70; k++) SimFrame(0.01f);   // clear cooldown
		SimFrame(0.01f); al->v.origin = pad->v.origin; al->v.velocity = Vector(0, 0, 700);
		SimTouch(pad, al);
		printf("  rising at 700 into the %.0f pad -> vz=%.1f\n", speed, al->v.velocity.z);
		Check(fabsf(al->v.velocity.z - speed) < 1.0f,
			"push_fps: a pad sets the launch, not adds - no stack when rising/chaining into it");
		// A FAN (tall / START_OFF) gives SUSTAINED lift: it holds the upward
		// velocity at the lift speed EVERY frame, not one-shot-then-nothing.
		for (int k = 0; k < 70; k++) SimFrame(0.01f);   // clear the log throttle
		pad->v.absmax.z = 300.0f;                       // 300u tall > 256 = fan
		al->v.velocity = Vector(0, 0, 0);
		bool held = true;
		for (int k = 0; k < 5; k++) {
			SimFrame(0.01f); al->v.origin = pad->v.origin;
			al->v.velocity.z = 50;                      // pretend gravity decayed it
			SimTouch(pad, al);                          // fan should restore it to the lift speed
			if (fabsf(al->v.velocity.z - speed) > 1.0f) held = false;
		}
		printf("  fan (300u tall): vz restored to %.1f each frame after decaying to 50 (lift %.0f)\n",
			al->v.velocity.z, speed);
		Check(held, "push_fps: a fan holds upward velocity at the lift speed every frame (sustained, not one-shot)");
		// Live tuning: the `tfx push_fan_scale <v>` console setter at runtime.
		SimServerCommand("tfx", "push_fan_scale", "0.5");
		for (int k = 0; k < 70; k++) SimFrame(0.01f);
		SimFrame(0.01f); al->v.origin = pad->v.origin; al->v.velocity = Vector(0, 0, 0);
		SimTouch(pad, al);
		printf("  after 'tfx push_fan_scale 0.5': fan lift = %.1f (half of %.0f)\n", al->v.velocity.z, speed);
		Check(fabsf(al->v.velocity.z - speed * 0.5f) < 1.0f,
			"push_fps: 'tfx push_fan_scale <v>' tunes fan lift live");
		SimServerCommand("tfx", "push_fan_scale", "1");
		pad->v.absmax.z = 64.0f; pad->v.spawnflags = 0;
	}
	else
	{
		float o1 = RidePush(al, pad, 1, 0.01f);
		float o5 = RidePush(al, pad, 5, 0.01f);
		printf("  stock launch: 1 frame=%.1f  5 frames=%.1f\n", o1, o5);
		Check(fabsf(o1 - speed) < 1.0f,
			"(control) one stock frame adds the pad's speed");
		Check(o5 > speed * 4.0f,
			"(control) stock accumulates per frame on the pad - 5 frames ~5x (fps/phase dependent)");
	}
}

struct Scenario { const char *name; void (*fn)(bool); };
static Scenario S[] = {
	{ "tele_death_entrance", S_TeleDeathEntrance }, { "tele_death_exit", S_TeleDeathExit }, { "tele_flag", S_TeleFlag },
	{ "tele_leaver", S_TeleLeaver }, { "grenade_spawn", S_GrenadeSpawn }, { "grenade_jump", S_GrenadeJump },
	{ "door_caltrops", S_DoorCaltrops }, { "zero_health", S_ZeroHealth }, { "nails", S_Nails },
	{ "nailgren_ceiling", S_NailGrenCeiling }, { "mirv", S_Mirv }, { "grenade_in_entity", S_GrenadeInEntity },
	{ "camera", S_Camera }, { "conc_respawn", S_ConcRespawn }, { "menus", S_Menus }, { "flag_toss", S_FlagToss }, { "flag_in_wall", S_FlagInWall },
	{ "drop_grenade", S_DropGrenade }, { "cap_grenade", S_CapGrenade }, { "push_fps", S_PushFps },
};

int main(int argc, char **argv)
{
	if (argc < 3) { for (auto &s : S) printf("%s\n", s.name); return 0; }
	bool on = !strcmp(argv[2], "on");
	g_quiet = getenv("FX_VERBOSE") ? 0 : 2;
	SimInit(on);
	for (auto &s : S)
		if (!strcmp(s.name, argv[1]))
		{
			printf("== %s (plugin %s)\n", s.name, on ? "ON" : "OFF");
			s.fn(on);
			printf("== %s: %d passed, %d failed\n", s.name, g_pass, g_fail);
			return g_fail ? 1 : 0;
		}
	printf("no such scenario\n");
	return 2;
}
