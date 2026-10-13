// Compare the proxy with Windows' real DirectInput DLL. Creates/releases only
// an input interface: no devices are enumerated, acquired, polled or vibrated.
#define DIRECTINPUT_VERSION 0x0800
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <dinput.h>
#include <iterator>
#include <windows.h>

static unsigned hidden_exports{};

static FARPROC WINAPI without_joystick_export(HMODULE module, const char* name) {
    if (!IS_INTRESOURCE(name) && !std::strcmp(name, "GetdfDIJoystick")) {
        ++hidden_exports;
        SetLastError(ERROR_PROC_NOT_FOUND);
        return nullptr;
    }
    return GetProcAddress(module, name);
}

// Change only this test process's proxy import, leaving Windows and the DLL on
// disk untouched. This reproduces Wine's missing optional export on Windows.
static void hide_joystick_export(HMODULE proxy) {
    const auto base = reinterpret_cast<unsigned char*>(proxy);
    const auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    const auto pe = reinterpret_cast<const IMAGE_NT_HEADERS32*>(base + dos->e_lfanew);
    const auto& directory = pe->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    assert(directory.VirtualAddress);
    auto descriptor =
        reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR*>(base + directory.VirtualAddress);
    for (; descriptor->Name; ++descriptor) {
        assert(descriptor->OriginalFirstThunk);
        auto names =
            reinterpret_cast<const IMAGE_THUNK_DATA32*>(base + descriptor->OriginalFirstThunk);
        auto addresses = reinterpret_cast<IMAGE_THUNK_DATA32*>(base + descriptor->FirstThunk);
        for (; names->u1.AddressOfData; ++names, ++addresses) {
            if (IMAGE_SNAP_BY_ORDINAL32(names->u1.Ordinal))
                continue;
            const auto import =
                reinterpret_cast<const IMAGE_IMPORT_BY_NAME*>(base + names->u1.AddressOfData);
            if (std::strcmp(import->Name, "GetProcAddress"))
                continue;
            DWORD protection{}, ignored{};
            assert(VirtualProtect(&addresses->u1.Function, sizeof(DWORD), PAGE_READWRITE,
                                  &protection));
            addresses->u1.Function = reinterpret_cast<DWORD>(&without_joystick_export);
            assert(VirtualProtect(&addresses->u1.Function, sizeof(DWORD), protection, &ignored));
            return;
        }
    }
    assert(false && "Proxy must import GetProcAddress");
}

static void check_format(const DIDATAFORMAT* actual) {
    const auto& expected = c_dfDIJoystick;
    assert(actual && actual->dwSize == expected.dwSize);
    assert(actual->dwObjSize == expected.dwObjSize && actual->dwFlags == expected.dwFlags);
    assert(actual->dwDataSize == expected.dwDataSize && actual->dwNumObjs == expected.dwNumObjs);
    assert(actual->rgodf);
    for (DWORD i = 0; i < expected.dwNumObjs; ++i) {
        const auto& a = actual->rgodf[i];
        const auto& b = expected.rgodf[i];
        assert(a.dwOfs == b.dwOfs && a.dwType == b.dwType && a.dwFlags == b.dwFlags);
        assert((a.pguid == nullptr) == (b.pguid == nullptr));
        assert(!a.pguid || IsEqualGUID(*a.pguid, *b.pguid));
    }
}

