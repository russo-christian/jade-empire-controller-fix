#include "xbox_controls.hpp"

#include <cstdint>
#include <cwchar>
#include <intrin.h>

// Drop-in DirectInput 8 proxy. Initialization happens on an exported call,
// never in DllMain. The verified game calls DirectInput8Create at 0x6672C3
// inside input construction, before it creates devices or polls controllers.

static INIT_ONCE directinput_once = INIT_ONCE_STATIC_INIT;
static INIT_ONCE controls_once = INIT_ONCE_STATIC_INIT;
static FARPROC directinput_exports[6]{};
static DWORD directinput_error = ERROR_MOD_NOT_FOUND;
static bool controls_installed = false;

static BOOL CALLBACK load_directinput(PINIT_ONCE, void*, void**) {
    wchar_t path[32768]{};
    const UINT length = GetSystemDirectoryW(path, 32768);
    if (!length || length >= 32768 - 13) {
        directinput_error = ERROR_BAD_PATHNAME;
        return TRUE;
    }
    wcscat_s(path, L"\\dinput8.dll");
    // An absolute system path avoids loading this proxy again through DLL search.
    HMODULE module = LoadLibraryW(path);
    if (!module) {
        directinput_error = GetLastError();
        return TRUE;
    }
    const char* names[]{"DirectInput8Create", "DllCanUnloadNow",     "DllGetClassObject",
                        "DllRegisterServer",  "DllUnregisterServer", "GetdfDIJoystick"};
    for (unsigned i = 0; i < 6; ++i) {
        directinput_exports[i] = GetProcAddress(module, names[i]);
        if (!directinput_exports[i]) {
            directinput_error = ERROR_PROC_NOT_FOUND;
            return TRUE;
        }
    }
    directinput_error = ERROR_SUCCESS;
    return TRUE;
}

static bool directinput_ready() {
    InitOnceExecuteOnce(&directinput_once, load_directinput, nullptr, nullptr);
    return directinput_error == ERROR_SUCCESS;
}

static BOOL CALLBACK install_controls(PINIT_ONCE, void*, void**) {
    controls_installed = install_controller_hooks() == ERROR_SUCCESS;
    // Installed jumps refer into this DLL until process exit; forbid unloading it.
    if (controls_installed) {
        HMODULE pinned{};
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                           reinterpret_cast<LPCWSTR>(&controls_installed), &pinned);
    }
    return TRUE; // A mismatch is final for this launch, with DirectInput still usable.
}

extern "C" HRESULT WINAPI ProxyDirectInput8Create(HINSTANCE instance, DWORD version, REFIID iid,
                                                  void** output, IUnknown* outer) {
    if (!directinput_ready())
        return HRESULT_FROM_WIN32(directinput_error);
    const auto caller = reinterpret_cast<uintptr_t>(_ReturnAddress());
    const auto main = GetModuleHandleW(nullptr);
    // These guards leave configuration tools and unrelated applications alone.
    // Hook installation validates every signature before any memory is changed.
    if (caller == 0x6672c8 && main == reinterpret_cast<HMODULE>(0x400000) && instance == main) {
        InitOnceExecuteOnce(&controls_once, install_controls, nullptr, nullptr);
    }
    using Create = HRESULT(WINAPI*)(HINSTANCE, DWORD, REFIID, void**, IUnknown*);
    return reinterpret_cast<Create>(directinput_exports[0])(instance, version, iid, output, outer);
}

extern "C" HRESULT WINAPI ProxyDllCanUnloadNow() {
    if (controls_installed)
        return S_FALSE;
    if (!directinput_ready())
        return S_FALSE;
    return reinterpret_cast<HRESULT(WINAPI*)()>(directinput_exports[1])();
}
extern "C" HRESULT WINAPI ProxyDllGetClassObject(REFCLSID clsid, REFIID iid, void** output) {
    if (!directinput_ready())
        return HRESULT_FROM_WIN32(directinput_error);
    return reinterpret_cast<HRESULT(WINAPI*)(REFCLSID, REFIID, void**)>(directinput_exports[2])(
        clsid, iid, output);
}
extern "C" HRESULT WINAPI ProxyDllRegisterServer() {
    if (!directinput_ready())
        return HRESULT_FROM_WIN32(directinput_error);
    return reinterpret_cast<HRESULT(WINAPI*)()>(directinput_exports[3])();
}
extern "C" HRESULT WINAPI ProxyDllUnregisterServer() {
    if (!directinput_ready())
        return HRESULT_FROM_WIN32(directinput_error);
    return reinterpret_cast<HRESULT(WINAPI*)()>(directinput_exports[4])();
}
extern "C" const void* WINAPI ProxyGetdfDIJoystick() {
    if (!directinput_ready())
        return nullptr;
    return reinterpret_cast<const void*(WINAPI*)()>(directinput_exports[5])();
}
