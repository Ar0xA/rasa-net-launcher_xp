// The static CRT of current Visual Studio imports a few kernel32 functions that only exist
// from Windows Vista (or Server 2003) on. Windows XP refuses to start an exe whose import
// table names a missing function, so xp_imports.asm points those imports here instead.
// Each shim uses the real function where Windows has it and an XP equivalent otherwise.

#include <windows.h>

namespace {

FARPROC Kernel32(const char* name) { return GetProcAddress(GetModuleHandleW(L"kernel32.dll"), name); }

// Looked up once; a race only means two threads store the same value.
template <typename F>
F Real(F* cache, const char* name) {
    if (*cache == (F)1) *cache = (F)Kernel32(name);
    return *cache;
}

typedef DWORD(WINAPI* FlsAllocFn)(PFLS_CALLBACK_FUNCTION);
typedef PVOID(WINAPI* FlsGetValueFn)(DWORD);
typedef BOOL(WINAPI* FlsSetValueFn)(DWORD, PVOID);
typedef BOOL(WINAPI* FlsFreeFn)(DWORD);
typedef BOOL(WINAPI* InitCsExFn)(LPCRITICAL_SECTION, DWORD, DWORD);

FlsAllocFn g_flsAlloc = (FlsAllocFn)1;
FlsGetValueFn g_flsGetValue = (FlsGetValueFn)1;
FlsSetValueFn g_flsSetValue = (FlsSetValueFn)1;
FlsFreeFn g_flsFree = (FlsFreeFn)1;
InitCsExFn g_initCsEx = (InitCsExFn)1;

}  // namespace

extern "C" {

// Fiber-local storage falls back to thread-local storage. The CRT uses the FLS callback
// to free per-thread data when a thread exits; on XP that small amount is not freed.
DWORD WINAPI xp_FlsAlloc(PFLS_CALLBACK_FUNCTION callback) {
    FlsAllocFn f = Real(&g_flsAlloc, "FlsAlloc");
    return f ? f(callback) : TlsAlloc();
}

PVOID WINAPI xp_FlsGetValue(DWORD index) {
    FlsGetValueFn f = Real(&g_flsGetValue, "FlsGetValue");
    if (f) return f(index);
    DWORD err = GetLastError();  // TlsGetValue clears it; the CRT expects it kept
    PVOID v = TlsGetValue(index);
    SetLastError(err);
    return v;
}

BOOL WINAPI xp_FlsSetValue(DWORD index, PVOID value) {
    FlsSetValueFn f = Real(&g_flsSetValue, "FlsSetValue");
    return f ? f(index, value) : TlsSetValue(index, value);
}

BOOL WINAPI xp_FlsFree(DWORD index) {
    FlsFreeFn f = Real(&g_flsFree, "FlsFree");
    return f ? f(index) : TlsFree(index);
}

BOOL WINAPI xp_InitializeCriticalSectionEx(LPCRITICAL_SECTION cs, DWORD spin, DWORD flags) {
    InitCsExFn f = Real(&g_initCsEx, "InitializeCriticalSectionEx");
    return f ? f(cs, spin, flags) : InitializeCriticalSectionAndSpinCount(cs, spin);
}

}  // extern "C"
