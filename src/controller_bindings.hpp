#pragma once

#include <array>

using ControllerBindings = std::array<int, 20>;

// Initialise each native INI profile once, then load its saved user bindings.
// Always supplies usable bindings; false means they could not all be persisted.
bool initialize_xinput_bindings(const char* ini_path, const char* profile,
                                ControllerBindings& bindings);
