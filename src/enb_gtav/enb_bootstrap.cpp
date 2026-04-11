//=============================================================================
//  enb_bootstrap.cpp — version.dll Bootstrap Proxy
//
//  GTA V v1.0.3751+ uses LOAD_LIBRARY_SEARCH_SYSTEM32 for d3d11.dll,
//  which bypasses the application directory. This bootstrap deploys as
//  version.dll (which IS loaded from the app directory via static import)
//  and force-loads our d3d11.dll from the game directory.
//
//  Once our d3d11.dll is loaded, the Windows loader will use it instead
//  of the system one when GTA V later calls LoadLibrary("d3d11.dll")
//  because it's already in the loaded module list.
//
//  version.dll exports are forwarded to the real system version.dll.
//=============================================================================

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <cstdio>

// ═══════════════════════════════════════════════════════════════════════════
//  Real version.dll function pointers
// ═══════════════════════════════════════════════════════════════════════════

static HMODULE s_realVersionDLL = nullptr;

// version.dll exports (6 functions that GTA V imports)
static FARPROC s_GetFileVersionInfoA     = nullptr;
static FARPROC s_GetFileVersionInfoSizeA = nullptr;
static FARPROC s_GetFileVersionInfoSizeW = nullptr;
static FARPROC s_GetFileVersionInfoW     = nullptr;
static FARPROC s_VerQueryValueA          = nullptr;
static FARPROC s_VerQueryValueW          = nullptr;

static void LoadRealVersionDLL()
{
    if (s_realVersionDLL) return;

    char systemDir[MAX_PATH];
    GetSystemDirectoryA(systemDir, MAX_PATH);

    char realPath[MAX_PATH];
    snprintf(realPath, MAX_PATH, "%s\\version.dll", systemDir);

    s_realVersionDLL = LoadLibraryA(realPath);
    if (!s_realVersionDLL) return;

    s_GetFileVersionInfoA     = GetProcAddress(s_realVersionDLL, "GetFileVersionInfoA");
    s_GetFileVersionInfoSizeA = GetProcAddress(s_realVersionDLL, "GetFileVersionInfoSizeA");
    s_GetFileVersionInfoSizeW = GetProcAddress(s_realVersionDLL, "GetFileVersionInfoSizeW");
    s_GetFileVersionInfoW     = GetProcAddress(s_realVersionDLL, "GetFileVersionInfoW");
    s_VerQueryValueA          = GetProcAddress(s_realVersionDLL, "VerQueryValueA");
    s_VerQueryValueW          = GetProcAddress(s_realVersionDLL, "VerQueryValueW");
}

// ═══════════════════════════════════════════════════════════════════════════
//  Forwarded version.dll exports
// ═══════════════════════════════════════════════════════════════════════════

