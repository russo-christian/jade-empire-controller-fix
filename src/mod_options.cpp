#include "mod_options.hpp"

#include <cwchar>
#include <initializer_list>
#include <windows.h>

ModOptions mod_options;

static bool ini_switch(const wchar_t* path, const wchar_t* section, const wchar_t* key,
                       bool fallback) {
    wchar_t value[32]{};
    const DWORD size = GetPrivateProfileStringW(section, key, L"", value, 32, path);
    if (size == 31)
        return fallback; // A truncated or malformed value is not a switch.
    wchar_t* first = value;
    while (*first == L' ' || *first == L'\t')
        ++first;
    wchar_t* end = first + std::wcslen(first);
    while (end != first && (end[-1] == L' ' || end[-1] == L'\t'))
        --end;
    *end = 0;
    for (const auto* enabled : {L"1", L"true", L"on", L"yes"})
        if (!_wcsicmp(first, enabled))
            return true;
    for (const auto* disabled : {L"0", L"false", L"off", L"no"})
        if (!_wcsicmp(first, disabled))
            return false;
    return fallback;
}

// Called once during hook installation, outside the loader lock.
// Resolve the DLL directory because launchers may use another working directory.
void load_mod_options() {
    mod_options = {};
    HMODULE module{};
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(&mod_options), &module))
        return;
    wchar_t path[32768]{};
    const DWORD length = GetModuleFileNameW(module, path, 32768);
    if (!length || length >= 32768)
        return;
    wchar_t* name = std::wcsrchr(path, L'\\');
    if (!name)
        return;
    ++name;
    const size_t remaining = 32768 - static_cast<size_t>(name - path);
    if (remaining < sizeof(L"JadeEmpireControllerFix.ini") / sizeof(wchar_t))
        return;
    wcscpy_s(name, remaining, L"JadeEmpireControllerFix.ini");
    mod_options.right_stick_target_switching =
        ini_switch(path, L"Controls", L"RightStickTargetSwitching", true);
    mod_options.controller_only = ini_switch(path, L"Controls", L"ControllerOnly", true);
    mod_options.controller_prompts = ini_switch(path, L"Interface", L"ControllerPrompts", true);
    mod_options.rumble = ini_switch(path, L"Feedback", L"Rumble", true);
}
