#pragma once

#include <windows.h>

// Install only during input construction, before gameplay can use the hooks.
DWORD install_controller_hooks();
