// Compare the proxy with Windows' real DirectInput DLL. Creates/releases only
// an input interface: no devices are enumerated, acquired, polled or vibrated.
#define DIRECTINPUT_VERSION 0x0800
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <dinput.h>
#include <iterator>
#include <windows.h>

int wmain(int argc, wchar_t** argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    _set_error_mode(_OUT_TO_STDERR);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    assert(argc == 2);
    const auto proxy = LoadLibraryW(argv[1]);
    assert(proxy);
    wchar_t path[32768]{};
    assert(GetSystemDirectoryW(path, 32768));
    wcscat_s(path, L"\\dinput8.dll");
    const auto real = LoadLibraryW(path);
    assert(real && real != proxy);
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
    using Format = const DIDATAFORMAT*(WINAPI*)();
    assert(reinterpret_cast<Format>(GetProcAddress(proxy, exports[5]))() ==
           reinterpret_cast<Format>(GetProcAddress(real, exports[5]))());
    using Create = HRESULT(WINAPI*)(HINSTANCE, DWORD, REFIID, void**, IUnknown*);
    const auto create = reinterpret_cast<Create>(GetProcAddress(proxy, exports[0]));
    const auto original = reinterpret_cast<Create>(GetProcAddress(real, exports[0]));
    const auto instance = GetModuleHandleW(nullptr);
    void *a{}, *b{};
    assert(create(instance, 0, IID_IDirectInput8A, &a, nullptr) ==
           original(instance, 0, IID_IDirectInput8A, &b, nullptr));
    assert(!a && !b);
    IDirectInput8A* input{};
    assert(SUCCEEDED(
        create(instance, 0x800, IID_IDirectInput8A, reinterpret_cast<void**>(&input), nullptr)));
    assert(input);
    input->Release();
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
    FreeLibrary(proxy);
    FreeLibrary(real);
}
