// tfx_trace.cpp - fixes_trace.log, beside the DLL.

#include <extdll.h>
#include <meta_api.h>
#include <enginecallback.h>

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>

#include "sdk_compat.h"
#include "mutil.h"
#include "tfx.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

#define TFX_TRACE_FILE "fixes_trace.log"

// The folder this DLL was loaded from - asked of the OS, not of the engine:
// GET_GAME_DIR() proved unreliable for this in Weapon Changer Plus, while the
// module's own path never has been.
static bool TFX_ModuleDir(char *out, size_t len)
{
#ifdef _WIN32
	HMODULE hModule = NULL;
	if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
			(LPCSTR)&TFX_ModuleDir, &hModule))
		return false;
	char path[MAX_PATH];
	DWORD n = GetModuleFileNameA(hModule, path, sizeof(path));
	if (n == 0 || n >= sizeof(path))
		return false;
	char *slash = strrchr(path, '\\');
	if (!slash)
		return false;
	*slash = 0;
	_snprintf_wc(out, len - 1, "%s", path);
#else
	Dl_info info;
	if (!dladdr((void *)&TFX_ModuleDir, &info) || !info.dli_fname)
		return false;
	_snprintf_wc(out, len - 1, "%s", info.dli_fname);
	out[len - 1] = 0;
	char *slash = strrchr(out, '/');
	if (!slash)
		return false;
	*slash = 0;
#endif
	out[len - 1] = 0;
	return true;
}

void TFX_LogModuleIdentity(void)
{
#ifdef _WIN32
	HMODULE hModule = NULL;
	if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
			(LPCSTR)&TFX_LogModuleIdentity, &hModule))
	{
		TFX_Trace("==== build identity: unavailable (GetModuleHandleEx failed) ====");
		return;
	}
	char modulePath[MAX_PATH];
	DWORD len = GetModuleFileNameA(hModule, modulePath, sizeof(modulePath));
	if (len == 0 || len >= sizeof(modulePath))
	{
		TFX_Trace("==== build identity: unavailable (GetModuleFileName failed) ====");
		return;
	}
	WIN32_FILE_ATTRIBUTE_DATA fad;
	if (!GetFileAttributesExA(modulePath, GetFileExInfoStandard, &fad))
	{
		TFX_Trace("==== build identity: %s (timestamp unreadable) ====", modulePath);
		return;
	}
	SYSTEMTIME stUtc, stLocal;
	FileTimeToSystemTime(&fad.ftLastWriteTime, &stUtc);
	SystemTimeToTzSpecificLocalTime(NULL, &stUtc, &stLocal);
	TFX_Trace("==== build identity: %s written %04d-%02d-%02d %02d:%02d:%02d local, %lu bytes ====",
		modulePath, (int)stLocal.wYear, (int)stLocal.wMonth, (int)stLocal.wDay,
		(int)stLocal.wHour, (int)stLocal.wMinute, (int)stLocal.wSecond, (unsigned long)fad.nFileSizeLow);
#else
	char dir[256];
	TFX_Trace("==== build identity: loaded from %s ====", TFX_ModuleDir(dir, sizeof(dir)) ? dir : "(unknown)");
#endif
}

void TFX_Trace(const char *fmt, ...)
{
	char msg[256];
	va_list argptr;
	va_start(argptr, fmt);
	vsnprintf(msg, sizeof(msg), fmt, argptr);
	va_end(argptr);
	msg[sizeof(msg) - 1] = 0;

	char dir[256], path[300];
	if (!TFX_ModuleDir(dir, sizeof(dir)))
		return;
#ifdef _WIN32
	_snprintf_wc(path, sizeof(path) - 1, "%s\\%s", dir, TFX_TRACE_FILE);
#else
	_snprintf_wc(path, sizeof(path) - 1, "%s/%s", dir, TFX_TRACE_FILE);
#endif
	path[sizeof(path) - 1] = 0;

	errno = 0;
	FILE *f = fopen(path, "a");
	if (!f)
	{
		static bool said = false;
		if (!said && gpMetaUtilFuncs)
			LOG_MESSAGE(PLID, "[TFC Fixes] (file log unavailable: fopen(\"%s\") failed, errno=%d)", path, errno);
		said = true;
		return;
	}
	fprintf(f, "[t=%.3f] %s\n", gpGlobals ? gpGlobals->time : -1.0f, msg);
	fflush(f);
	fclose(f);
}

// For the config loader: "<this DLL's folder>/<name>".
bool TFX_PathBesideModule(const char *name, char *out, size_t len)
{
	char dir[256];
	if (!TFX_ModuleDir(dir, sizeof(dir)))
		return false;
#ifdef _WIN32
	_snprintf_wc(out, len - 1, "%s\\%s", dir, name);
#else
	_snprintf_wc(out, len - 1, "%s/%s", dir, name);
#endif
	out[len - 1] = 0;
	return true;
}

// Keep the log from growing forever: past 1 MB it becomes fixes_trace.old.log
// (replacing the previous one) and a new one starts. Called at load.
void TFX_TraceRotate(void)
{
	char path[300], old[300];
	if (!TFX_PathBesideModule(TFX_TRACE_FILE, path, sizeof(path))
		|| !TFX_PathBesideModule("fixes_trace.old.log", old, sizeof(old)))
		return;
	FILE *f = fopen(path, "rb");
	if (!f)
		return;
	fseek(f, 0, SEEK_END);
	long size = ftell(f);
	fclose(f);
	if (size < 1024 * 1024)
		return;
	remove(old);
	rename(path, old);
}
