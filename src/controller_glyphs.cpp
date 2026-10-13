#include "controller_glyphs.hpp"

#include "game_memory.hpp"
#include "glyph_assets.hpp"
#include "mod_options.hpp"

#include <cstring>
#include <iterator>
#include <windows.h>

void load_controller_glyphs() {
    if (!mod_options.controller_prompts)
        return;
    HMODULE module{};
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(&glyph_assets::ready), &module))
        return;
    wchar_t path[32768]{};
    const DWORD size = GetModuleFileNameW(module, path, 32768);
    if (!size || size >= 32768)
        return;
    try {
        glyph_assets::load(std::filesystem::path(path).parent_path());
    } catch (...) {
        glyph_assets::ready = false;
    }
}

// The native completion has read a glyph's ABC metrics and bitmap, then calls
// 644F00 to upload it and release its I/O request. Keep that lifetime and LRU
// behavior intact, replacing only our private-use cache cells after completion.
void __fastcall glyph_upload(void* request, void*) {
    const unsigned code = field<unsigned short>(request, 0x18);
    void* cache = field<void*>(request, 0x1c);
    void* record = nullptr;
    if (glyph_assets::ready && cache && code >= 0xe000 && code < 0xe000 + std::size(glyphs)) {
        void* node = nullptr;
        using Find = void*(__thiscall*)(void*, void**, const void*);
        reinterpret_cast<Find>(0x643a90)(static_cast<Byte*>(cache) + 0x2c, &node,
                                         static_cast<Byte*>(request) + 0x18);
        if (node && node != field<void*>(cache, 0x34))
            record = static_cast<Byte*>(node) + 0xc;
    }
    reinterpret_cast<void(__thiscall*)(void*)>(0x644f00)(request);
    if (!record || field<unsigned>(cache, 0x14) < 22 || field<unsigned>(cache, 0x18) < 22)
        return;
    void* texture = field<void*>(cache, 0x10);
    if (!texture)
        return;
    void** table = field<void**>(texture, 0);
    struct Locked {
        int pitch;
        Byte* pixels;
    } locked{};
    using Lock = HRESULT(__stdcall*)(void*, unsigned, Locked*, const RECT*, unsigned);
    using Unlock = HRESULT(__stdcall*)(void*, unsigned);
    if (FAILED(reinterpret_cast<Lock>(table[19])(texture, 0, &locked, nullptr, 0)))
        return;
    const unsigned y = field<unsigned short>(record, 6), x = field<unsigned short>(record, 8);
    // Whole cells were already cleared/filled by the native upload. Clear them
    // again to remove its missing-character placeholder; leave neighboring cells.
    for (unsigned row = 0; row < field<unsigned>(cache, 0x18); ++row)
        std::memset(locked.pixels + (y + row) * locked.pitch + x * 4, 0,
                    field<unsigned>(cache, 0x14) * 4);
    for (unsigned row = 0; row < 20; ++row)
        std::memcpy(locked.pixels + (y + row + 1) * locked.pitch + x * 4,
                    glyph_assets::pixels[code - 0xe000].data() + row * 20, 20 * sizeof(unsigned));
    reinterpret_cast<Unlock>(table[20])(texture, 0);
    // A/B/C metrics: zero leading bearing, 20 pixels of artwork, two of space.
    field<Byte>(record, 0xa) = 0;
    field<Byte>(record, 0xb) = 20;
    field<Byte>(record, 0xc) = 2;
}
