#include "xbox_controls.hpp"

#include "control_math.hpp"
#include "controller_glyphs.hpp"
#include "controller_prompts.hpp"
#include "game_memory.hpp"
#include "mod_options.hpp"
#include "xinput_backend.hpp"

#include <cmath>
#include <cstdint>
#include <cstring>

static_assert(sizeof(void*) == 4, "The game requires a 32-bit DLL");

struct OrbitContext {
    void* engine{};
    void* campaign{};
    void* player{};
    void* behaviour{};
    void* camera{};
    void* model{};
    uint64_t area{};
};
static OrbitContext orbit_context;

// A camera can be reset/rebound without changing its address. Invalidate the
// cached heading after the actual native reset, including same-area reloads.
static void __declspec(naked) original_camera_reset() {
    __asm {
        sub esp,0xc
        push esi
        mov esi,ecx
        push 0x41ff76
        ret
    }
}
static void __fastcall camera_reset(void* behaviour, void*) {
    reinterpret_cast<void(__thiscall*)(void*)>(&original_camera_reset)(behaviour);
    if (orbit_context.behaviour == behaviour)
        orbit_context = {};
}

static void* exploration_player(void* expected_behaviour = nullptr) {
    void* engine = *reinterpret_cast<void**>(0x76c924);
    if (!engine) {
        return nullptr;
    }
    if (!controller_controls_active(engine)) {
        orbit_context = {}; // Rebase on return after mouse look or a disconnect.
        return nullptr;
    }
    const unsigned mode = field<unsigned>(engine, 0x38);
    if (mode != 0 && mode != 5) {
        return nullptr;
    }
    void* campaign = field<void*>(engine, 0xa0);
    if (!campaign || field<unsigned>(campaign, 4) != 1) {
        return nullptr;
    }
    void* player = *field<void**>(campaign, 0);
    if (!player || field<unsigned>(player, 0x588) != 0) {
        return nullptr;
    }
    // The same tagged target-ID check used by the original movement function.
    if (field<unsigned short>(player, 0x590) == 1 &&
        field<unsigned>(player, 0x58c) != *reinterpret_cast<unsigned*>(0x75b394)) {
        return nullptr;
    }
    void* manager = field<void*>(campaign, 0x124);
    if (!manager) {
        return nullptr;
    }
    using GetActive = void*(__thiscall*)(void*, int);
    void* behaviour = method<GetActive>(manager, 0x90)(manager, -1);
    using GetType = unsigned(__thiscall*)(void*);
    const unsigned camera_type = behaviour ? method<GetType>(behaviour, 0x10)(behaviour) : 0;
    if (camera_type != 0x106e) {
        return nullptr;
    }
    if (expected_behaviour && behaviour != expected_behaviour) {
        return nullptr;
    }
    return player;
}

