#include "xinput_backend.hpp"

#include "combat_stick.hpp"
#include "controller_bindings.hpp"
#include "game_memory.hpp"
#include "mod_options.hpp"

#include <array>
#include <cmath>
#include <cstring>
#include <windows.h>
#include <xinput.h>

// The legacy poll still owns keyboard/mouse, binding lookup, activity tracking
// and canonical packet history. Substitute only its primary joystick sample.
// Keep XInput ownership after disconnect so stale DirectInput cannot take over.
struct XInputBackend {
    using GetState = DWORD(WINAPI*)(DWORD, XINPUT_STATE*);
    using SetState = DWORD(WINAPI*)(DWORD, XINPUT_VIBRATION*);
    GetState get_state{};
    SetState set_state{};
    bool initialized{}, claimed{}, connected{}, armed{}, scanned{};
    void* controller{};
    DWORD slot{}, last_scan{};
    std::array<Byte, 80> raw{};
    HWND window{};
    WNDPROC window_proc{};
    unsigned focus_epoch{}, seen_focus_epoch{};
    WORD motors[2]{};
    DWORD motor_started[2]{}, motor_duration[2]{};
    XINPUT_VIBRATION sent{};
    bool sent_valid{};
};
static XInputBackend xinput;

bool controller_controls_active(void* engine) {
    if (!mod_options.controller_only)
        return true;
    void* controller = static_cast<Byte*>(engine) + 0x5ac;
    void* settings = static_cast<Byte*>(controller) + 0x58;
    if (!field<unsigned>(controller, 0x224))
        return false;
    if (xinput.claimed && controller == xinput.controller) {
        if (!xinput.connected || !xinput.armed)
            return false;
    } else if (!field<unsigned>(controller, 0x3308))
        return false;
    // These are the native poll's current/previous keyboard and mouse samples.
    // Give desktop input priority during simultaneous use, including its release
    // frame; do not let a held stick turn keyboard strafing into Xbox movement.
    for (unsigned i = 0; i < 512; ++i)
        if (field<Byte>(settings, 0x328 + i))
            return false;
    for (unsigned offset : {0x528u, 0x538u}) {
        const int x = field<int>(settings, offset), y = field<int>(settings, offset + 4);
        if (x || y || field<int>(settings, offset + 8) || field<unsigned>(settings, offset + 12))
            return false;
    }
    if (field<unsigned>(settings, 0x324) == 1)
        return true;
    // Native activity detection needs 50% deflection, but calibrated axes start
    // at 35%. Accept actual filtered stick movement after desktop input is ruled
    // out so gentle stick input switches immediately as well.
    for (unsigned offset : {0x10u, 0x12u, 0x14u, 0x16u})
        if (field<short>(controller, offset))
            return true;
    return false;
}

struct RumbleContext {
    void* engine{};
    void* campaign{};
    void* player{};
    uint64_t area{};
    bool operator==(const RumbleContext& other) const {
        return engine == other.engine && campaign == other.campaign && player == other.player &&
               area == other.area;
    }
};
static RumbleContext rumble_context;
static unsigned rumble_suspensions{};

static RumbleContext rumble_gameplay_context() {
    void* engine = *reinterpret_cast<void**>(0x76c924);
    if (!engine || !*reinterpret_cast<unsigned*>(0x75ecdc))
        return {};
    const unsigned mode = field<unsigned>(engine, 0x38);
    // Native input modes: combat (0), flying minigame (2), exploration (5).
    if ((mode != 0 && mode != 2 && mode != 5) || field<unsigned>(engine, 0x3c) ||
        field<unsigned>(engine, 0x40) || field<unsigned>(engine, 0x48))
        return {};
    void* campaign = field<void*>(engine, 0xa0);
    if (!campaign || field<unsigned>(campaign, 4) != 1)
        return {};
    void** players = field<void**>(campaign, 0);
    if (!players || !players[0])
        return {};
    return {engine, campaign, players[0], field<uint64_t>(campaign, 0x11c)};
}

static void xinput_send_rumble() {
    XINPUT_VIBRATION value{xinput.motors[0], xinput.motors[1]};
    if (!xinput.set_state || !xinput.connected)
        return;
    if (!xinput.sent_valid || std::memcmp(&value, &xinput.sent, sizeof value)) {
        const DWORD result = xinput.set_state(xinput.slot, &value);
        // A failed stop must be retried, not cached as already delivered.
        xinput.sent_valid = result == ERROR_SUCCESS;
        if (xinput.sent_valid)
            xinput.sent = value;
    }
}

