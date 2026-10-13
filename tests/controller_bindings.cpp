#include "../src/controller_bindings.hpp"

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <windows.h>

static constexpr const char* marker = "ControllerFixBindingsInitialized";
static constexpr ControllerBindings expected{0,  4,  12, 16, 55, 54, 32, 32, 32, 32,
                                             48, 49, 50, 51, 53, 52, 20, 20, -1, 57};

static void expect_value(const char* path, const char* profile, const char* key,
                         const char* expected_value) {
    char value[128]{};
    GetPrivateProfileStringA(profile, key, "", value, sizeof value, path);
    assert(!std::strcmp(value, expected_value));
}

int main() {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    _set_error_mode(_OUT_TO_STDERR);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    char directory[MAX_PATH]{}, path[MAX_PATH]{};
    const DWORD length = GetTempPathA(MAX_PATH, directory);
    assert(length && length < MAX_PATH);
    assert(GetTempFileNameA(directory, "jec", 0, path));

    assert(WritePrivateProfileStringA("Config1", "Attack", "MOUSE1", path));
    assert(WritePrivateProfileStringA("InputOptions", "Controller Sensitivity", "3", path));
    assert(WritePrivateProfileStringA("Unrelated Controller", "Attack", "Button 9", path));
    ControllerBindings bindings{};
    // Device names must not affect defaults, including generic fallback profiles.
    const char* profiles[]{"Controller (8BitDo Ultimate wireless Controller for PC)",
                           "Controller (XBOX 360 For Windows)", "Third-party XInput Gamepad",
                           "XInput Controller"};
    for (const char* profile : profiles) {
        assert(WritePrivateProfileStringA(profile, "Look L/R", "Left StickZ", path));
        assert(WritePrivateProfileStringA(profile, "Menu", "Button 11", path));
        assert(WritePrivateProfileStringA(profile, "Unrelated setting", "keep", path));
        bindings.fill(-99);
        assert(initialize_xinput_bindings(path, profile, bindings));
        assert(bindings == expected);
        expect_value(path, profile, marker, "1");
        expect_value(path, profile, "Look L/R", "Right StickX");
        expect_value(path, profile, "Look U/D", "Right StickY");
        expect_value(path, profile, "Menu", "Button 8");
        expect_value(path, profile, "Combat Pause", "Button 7");
        expect_value(path, profile, "Chi Strike", "Button 6");
        expect_value(path, profile, "Chi Heal", "Button 5");
        expect_value(path, profile, "Target Left", "Right StickZ");
        expect_value(path, profile, "Target Right", "Right StickZ");
        expect_value(path, profile, "No Target", "-");
        expect_value(path, profile, "Free Look", "Button 10");
        expect_value(path, profile, "Unrelated setting", "keep");
        // A new launch must load saved bindings, even if native startup supplied bad ones.
        bindings.fill(-99);
        assert(initialize_xinput_bindings(path, profile, bindings));
        assert(bindings == expected);
    }

    // Verify persistence through file I/O independently of the profile API's cache.
    {
        std::ifstream file(path, std::ios::binary);
        assert(file);
        const std::string contents{std::istreambuf_iterator<char>(file),
                                   std::istreambuf_iterator<char>()};
        assert(contents.find("Look L/R=Right StickX") != std::string::npos);
        assert(contents.find("Menu=Button 8") != std::string::npos);
        assert(contents.find("ControllerFixBindingsInitialized=1") != std::string::npos);
    }

    const char* profile = profiles[2];
    assert(WritePrivateProfileStringA(profile, "Attack", "Button 2", path));
    assert(WritePrivateProfileStringA(profile, "Block", "-", path));
    assert(WritePrivateProfileStringA(profile, "Target Left", "Button 11", path));
    assert(WritePrivateProfileStringA(profile, "Target Right", "Button 12", path));
    assert(WritePrivateProfileStringA(profile, "Look U/D", "left stickY", path));
    assert(WritePrivateProfileStringA(profile, "Fight Style 1", "Hat 4", path));
    assert(WritePrivateProfileStringA(profile, "Focus", "Button 32", path));
    assert(WritePrivateProfileStringA(profile, "Movement L/R", "Second Slider", path));
    ControllerBindings remapped = expected;
    remapped[10] = 49;
    remapped[11] = -1;
    remapped[16] = 58;
    remapped[17] = 59;
    remapped[3] = 4;
    remapped[6] = 44;
    remapped[13] = 79;
    remapped[0] = 28;
    assert(initialize_xinput_bindings(path, profile, bindings));
    assert(bindings == remapped);
    expect_value(path, profile, "Look U/D", "left stickY");

    // Repair individual missing/corrupt entries without disturbing valid user remaps.
    assert(WritePrivateProfileStringA(profile, "Menu", nullptr, path));
    for (const char* invalid : {"", "Button 0", "Button 33", "Button -1", "Button +1",
                                "Button 1junk", "Left Stick", "Button 99999999999999999999999"}) {
        assert(WritePrivateProfileStringA(profile, "Chi Heal", invalid, path));
        assert(initialize_xinput_bindings(path, profile, bindings));
        assert(bindings == remapped);
        expect_value(path, profile, "Chi Heal", "Button 5");
    }
    expect_value(path, profile, "Menu", "Button 8");
    assert(WritePrivateProfileStringA(profile, "Menu", std::string(100, 'X').c_str(), path));
    assert(initialize_xinput_bindings(path, profile, bindings));
    assert(bindings == remapped);
    // A different controller profile is independent of this profile's remaps.
    assert(initialize_xinput_bindings(path, profiles[0], bindings));
    assert(bindings == expected);
    expect_value(path, profile, "Attack", "Button 2");
    // Removing the marker explicitly requests the standard layout again.
    assert(WritePrivateProfileStringA(profile, marker, nullptr, path));
    assert(initialize_xinput_bindings(path, profile, bindings));
    assert(bindings == expected);

    // A failed write still supplies live defaults and must not record completion.
    assert(SetFileAttributesA(path, FILE_ATTRIBUTE_READONLY));
    assert(initialize_xinput_bindings(path, profile, bindings));
    assert(bindings == expected);
    assert(!initialize_xinput_bindings(path, "Write failure", bindings));
    assert(bindings == expected);
    assert(GetPrivateProfileIntA("Write failure", marker, 0, path) == 0);
    assert(SetFileAttributesA(path, FILE_ATTRIBUTE_NORMAL));
    assert(initialize_xinput_bindings(path, "Write failure", bindings));
    assert(bindings == expected);
    expect_value(path, "Write failure", marker, "1");
    assert(!initialize_xinput_bindings("", profile, bindings));
    assert(bindings == expected);
    assert(!initialize_xinput_bindings(path, "", bindings));
    assert(!initialize_xinput_bindings(path, "Bad\n[section]", bindings));
    expect_value(path, "Config1", "Attack", "MOUSE1");
    expect_value(path, "InputOptions", "Controller Sensitivity", "3");
    expect_value(path, "Unrelated Controller", "Attack", "Button 9");
    assert(DeleteFileA(path));
    std::puts("Controller bindings passed: first setup, saved remaps, profile isolation, repairs "
              "and write failures");
}
