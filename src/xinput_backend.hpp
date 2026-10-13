#pragma once

bool controller_controls_active(void* engine);
bool xinput_settings(void* settings);
unsigned __fastcall controller_poll(void* controller, void*, int slot, unsigned gui);
void __fastcall controller_game_pause(void* engine, void*, unsigned paused, unsigned display);
void __fastcall controller_combat_pause(void* engine, void*, unsigned paused, unsigned display);
void __fastcall controller_input_mode(void* engine, void*, unsigned mode);
void* __fastcall controller_loading_screen(void* panel, void*, void* manager, unsigned loading);
void __fastcall controller_rumble(void* controller, void*, unsigned char motor, float amount,
                                  unsigned duration);
void __fastcall controller_rumble_update(void* controller, void*);
void __fastcall controller_rumble_stop(void* controller, void*);