static void __fastcall orbit_input(void* behaviour, void*, float horizontal, float vertical) {
    void* player = exploration_player(behaviour);
    using Original = void(__thiscall*)(void*, float, float);
    const auto original = reinterpret_cast<Original>(0x4215a0);
    if (!player) {
        orbit_context = {};
        original(behaviour, horizontal, vertical);
        return;
    }
    void* engine = *reinterpret_cast<void**>(0x76c924);
    void* campaign = field<void*>(engine, 0xa0);
    void* camera = field<void*>(behaviour, 0x3c);
    void* model = field<void*>(behaviour, 0x38);
    if (!camera || !model) {
        orbit_context = {};
        original(behaviour, horizontal, vertical);
        return;
    }
    const uint64_t area = field<uint64_t>(campaign, 0x11c);
    const bool changed = engine != orbit_context.engine || campaign != orbit_context.campaign ||
                         player != orbit_context.player || behaviour != orbit_context.behaviour ||
                         camera != orbit_context.camera || model != orbit_context.model ||
                         area != orbit_context.area;
    if (changed || !std::isfinite(field<float>(behaviour, 0xc0)) ||
        !std::isfinite(field<float>(behaviour, 0xa0)) ||
        !std::isfinite(field<float>(behaviour, 0xc4))) {
        // The behaviour stores a camera interface, which can point inside the
        // object. Its virtual getter adjusts the pointer before reading rotation.
        Quaternion rotation{};
        using Rotation = Quaternion*(__thiscall*)(void*, Quaternion*);
        method<Rotation>(camera, 0xb0)(camera, &rotation);
        Vec3 forward = camera_forward(rotation);
        float planar = std::hypot(forward.x, forward.y);
        // A transition may leave a vertical or not-yet-oriented camera. Do not
        // permanently refuse orbit initialization: use the player's spawn heading.
        if (!std::isfinite(planar) || planar < 0.001f) {
            forward = field<Vec3>(player, 0x50);
            planar = std::hypot(forward.x, forward.y);
        }
        field<float>(behaviour, 0xc0) = std::isfinite(planar) && planar >= 0.001f
                                            ? std::atan2(-forward.x, forward.y) * 57.2957795f
                                            : 0.0f;
        // 0x422B90 resets against the old render object BEFORE rebinding it.
        // Rebase the native ground-height fallback once the new object is active.
        Vec3 position{};
        using Position = Vec3*(__thiscall*)(void*, Vec3*);
        method<Position>(model, 0xac)(model, &position);
        if (std::isfinite(position.z)) {
            field<float>(behaviour, 0xc4) = position.z;
            field<float>(behaviour, 0xc8) = position.z;
        }
        field<float>(behaviour, 0xb4) = field<float>(behaviour, 0xb8) = 0;
        float& pitch = field<float>(behaviour, 0xa0);
        // Same legal range as native 0x41FF70/0x4200E0, including NaN recovery.
        if (!std::isfinite(pitch) || pitch < 82)
            pitch = 82;
        else if (pitch > 102.5f)
            pitch = 102.5f;
        orbit_context = {engine, campaign, player, behaviour, camera, model, area};
    }
    if (!std::isfinite(horizontal))
        horizontal = 0;
    if (!std::isfinite(vertical))
        vertical = 0;
    // Preserve the native independent orbit, including its persistent yaw.
    const unsigned saved = field<unsigned>(player, 0x994);
    field<unsigned>(player, 0x994) = 1;
    original(behaviour, horizontal, vertical);
    field<unsigned>(player, 0x994) = saved;
}

// With look input centred, the orbit camera update asks for the interaction
// target at 0x4225A9, steers toward it with a +/-1 horizontal input, then calls
// 0x421AD0, which zeroes the yaw at +0xC0. Independent orbit keeps an absolute
// heading there, so that recenter jumps the camera and restarts the steering in
// a loop. Report no target to this call site only; 0x461950 has many other
// callers, and target selection, labels and interaction are unchanged.
static void* __fastcall orbit_focus_target(void* campaign, void*, void* output) {
    using Target = void*(__thiscall*)(void*, void*);
    void* target = reinterpret_cast<Target>(0x461950)(campaign, output);
    return target && exploration_player() ? nullptr : target;
}

// The independent-orbit path at 0x42111F skips collision resolution. Pass its
// goal through the game's collision solver (0x420730) after this frame's target
// pivot has been updated, without changing orbit input.
static Vec3* __stdcall orbit_position(void* behaviour, Vec3* output) {
    Vec3* goal = &field<Vec3>(behaviour, 0x100);
    if (!exploration_player(behaviour) || !field<void*>(behaviour, 0x38) ||
        !field<void*>(behaviour, 0x3c))
        return goal;
    using Resolve = Vec3*(__thiscall*)(void*, Vec3*, const Vec3*);
    return reinterpret_cast<Resolve>(0x420730)(behaviour, output, goal);
}

// Replace only the six-byte LEA at 0x42111F. The +0x28 caller local is no
// longer used on this branch; keep +0x100 as the unblocked orbit goal so
// the camera can extend again when the obstacle clears.
static void __declspec(naked) orbit_position_bridge() {
    __asm {
        push ecx
        push edx
        lea eax, [esp+0x30]
        push eax
        push esi
        call orbit_position
        pop edx
        pop ecx
        push 0x421125
        ret
    }
}

