#include "controller_bindings.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <windows.h>

struct ActionBinding {
    const char* key;
    int value;
};

static constexpr ActionBinding defaults[]{
    {"Movement L/R", 0},   {"Movement U/D", 4},   {"Look L/R", 12},      {"Look U/D", 16},
    {"Menu", 55},          {"Combat Pause", 54},  {"Fight Style 1", 32}, {"Fight Style 2", 32},
    {"Fight Style 3", 32}, {"Fight Style 4", 32}, {"Attack", 48},        {"Block", 49},
    {"Strong Attack", 50}, {"Focus", 51},         {"Chi Strike", 53},    {"Chi Heal", 52},
    {"Target Left", 20},   {"Target Right", 20},  {"No Target", -1},     {"Free Look", 57}};
static_assert(std::size(defaults) == std::tuple_size<ControllerBindings>::value);

// Names and offsets used by the game's controller INI reader and binding UI.
static constexpr const char* axes[]{"Left StickX",  "Left StickY",  "Left StickZ",  "Right StickX",
                                    "Right StickY", "Right StickZ", "First Slider", "Second Slider",
                                    "Hat 1",        "Hat 2",        "Hat 3",        "Hat 4"};
static constexpr const char* marker = "ControllerFixBindingsInitialized";

static bool parse_binding(const char* text, int& value) {
    if (!std::strcmp(text, "-")) {
        value = -1;
        return true;
    }
    for (unsigned i = 0; i < std::size(axes); ++i) {
        if (!_stricmp(text, axes[i])) {
            value = static_cast<int>(i * 4);
            return true;
        }
    }
    if (_strnicmp(text, "Button ", 7) || text[7] < '1' || text[7] > '9')
        return false;
    char* end{};
    const long button = std::strtol(text + 7, &end, 10);
    if (*end || button < 1 || button > 32)
        return false;
    value = static_cast<int>(47 + button);
    return true;
}

static const char* binding_name(int value, char (&buffer)[32]) {
    if (value == -1)
        return "-";
    if (value < 48)
        return axes[value / 4];
    sprintf_s(buffer, "Button %d", value - 47);
    return buffer;
}

bool initialize_xinput_bindings(const char* ini_path, const char* profile,
                                ControllerBindings& bindings) {
    for (unsigned i = 0; i < bindings.size(); ++i)
        bindings[i] = defaults[i].value;
    if (!ini_path || !*ini_path || !profile || !*profile || std::strpbrk(profile, "[]\r\n"))
        return false;

    const bool initialized = GetPrivateProfileIntA(profile, marker, 0, ini_path) == 1;
    bool saved = true;
    for (unsigned i = 0; i < bindings.size(); ++i) {
        char text[64]{};
        if (initialized) {
            const DWORD length =
                GetPrivateProfileStringA(profile, defaults[i].key, "", text, sizeof text, ini_path);
            if (length < sizeof text - 1 && parse_binding(text, bindings[i]))
                continue;
        }
        // Missing or invalid entries get a default without resetting other remaps.
        char buffer[32]{};
        if (!WritePrivateProfileStringA(profile, defaults[i].key, binding_name(bindings[i], buffer),
                                        ini_path))
            saved = false;
    }
    // An incomplete first write must be retried on the next launch.
    if (!initialized && saved)
        saved = WritePrivateProfileStringA(profile, marker, "1", ini_path) != FALSE;
    return saved;
}
