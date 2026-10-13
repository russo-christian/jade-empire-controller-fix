#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace glyph_assets {
using Bytes = std::vector<unsigned char>;
using Pixels = std::array<uint32_t, 400>;
inline constexpr const char* names[]{
    "ui_button_a",     "ui_button_b",    "ui_button_x",     "ui_button_y",  "ui_button_white",
    "ui_button_black", "ui_button_back", "ui_button_start", "ui_button_la", "ui_button_ra",
    "ui_button_dpad",  "ui_lbutton",     "ui_rbutton"};
extern std::array<Pixels, 13> pixels;
extern bool ready;
struct RGBA {
    unsigned r{}, g{}, b{}, a = 255;
};

// Read only the requested textures; valid loose overrides take precedence.
void load(const std::filesystem::path& root);
bool relative_archive(std::string name, std::filesystem::path& result);
bool decode(const Bytes& txb, std::vector<RGBA>& output, unsigned& width, unsigned& height);
bool rasterize(const Bytes& txb, Pixels& result);
} // namespace glyph_assets