static void __fastcall movement_input(void* move, void*, Vec3* velocity) {
    void* player = exploration_player();
    if (player && field<void*>(move, 0x90) == player) {
        void* control = field<void*>(player, 0x7ec);
        void* camera = control ? field<void*>(control, 8) : nullptr;
        if (camera && field<unsigned>(control, 0) == 0x6e86e8) {
            Movement result{};
            if (plan_movement(field<Quaternion>(camera, 0x88), field<Vec3>(player, 0x50), *velocity,
                              field<float>(control, 0x10), field<float>(control, 0x14), result)) {
                field<Vec3>(move, 0x78) = result.facing;
                field<Vec3>(move, 0x94) = result.local;
                *velocity = result.velocity;
            }
        }
    }
    // Keep the game's collision, movement-speed and animation pipeline.
    reinterpret_cast<void(__thiscall*)(void*, Vec3*)>(0x45c720)(move, velocity);
}

static void __fastcall movement_animation(void* move, void*, Vec3 facing, Vec3 velocity,
                                          float* angle, unsigned* direction) {
    void* player = exploration_player();
    if (player && field<void*>(move, 0x90) == player) {
        // Use forward movement outside combat so the strafing branch at
        // 0x45CD3E cannot skip the character's facing update.
        if (angle)
            *angle = 0;
        if (direction)
            *direction = 0;
        return;
    }
    using Original = void(__thiscall*)(void*, Vec3, Vec3, float*, unsigned*);
    reinterpret_cast<Original>(0x45c190)(move, facing, velocity, angle, direction);
}

static unsigned __fastcall movement_turn(void* move, void*) {
    void* player = exploration_player();
    if (player && field<void*>(move, 0x90) == player) {
        // Let 0x45E070 interpolate facing. Scripted PC turn animations return
        // early from 0x45E300 before that update can run on a large reversal.
        return 0;
    }
    return reinterpret_cast<unsigned(__thiscall*)(void*)>(0x45c3a0)(move);
}

// This wrapper is installed only at 0x45E69F inside 0x45E300. Its output
// points to that caller's +0x74 local; the PC-only render direction is +0x58.
// Use the interpolated facing for both movement and the rendered model.
static Vec3* __fastcall movement_facing(void* context, void*, Vec3* output, void* move) {
    using Original = Vec3*(__thiscall*)(void*, Vec3*, void*);
    Vec3* result = reinterpret_cast<Original>(0x45e070)(context, output, move);
    void* player = exploration_player();
    if (player && field<void*>(move, 0x90) == player) {
        *reinterpret_cast<Vec3*>(reinterpret_cast<Byte*>(output) - 0x1c) = *result;
    }
    return result;
}

static void __fastcall controller_sample(void* settings, void*, void* state) {
    reinterpret_cast<void(__thiscall*)(void*, void*)>(0x5946b0)(settings, state);
    // After each axis reaches both ends of its range, lower its dead zone from
    // 0.60 to 0.35. The game retains the calibration fields but never updates
    // them. Settings begins at controller +0x58.
    for (unsigned axis = 0; axis < 4; ++axis) {
        const unsigned binding = field<unsigned>(settings, 0x2d4 + axis * 4);
        if (binding > 0x14 || binding % 4)
            continue; // Analog DIJOYSTATE axes only.
        const size_t calibration = 0x32b8 + axis * 12;
        if (field<unsigned>(settings, calibration + 8))
            continue;
        const int value = field<int>(state, binding);
        if (value < -32768 || value > 32767)
            continue;
        int& minimum = field<int>(settings, calibration);
        int& maximum = field<int>(settings, calibration + 4);
        if (value < minimum)
            minimum = value;
        if (value > maximum)
            maximum = value;
        if (maximum >= 26214 && minimum <= -26215) {
            field<float>(settings, 0x32e8 + axis * 4) = 0.35f;
            field<unsigned>(settings, calibration + 8) = 1;
        }
    }
}

// Only the GUI collector uses these call sites. Feed the D-pad through the
// same canonical left-stick axes used by arrow keys, preserving native menu
// press/hold/release handling and leaving gameplay style selection untouched.
static unsigned __fastcall menu_controller(void* controller, void*, int slot, unsigned gui) {
    using Poll = unsigned(__thiscall*)(void*, int, unsigned);
    const unsigned result = reinterpret_cast<Poll>(0x6678e0)(controller, slot, gui);
    if (result && gui) {
        const unsigned buttons = field<unsigned>(controller, 4);
        const int horizontal = ((buttons & 8) != 0) - ((buttons & 4) != 0);
        const int vertical = ((buttons & 1) != 0) - ((buttons & 2) != 0);
        if (horizontal)
            field<short>(controller, 0x10) = static_cast<short>(horizontal * 32767);
        if (vertical)
            field<short>(controller, 0x12) = static_cast<short>(vertical * 32767);
    }
    return result;
}