extern "C" {

__declspec(dllexport) BOOL WINAPI ENB_GetFileVersionInfoA(
    LPCSTR lptstrFilename, DWORD dwHandle, DWORD dwLen, LPVOID lpData)
{
    LoadRealVersionDLL();
    if (!s_GetFileVersionInfoA) return FALSE;
    typedef BOOL(WINAPI* PFN)(LPCSTR, DWORD, DWORD, LPVOID);
    return reinterpret_cast<PFN>(s_GetFileVersionInfoA)(lptstrFilename, dwHandle, dwLen, lpData);
}

__declspec(dllexport) DWORD WINAPI ENB_GetFileVersionInfoSizeA(LPCSTR lptstrFilename, LPDWORD lpdwHandle)
{
    LoadRealVersionDLL();
    if (!s_GetFileVersionInfoSizeA) return 0;
    typedef DWORD(WINAPI* PFN)(LPCSTR, LPDWORD);
    return reinterpret_cast<PFN>(s_GetFileVersionInfoSizeA)(lptstrFilename, lpdwHandle);
}

__declspec(dllexport) DWORD WINAPI ENB_GetFileVersionInfoSizeW(LPCWSTR lptstrFilename, LPDWORD lpdwHandle)
{
    LoadRealVersionDLL();
    if (!s_GetFileVersionInfoSizeW) return 0;
    typedef DWORD(WINAPI* PFN)(LPCWSTR, LPDWORD);
    return reinterpret_cast<PFN>(s_GetFileVersionInfoSizeW)(lptstrFilename, lpdwHandle);
}

__declspec(dllexport) BOOL WINAPI ENB_GetFileVersionInfoW(
    LPCWSTR lptstrFilename, DWORD dwHandle, DWORD dwLen, LPVOID lpData)
{
    LoadRealVersionDLL();
    if (!s_GetFileVersionInfoW) return FALSE;
    typedef BOOL(WINAPI* PFN)(LPCWSTR, DWORD, DWORD, LPVOID);
    return reinterpret_cast<PFN>(s_GetFileVersionInfoW)(lptstrFilename, dwHandle, dwLen, lpData);
}

__declspec(dllexport) BOOL WINAPI ENB_VerQueryValueA(
    LPCVOID pBlock, LPCSTR lpSubBlock, LPVOID* lplpBuffer, PUINT puLen)
{
    LoadRealVersionDLL();
    if (!s_VerQueryValueA) return FALSE;
    typedef BOOL(WINAPI* PFN)(LPCVOID, LPCSTR, LPVOID*, PUINT);
    return reinterpret_cast<PFN>(s_VerQueryValueA)(pBlock, lpSubBlock, lplpBuffer, puLen);
}

__declspec(dllexport) BOOL WINAPI ENB_VerQueryValueW(
    LPCVOID pBlock, LPCWSTR lpSubBlock, LPVOID* lplpBuffer, PUINT puLen)
{
    LoadRealVersionDLL();
    if (!s_VerQueryValueW) return FALSE;
    typedef BOOL(WINAPI* PFN)(LPCVOID, LPCWSTR, LPVOID*, PUINT);
    return reinterpret_cast<PFN>(s_VerQueryValueW)(pBlock, lpSubBlock, lplpBuffer, puLen);
}

} // extern "C"

// ═══════════════════════════════════════════════════════════════════════════
//  DllMain — Force-load our d3d11.dll ENB proxy
// ═══════════════════════════════════════════════════════════════════════════

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
    if (fdwReason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(hinstDLL);

        // Get our directory (same as the game exe)
        char ourDir[MAX_PATH];
        GetModuleFileNameA(hinstDLL, ourDir, MAX_PATH);
        char* lastSlash = strrchr(ourDir, '\\');
        if (lastSlash) *lastSlash = '\0';

        // Force-load our d3d11.dll from the game directory
        // This ensures it's in the loaded module list BEFORE GTA V
        // calls LoadLibrary("d3d11.dll") with SEARCH_SYSTEM32.
        char enbPath[MAX_PATH];
        snprintf(enbPath, MAX_PATH, "%s\\d3d11.dll", ourDir);

        HMODULE hENB = LoadLibraryA(enbPath);
        if (hENB)
        {
            // Write confirmation
            char markerPath[MAX_PATH];
            snprintf(markerPath, MAX_PATH, "%s\\enb_loaded.txt", ourDir);
            HANDLE hFile = CreateFileA(markerPath, GENERIC_WRITE, 0, nullptr,
                                        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (hFile != INVALID_HANDLE_VALUE)
            {
                char msg[512];
                int len = snprintf(msg, sizeof(msg),
                    "ENBSeries bootstrap (version.dll) loaded d3d11.dll at %p\r\n"
                    "Game directory: %s\r\n", hENB, ourDir);
                DWORD written;
                WriteFile(hFile, msg, len, &written, nullptr);
                CloseHandle(hFile);
            }
        }
    }
    else if (fdwReason == DLL_PROCESS_DETACH)
    {
        if (s_realVersionDLL)
        {
            FreeLibrary(s_realVersionDLL);
            s_realVersionDLL = nullptr;
        }
    }

    return TRUE;
}
