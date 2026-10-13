#pragma once

#include <windows.h>
#include <xinput.h>

// Returns whether combat consumes look input; flick is -1, 0 or +1.
bool combat_stick_sample(bool enabled, unsigned gui, const XINPUT_GAMEPAD& pad, int& flick);
void combat_stick_apply(void* controller, int flick);