struct Patch {
    uintptr_t address;
    Byte original[9];
    void* replacement;
    unsigned size = 5;
    Byte opcode = 0xe8;
};
static Patch patches[] = {
    {0x40be70,
     {0x56, 0x8b, 0xf1, 0x83, 0x7e, 0x48, 0x00},
     reinterpret_cast<void*>(&controller_game_pause),
     7,
     0xe9},
    {0x4094c0,
     {0x56, 0x8b, 0xf1, 0x8b, 0x86, 0xa0, 0x00, 0x00, 0x00},
     reinterpret_cast<void*>(&controller_combat_pause),
     9,
     0xe9},
    {0x40a480,
     {0x56, 0x57, 0x8b, 0x7c, 0x24, 0x0c},
     reinterpret_cast<void*>(&controller_input_mode),
     6,
     0xe9},
    {0x566433, {0xe8, 0x68, 0x47, 0xff, 0xff}, reinterpret_cast<void*>(&controller_loading_screen)},
    {0x41ff70,
     {0x83, 0xec, 0x0c, 0x56, 0x8b, 0xf1},
     reinterpret_cast<void*>(&camera_reset),
     6,
     0xe9},
    {0x6678e0,
     {0x55, 0x8b, 0xec, 0x83, 0xe4, 0xf8},
     reinterpret_cast<void*>(&controller_poll),
     6,
     0xe9},
    {0x6667a6, {0xe8, 0x05, 0x22, 0x00, 0x00}, reinterpret_cast<void*>(&controller_rumble)},
    {0x6667e2, {0xe8, 0xc9, 0x21, 0x00, 0x00}, reinterpret_cast<void*>(&controller_rumble)},
    {0x6666ca, {0xe8, 0xd1, 0x23, 0x00, 0x00}, reinterpret_cast<void*>(&controller_rumble_update)},
    {0x6666fc, {0xe8, 0x9f, 0x23, 0x00, 0x00}, reinterpret_cast<void*>(&controller_rumble_update)},
    {0x6663e8, {0xe8, 0x83, 0x27, 0x00, 0x00}, reinterpret_cast<void*>(&controller_rumble_stop)},
    {0x4735d0,
     {0x6a, 0xff, 0x68, 0x08, 0x51, 0x6c, 0x00},
     reinterpret_cast<void*>(&prompt_token),
     7,
     0xe9},
    {0x65a180,
     {0x6a, 0xff, 0x68, 0xb8, 0xcd, 0x6d, 0x00},
     reinterpret_cast<void*>(&prompt_text),
     7,
     0xe9},
    {0x646281, {0xe8, 0x7a, 0xec, 0xff, 0xff}, reinterpret_cast<void*>(&glyph_upload)},
    {0x667480, {0xe8, 0x5b, 0x04, 0x00, 0x00}, reinterpret_cast<void*>(&menu_controller)},
    {0x667561, {0xe8, 0x7a, 0x03, 0x00, 0x00}, reinterpret_cast<void*>(&menu_controller)},
    {0x422757, {0xe8, 0x44, 0xee, 0xff, 0xff}, reinterpret_cast<void*>(&orbit_input)},
    {0x4225a9, {0xe8, 0xa2, 0xf3, 0x03, 0x00}, reinterpret_cast<void*>(&orbit_focus_target)},
    {0x42111f,
     {0x8d, 0x86, 0x00, 0x01, 0x00, 0x00},
     reinterpret_cast<void*>(&orbit_position_bridge),
     6,
     0xe9},
    {0x4649cc, {0xe8, 0x4f, 0x7d, 0xff, 0xff}, reinterpret_cast<void*>(&movement_input)},
    {0x45cc12, {0xe8, 0x79, 0xf5, 0xff, 0xff}, reinterpret_cast<void*>(&movement_animation)},
    {0x45ce20, {0xe8, 0x7b, 0xf5, 0xff, 0xff}, reinterpret_cast<void*>(&movement_turn)},
    {0x45e69f, {0xe8, 0xcc, 0xf9, 0xff, 0xff}, reinterpret_cast<void*>(&movement_facing)},
    {0x668455, {0xe8, 0x56, 0xc2, 0xf2, 0xff}, reinterpret_cast<void*>(&controller_sample)}};