static void xinput_stop_rumble() {
    xinput.motors[0] = xinput.motors[1] = 0;
    xinput.motor_duration[0] = xinput.motor_duration[1] = 0;
    xinput_send_rumble();
}

static void xinput_cancel_rumble() {
    rumble_context = {};
    xinput_stop_rumble();
}

// Cancel before native pause/loading work begins, even if it stops polling or
// reenters the input collector. Only a later gameplay sample can enable effects.
struct RumbleSuspension {
    RumbleSuspension() {
        ++rumble_suspensions;
        xinput_cancel_rumble();
    }
    ~RumbleSuspension() {
        --rumble_suspensions;
    }
};

static void __declspec(naked) original_game_pause() {
    __asm {
        push esi
        mov esi,ecx
        cmp dword ptr [esi+0x48],0
        push 0x40be77
        ret
    }
}

void __fastcall controller_game_pause(void* engine, void*, unsigned paused, unsigned display) {
    using Pause = void(__thiscall*)(void*, unsigned, unsigned);
    const auto original = reinterpret_cast<Pause>(&original_game_pause);
    if (paused) {
        RumbleSuspension stop;
        original(engine, paused, display);
    } else
        original(engine, paused, display);
}

static void __declspec(naked) original_combat_pause() {
    __asm {
        push esi
        mov esi,ecx
        mov eax,[esi+0xa0]
        push 0x4094c9
        ret
    }
}

void __fastcall controller_combat_pause(void* engine, void*, unsigned paused, unsigned display) {
    using Pause = void(__thiscall*)(void*, unsigned, unsigned);
    const auto original = reinterpret_cast<Pause>(&original_combat_pause);
    if (paused) {
        RumbleSuspension stop;
        original(engine, paused, display);
    } else
        original(engine, paused, display);
}

static void __declspec(naked) original_input_mode() {
    __asm {
        push esi
        push edi
        mov edi,[esp+0xc]
        push 0x40a486
        ret
    }
}

void __fastcall controller_input_mode(void* engine, void*, unsigned mode) {
    using Mode = void(__thiscall*)(void*, unsigned);
    const auto original = reinterpret_cast<Mode>(&original_input_mode);
    if (mode != field<unsigned>(engine, 0x38) || (mode != 0 && mode != 2 && mode != 5)) {
        RumbleSuspension stop;
        original(engine, mode);
    } else
        original(engine, mode);
}

void* __fastcall controller_loading_screen(void* panel, void*, void* manager, unsigned loading) {
    RumbleSuspension stop;
    using Create = void*(__thiscall*)(void*, void*, unsigned);
    return reinterpret_cast<Create>(0x55aba0)(panel, manager, loading);
}

static LRESULT CALLBACK controller_window_proc(HWND window, UINT message, WPARAM w, LPARAM l) {
    const WNDPROC original = xinput.window_proc;
    if ((message == WM_ACTIVATEAPP && !w) || message == WM_KILLFOCUS || message == WM_NCDESTROY) {
        ++xinput.focus_epoch;
        xinput.armed = false;
        xinput_stop_rumble();
    }
    if (message == WM_NCDESTROY) {
        xinput.window = nullptr;
        xinput.window_proc = nullptr;
    }
    return CallWindowProcW(original, window, message, w, l);
}

static bool controller_foreground() {
    HWND window = GetForegroundWindow();
    DWORD process{};
    const DWORD thread = GetWindowThreadProcessId(window, &process);
    if (!window || process != GetCurrentProcessId())
        return false;
    // Observe focus loss even when the game stops polling while in background.
    // Subclass only the window on the polling thread, preserving its procedure.
    if (!xinput.window && thread == GetCurrentThreadId()) {
        const auto previous = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(
            window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&controller_window_proc)));
        if (previous) {
            xinput.window = window;
            xinput.window_proc = previous;
        }
    }
    return true;
}

