#include "combat_stick.hpp"

#include "game_memory.hpp"
#include "mod_options.hpp"

// Right-stick target switching is an addition to the native target actions.
// Each deliberate horizontal flick produces one press, then requires center.
// Keep native target selection/validation and retain physical trigger controls.
struct CombatStick {
    void* player{};
    void* campaign{};
    uint64_t area{};
    bool ready{};
};
static CombatStick combat_stick;

static void* locked_combat_player(void*& campaign) {
    void* engine = *reinterpret_cast<void**>(0x76c924);
    if (!engine || !*reinterpret_cast<unsigned*>(0x75ecdc) ||
        (field<unsigned>(engine, 0x38) != 0 && field<unsigned>(engine, 0x38) != 5))
        return nullptr;
    campaign = field<void*>(engine, 0xa0);
    if (!campaign || field<unsigned>(campaign, 4) != 1 || !field<void*>(campaign, 0))
        return nullptr;
    void* player = *field<void**>(campaign, 0);
    if (!player || !field<unsigned>(player, 0x588) || field<unsigned short>(player, 0x590) != 1 ||
        field<unsigned>(player, 0x58c) == *reinterpret_cast<unsigned*>(0x75b394))
        return nullptr;
    return player;
}

bool combat_stick_sample(bool enabled, unsigned gui, const XINPUT_GAMEPAD& pad, int& flick) {
    flick = 0;
    void* campaign{};
    void* player = mod_options.right_stick_target_switching && enabled && !gui
                       ? locked_combat_player(campaign)
                       : nullptr;
    if (!player) {
        combat_stick = {};
        return false;
    }
    const uint64_t area = field<uint64_t>(campaign, 0x11c);
    if (player != combat_stick.player || campaign != combat_stick.campaign ||
        area != combat_stick.area)
        combat_stick = {player, campaign, area, false};
    const int x = pad.sThumbRX, y = pad.sThumbRY;
    const int ax = x < 0 ? -x : x, ay = y < 0 ? -y : y;
    if (ax <= 11468 && ay <= 11468)
        combat_stick.ready = true;
    else if (combat_stick.ready && ax >= 21299 && ax > ay) {
        combat_stick.ready = false;
        flick = x < 0 ? -1 : 1;
    }
    return true;
}

void combat_stick_apply(void* controller, int flick) {
    // Do not combine a flick with an independently held target action: that
    // would create the native both-trigger Free Target command accidentally.
    if (flick && !field<Byte>(controller, 0xe) && !field<Byte>(controller, 0xf))
        field<Byte>(controller, flick < 0 ? 0xe : 0xf) = 255;
}
