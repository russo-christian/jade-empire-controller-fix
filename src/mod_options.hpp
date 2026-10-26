#pragma once

struct ModOptions {
    bool right_stick_target_switching = true;
    bool controller_only = true;
    bool controller_prompts = true;
    bool rumble = true;
    bool invert_look_x = false;
    bool invert_look_y = false;
};
extern ModOptions mod_options;

// Read settings beside the DLL once, before hooks are installed.
void load_mod_options();