static bool xinput_read(XINPUT_GAMEPAD& pad) {
    if (!xinput.initialized) {
        xinput.initialized = true;
        // Explicit system-directory search; do not load a game-folder proxy.
        for (const wchar_t* name : {L"xinput1_4.dll", L"xinput9_1_0.dll"}) {
            HMODULE module = LoadLibraryExW(name, nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
            if (!module)
                continue;
            xinput.get_state =
                reinterpret_cast<XInputBackend::GetState>(GetProcAddress(module, "XInputGetState"));
            if (xinput.get_state) {
                xinput.set_state = reinterpret_cast<XInputBackend::SetState>(
                    GetProcAddress(module, "XInputSetState"));
                break;
            }
            FreeLibrary(module);
        }
    }
    if (!xinput.get_state)
        return false;
    XINPUT_STATE state{};
    if (xinput.connected) {
        if (xinput.get_state(xinput.slot, &state) == ERROR_SUCCESS) {
            pad = state.Gamepad;
            return true;
        }
        xinput_stop_rumble();
        xinput.connected = false;
        xinput.armed = false;
        xinput.scanned = false;
    }
    const DWORD now = GetTickCount();
    if (xinput.scanned && now - xinput.last_scan < 1000)
        return false;
    xinput.scanned = true;
    xinput.last_scan = now;
    // Prefer the previous slot on reconnect; never switch a connected player.
    for (DWORD offset = 0; offset < XUSER_MAX_COUNT; ++offset) {
        const DWORD slot = (xinput.slot + offset) % XUSER_MAX_COUNT;
        if (xinput.get_state(slot, &state) != ERROR_SUCCESS)
            continue;
        xinput.slot = slot;
        xinput.connected = true;
        xinput.armed = false;
        xinput.sent_valid = false;
        xinput_stop_rumble();
        pad = state.Gamepad;
        return true;
    }
    return false;
}

bool xinput_settings(void* settings) {
    return xinput.claimed && static_cast<Byte*>(xinput.controller) + 0x58 == settings;
}

static bool xinput_rumble_enabled() {
    void* engine = *reinterpret_cast<void**>(0x76c924);
    void* options = engine ? field<void*>(engine, 0xb8) : nullptr;
    return mod_options.rumble && options && field<unsigned>(options, 0xcc) != 0 &&
           !rumble_suspensions && rumble_context.engine &&
           rumble_context == rumble_gameplay_context();
}

static void xinput_expire_rumble() {
    if (!xinput.connected || !xinput.armed || !xinput_rumble_enabled()) {
        xinput_cancel_rumble();
        return;
    }
    const DWORD now = GetTickCount();
    for (unsigned motor = 0; motor < 2; ++motor) {
        if (now - xinput.motor_started[motor] > xinput.motor_duration[motor]) {
            xinput.motors[motor] = 0;
            xinput.motor_duration[motor] = 0;
        }
    }
    xinput_send_rumble();
}

// These are the game's own effects, intensities and durations. Keep both motor
// channels in each XInput write so one effect cannot cancel the other motor.
void __fastcall controller_rumble(void* controller, void*, Byte motor, float amount,
                                  unsigned duration) {
    if (!xinput.claimed || controller != xinput.controller) {
        reinterpret_cast<void(__thiscall*)(void*, Byte, float, unsigned)>(0x6689b0)(
            controller, motor, amount, duration);
        return;
    }
    if (!controller_foreground() || !xinput_rumble_enabled() || !xinput.armed ||
        !xinput.connected) {
        xinput_cancel_rumble();
        return;
    }
    if ((motor != 1 && motor != 2) || !std::isfinite(amount) || amount <= 0 || !duration)
        return;
    if (amount > 1)
        amount = 1;
    if (static_cast<unsigned>(amount * 65535.0f) <= 10000)
        return; // Original game's effect threshold.
    xinput_expire_rumble();
    const unsigned index = motor - 1;
    if (!xinput.motors[index]) {
        xinput.motor_started[index] = GetTickCount();
        xinput.motor_duration[index] = 0;
    }
    const unsigned long long total =
        static_cast<unsigned long long>(xinput.motor_duration[index]) + duration;
    xinput.motor_duration[index] = static_cast<DWORD>(total > 0x7fffffff ? 0x7fffffff : total);
    xinput.motors[index] = static_cast<WORD>(amount * 65335.0f); // Native PC magnitude scale.
    xinput_send_rumble();
}

void __fastcall controller_rumble_update(void* controller, void*) {
    if (xinput.claimed && controller == xinput.controller)
        xinput_expire_rumble();
    else
        reinterpret_cast<void(__thiscall*)(void*)>(0x668aa0)(controller);
}

void __fastcall controller_rumble_stop(void* controller, void*) {
    if (xinput.claimed && controller == xinput.controller) {
        xinput_stop_rumble();
        field<DWORD>(controller, 0x3398) = field<DWORD>(controller, 0x339c) = 0;
        field<DWORD>(controller, 0x33a0) = field<DWORD>(controller, 0x33a4) = 0xffffffff;
    } else
        reinterpret_cast<void(__thiscall*)(void*)>(0x668b70)(controller);
}

static void xinput_claim(void* controller) {
    if (xinput.claimed && xinput.controller == controller)
        return;
    xinput_cancel_rumble();
    xinput.claimed = true;
    xinput.controller = controller;
    xinput.armed = false;
    void* settings = static_cast<Byte*>(controller) + 0x58;
    char* profile = static_cast<char*>(settings) + 0x1d0;
    // A controller connected after startup may have no native DirectInput profile.
    // Give native menu saves the same section used for our initial bindings.
    if (!*profile)
        strcpy_s(profile, 260, "XInput Controller");
    ControllerBindings bindings{};
    // Use the native INI path so setup and the game's binding UI share one file.
    if (!initialize_xinput_bindings(reinterpret_cast<const char*>(0x76ea78), profile, bindings))
        OutputDebugStringA("Jade Empire Controller Fix: could not save controller bindings.\n");
    std::memcpy(static_cast<Byte*>(settings) + 0x2d4, bindings.data(), sizeof bindings);
    field<unsigned>(settings, 0x1cc) = 1;
    // XInput already supplies a defined signed full range. Start at the same
    // 35% dead zone used by the native Xbox filter after calibration.
    for (unsigned axis = 0; axis < 4; ++axis) {
        field<int>(settings, 0x32b8 + axis * 12) = -32768;
        field<int>(settings, 0x32bc + axis * 12) = 32767;
        field<unsigned>(settings, 0x32c0 + axis * 12) = 1;
        field<float>(settings, 0x32e8 + axis * 4) = 0.35f;
    }
}

static bool xinput_neutral(const XINPUT_GAMEPAD& pad) {
    // Below the native joystick activation threshold, including ordinary drift.
    const auto centered = [](short v) { return v >= -6535 && v <= 6535; };
    return !(pad.wButtons & 0xf3ff) && pad.bLeftTrigger <= 30 && pad.bRightTrigger <= 30 &&
           centered(pad.sThumbLX) && centered(pad.sThumbLY) && centered(pad.sThumbRX) &&
           centered(pad.sThumbRY);
}

static void xinput_raw(const XINPUT_GAMEPAD& pad) {
    xinput.raw.fill(0);
    void* raw = xinput.raw.data();
    const auto invert = [](short v) { return v == -32768 ? 32767 : -static_cast<int>(v); };
    field<int>(raw, 0) = pad.sThumbLX;
    field<int>(raw, 4) = invert(pad.sThumbLY);
    field<int>(raw, 12) = mod_options.invert_look_x ? invert(pad.sThumbRX) : pad.sThumbRX;
    field<int>(raw, 16) = mod_options.invert_look_y ? pad.sThumbRY : invert(pad.sThumbRY);
    for (unsigned i = 0; i < 4; ++i)
        field<int>(raw, 32 + i * 4) = -1;
    const int x = ((pad.wButtons & XINPUT_GAMEPAD_DPAD_RIGHT) != 0) -
                  ((pad.wButtons & XINPUT_GAMEPAD_DPAD_LEFT) != 0);
    const int y = ((pad.wButtons & XINPUT_GAMEPAD_DPAD_UP) != 0) -
                  ((pad.wButtons & XINPUT_GAMEPAD_DPAD_DOWN) != 0);
    if (x || y)
        field<int>(raw, 32) = y > 0   ? (x > 0   ? 4500
                                         : x < 0 ? 31500
                                                 : 0)
                              : y < 0 ? (x > 0   ? 13500
                                         : x < 0 ? 22500
                                                 : 18000)
                              : x > 0 ? 9000
                                      : 27000;
    const WORD buttons[]{XINPUT_GAMEPAD_A,
                         XINPUT_GAMEPAD_B,
                         XINPUT_GAMEPAD_X,
                         XINPUT_GAMEPAD_Y,
                         XINPUT_GAMEPAD_LEFT_SHOULDER,
                         XINPUT_GAMEPAD_RIGHT_SHOULDER,
                         XINPUT_GAMEPAD_BACK,
                         XINPUT_GAMEPAD_START,
                         XINPUT_GAMEPAD_LEFT_THUMB,
                         XINPUT_GAMEPAD_RIGHT_THUMB};
    for (unsigned i = 0; i < 10; ++i)
        xinput.raw[48 + i] = (pad.wButtons & buttons[i]) ? 0x80 : 0;
    // Native binding UI sees LT/RT as separate buttons 11/12. The default
    // targeting bindings receive the full analog bytes after the legacy poll.
    xinput.raw[58] = pad.bLeftTrigger > 40 ? 0x80 : 0;
    xinput.raw[59] = pad.bRightTrigger > 40 ? 0x80 : 0;
}

static void __declspec(naked) original_controller_poll() {
    __asm {
        push ebp
        mov ebp,esp
        and esp,0xfffffff8
        push 0x6678e6
        ret
    }
}

unsigned __fastcall controller_poll(void* controller, void*, int slot, unsigned gui) {
    using Poll = unsigned(__thiscall*)(void*, int, unsigned);
    const auto original = reinterpret_cast<Poll>(&original_controller_poll);
    void* engine = *reinterpret_cast<void**>(0x76c924);
    if (!engine || controller != static_cast<Byte*>(engine) + 0x5ac || slot != 0)
        return original(controller, slot, gui);
    XINPUT_GAMEPAD pad{};
    const bool connected = xinput_read(pad);
    if (!connected && !xinput.claimed)
        return original(controller, slot, gui);
    if (connected)
        xinput_claim(controller);
    const bool focused = controller_foreground();
    const bool enabled = field<unsigned>(controller, 0x224) != 0;
    if (!focused || !connected || !enabled || xinput.focus_epoch != xinput.seen_focus_epoch)
        xinput.armed = false;
    xinput.seen_focus_epoch = xinput.focus_epoch;
    if (focused && connected && enabled && !xinput.armed && xinput_neutral(pad))
        xinput.armed = true;
    if (!xinput.armed)
        pad = {};
    const auto context =
        (!gui && xinput.armed && !rumble_suspensions) ? rumble_gameplay_context() : RumbleContext{};
    if (!(rumble_context == context))
        xinput_stop_rumble();
    rumble_context = context;
    xinput_expire_rumble();
    int flick{};
    const bool combat_look = combat_stick_sample(xinput.armed, gui, pad, flick);
    xinput_raw(pad);
    if (combat_look) {
        // Remove only the physical right stick's look contribution, preserving
        // keyboard/mouse processing and the native camera's combat tracking.
        field<int>(xinput.raw.data(), 12) = field<int>(xinput.raw.data(), 16) = 0;
    }
    const unsigned joystick = field<unsigned>(controller, 0x3308);
    void* raw = field<void*>(controller, 0x330c);
    field<unsigned>(controller, 0x3308) = 1;
    field<void*>(controller, 0x330c) = xinput.raw.data();
    // A native 360-profile flag derives trigger buttons from the shared Z axis.
    // Suppress only that translation while our independent sample is consumed.
    Byte& legacy360 = *reinterpret_cast<Byte*>(0x76d738);
    const Byte saved360 = legacy360;
    legacy360 = 0;
    const unsigned result = original(controller, slot, gui);
    legacy360 = saved360;
    field<unsigned>(controller, 0x3308) = joystick;
    field<void*>(controller, 0x330c) = raw;
    if (!focused) {
        // The legacy poll can preserve an old packet on focus loss. Clear it
        // explicitly while retaining the previous packet for release events.
        std::memset(controller, 0, 0x1c);
    } else if (result && xinput.armed) {
        if (field<int>(controller, 0x36c) == 20 && pad.bLeftTrigger > field<Byte>(controller, 0xe))
            field<Byte>(controller, 0xe) = pad.bLeftTrigger;
        if (field<int>(controller, 0x370) == 20 && pad.bRightTrigger > field<Byte>(controller, 0xf))
            field<Byte>(controller, 0xf) = pad.bRightTrigger;
        if (combat_look) {
            combat_stick_apply(controller, flick);
            if (pad.sThumbRX < -11468 || pad.sThumbRX > 11468 || pad.sThumbRY < -11468 ||
                pad.sThumbRY > 11468) {
                field<unsigned>(controller, 0x58 + 0x324) = 1;
                void* services = *reinterpret_cast<void**>(0x76c588);
                void* talk = services ? field<void*>(services, 0x10) : nullptr;
                if (talk)
                    reinterpret_cast<void(__thiscall*)(void*, unsigned)>(0x403180)(talk, 1);
            }
        }
    }
    return result;
}