static constexpr unsigned patch_count = sizeof patches / sizeof patches[0];

// The proxy installs synchronously during native input construction, before
// gameplay can execute any of the instructions being replaced.
DWORD install_controller_hooks() {
    const Byte movement_signature[] = {0x6a, 0xff, 0x64, 0xa1, 0,    0, 0,
                                       0,    0x68, 0x8b, 0x46, 0x6c, 0};
    const Byte camera_signature[] = {0x8b, 0x15, 0x24, 0xc9, 0x76, 0, 0x83, 0xec, 0x38};
    if (std::memcmp(reinterpret_cast<void*>(0x463570), movement_signature,
                    sizeof movement_signature) ||
        std::memcmp(reinterpret_cast<void*>(0x4223b0), camera_signature, sizeof camera_signature))
        return ERROR_REVISION_MISMATCH;
    void* engine = *reinterpret_cast<void**>(0x76c924);
    void* campaign = engine ? field<void*>(engine, 0xa0) : nullptr;
    if (campaign && field<unsigned>(campaign, 4))
        return ERROR_BUSY;
    for (const auto& patch : patches)
        if (std::memcmp(reinterpret_cast<void*>(patch.address), patch.original, patch.size))
            return ERROR_REVISION_MISMATCH;
    load_mod_options();
    load_controller_glyphs();
    DWORD protections[patch_count]{};
    for (unsigned i = 0; i < patch_count; ++i) {
        if (!VirtualProtect(reinterpret_cast<void*>(patches[i].address), patches[i].size,
                            PAGE_EXECUTE_READWRITE, &protections[i])) {
            const DWORD error = GetLastError();
            for (unsigned j = i; j-- > 0;) {
                DWORD ignored;
                VirtualProtect(reinterpret_cast<void*>(patches[j].address), patches[j].size,
                               protections[j], &ignored);
            }
            return error;
        }
    }
    for (const auto& patch : patches) {
        const auto relative = static_cast<int32_t>(reinterpret_cast<uintptr_t>(patch.replacement) -
                                                   patch.address - 5);
        Byte instruction[9]{patch.opcode, 0, 0, 0, 0, 0x90, 0x90, 0x90, 0x90};
        std::memcpy(instruction + 1, &relative, 4);
        std::memcpy(reinterpret_cast<void*>(patch.address), instruction, patch.size);
    }
    DWORD failure = ERROR_SUCCESS;
    // Unwind in reverse order: multiple calls can share the same memory page.
    for (unsigned i = patch_count; i-- > 0;) {
        DWORD ignored;
        if (!VirtualProtect(reinterpret_cast<void*>(patches[i].address), patches[i].size,
                            protections[i], &ignored))
            failure = GetLastError();
    }
    if (!FlushInstructionCache(GetCurrentProcess(), nullptr, 0))
        failure = GetLastError();
    if (failure != ERROR_SUCCESS) {
        // Gameplay is not running yet: undo all calls before reporting failure.
        for (unsigned i = patch_count; i-- > 0;) {
            DWORD ignored;
            if (VirtualProtect(reinterpret_cast<void*>(patches[i].address), patches[i].size,
                               PAGE_EXECUTE_READWRITE, &ignored)) {
                std::memcpy(reinterpret_cast<void*>(patches[i].address), patches[i].original,
                            patches[i].size);
                VirtualProtect(reinterpret_cast<void*>(patches[i].address), patches[i].size,
                               protections[i], &ignored);
            }
        }
        FlushInstructionCache(GetCurrentProcess(), nullptr, 0);
    }
    return failure;
}

BOOL WINAPI DllMain(HINSTANCE, DWORD, void*) {
    return TRUE;
}