int wmain(int argc, wchar_t** argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    _set_error_mode(_OUT_TO_STDERR);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    assert(argc == 2 || (argc == 3 && !std::wcscmp(argv[2], L"--without-joystick-export")));
    const bool hide_export = argc == 3;
    const auto proxy = LoadLibraryW(argv[1]);
    assert(proxy);
    wchar_t path[32768]{};
    assert(GetSystemDirectoryW(path, 32768));
    wcscat_s(path, L"\\dinput8.dll");
    const auto real = LoadLibraryW(path);
    assert(real && real != proxy);
    if (hide_export)
        hide_joystick_export(proxy);
    const char* exports[]{"DirectInput8Create", "DllCanUnloadNow",     "DllGetClassObject",
                          "DllRegisterServer",  "DllUnregisterServer", "GetdfDIJoystick"};
    // Keep the proxy's public interface limited to the system DLL's six exports.
    const auto base = reinterpret_cast<const unsigned char*>(proxy);
    const auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    assert(dos->e_magic == IMAGE_DOS_SIGNATURE);
    const auto pe = reinterpret_cast<const IMAGE_NT_HEADERS32*>(base + dos->e_lfanew);
    assert(pe->Signature == IMAGE_NT_SIGNATURE);
    assert(pe->OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR32_MAGIC);
    const auto& directory = pe->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    assert(directory.VirtualAddress && directory.Size >= sizeof(IMAGE_EXPORT_DIRECTORY));
    const auto table =
        reinterpret_cast<const IMAGE_EXPORT_DIRECTORY*>(base + directory.VirtualAddress);
    assert(table->Base == 1 && table->NumberOfFunctions == std::size(exports) &&
           table->NumberOfNames == std::size(exports));
    for (unsigned i = 0; i < 6; ++i) {
        assert(GetProcAddress(proxy, exports[i]));
        assert(GetProcAddress(proxy, exports[i]) == GetProcAddress(proxy, MAKEINTRESOURCEA(i + 1)));
    }
    using Create = HRESULT(WINAPI*)(HINSTANCE, DWORD, REFIID, void**, IUnknown*);
    const auto create = reinterpret_cast<Create>(GetProcAddress(proxy, exports[0]));
    const auto original = reinterpret_cast<Create>(GetProcAddress(real, exports[0]));
    const auto instance = GetModuleHandleW(nullptr);
    IDirectInput8A* input{};
    const HRESULT created =
        create(instance, 0x800, IID_IDirectInput8A, reinterpret_cast<void**>(&input), nullptr);
    if (FAILED(created)) {
        std::printf("DirectInput8Create failed: 0x%08lx (hidden exports: %u)\n",
                    static_cast<unsigned long>(created), hidden_exports);
        return 1;
    }
    assert(input);
    input->Release();
    using Format = const DIDATAFORMAT*(WINAPI*)();
    const auto actual_format = reinterpret_cast<Format>(GetProcAddress(proxy, exports[5]))();
    const auto real_format = reinterpret_cast<Format>(GetProcAddress(real, exports[5]));
    if (real_format && !hide_export)
        assert(actual_format == real_format());
    else
        check_format(actual_format);
    if (hide_export)
        assert(hidden_exports == 1);
    void *a{}, *b{};
    assert(create(instance, 0, IID_IDirectInput8A, &a, nullptr) ==
           original(instance, 0, IID_IDirectInput8A, &b, nullptr));
    assert(!a && !b);
    using ClassObject = HRESULT(WINAPI*)(REFCLSID, REFIID, void**);
    const GUID absent{};
    assert(reinterpret_cast<ClassObject>(GetProcAddress(proxy, exports[2]))(
               absent, IID_IDirectInput8A, &a) ==
           reinterpret_cast<ClassObject>(GetProcAddress(real, exports[2]))(absent,
                                                                           IID_IDirectInput8A, &b));
    assert(!a && !b);
    using CanUnload = HRESULT(WINAPI*)();
    assert(reinterpret_cast<CanUnload>(GetProcAddress(proxy, exports[1]))() ==
           reinterpret_cast<CanUnload>(GetProcAddress(real, exports[1]))());
    // Registration exports are resolved but never invoked: they would edit Windows.
    std::puts("System DirectInput forwarding passed: exactly six named/ordinal exports, joystick "
              "format, COM interface, error results and unload behavior");
    if (hide_export)
        std::puts("Missing GetdfDIJoystick export passed: input creation and SDK format fallback");
    FreeLibrary(proxy);
    FreeLibrary(real);
}
