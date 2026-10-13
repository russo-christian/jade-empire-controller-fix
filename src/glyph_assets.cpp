#include "glyph_assets.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iterator>
#include <windows.h>

namespace glyph_assets {
std::array<Pixels, 13> pixels{};
bool ready = false;

static uint16_t u16(const unsigned char* p) {
    return static_cast<uint16_t>(p[0] | (p[1] << 8));
}
static uint32_t u32(const unsigned char* p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}
struct File {
    HANDLE handle = INVALID_HANDLE_VALUE;
    uint64_t size{};
    explicit File(const std::filesystem::path& path) {
        handle = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                             nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        LARGE_INTEGER length{};
        if (handle != INVALID_HANDLE_VALUE && GetFileSizeEx(handle, &length) &&
            length.QuadPart >= 0)
            size = static_cast<uint64_t>(length.QuadPart);
    }
    ~File() {
        if (handle != INVALID_HANDLE_VALUE)
            CloseHandle(handle);
    }
    File(const File&) = delete;
    File& operator=(const File&) = delete;
    bool read(uint64_t offset, void* output, DWORD count) const {
        if (offset > size || count > size - offset)
            return false;
        LARGE_INTEGER position{};
        position.QuadPart = static_cast<LONGLONG>(offset);
        DWORD got{};
        return SetFilePointerEx(handle, position, nullptr, FILE_BEGIN) &&
               ReadFile(handle, output, count, &got, nullptr) && got == count;
    }
};
static bool texture(const File& file, uint64_t offset, uint32_t length, Bytes& output) {
    if (length < 128 || length > 2 * 1024 * 1024)
        return false;
    output.resize(length);
    if (file.read(offset, output.data(), length))
        return true;
    output.clear();
    return false;
}

// KEY filenames are relative paths. Reject traversal/absolute paths even in a
// damaged or modified index; only files beneath this installation are needed.
bool relative_archive(std::string name, std::filesystem::path& result) {
    if (name.empty() || name.size() > 260 || name.find(':') != std::string::npos)
        return false;
    std::replace(name.begin(), name.end(), '\\', '/');
    result = std::filesystem::path(name);
    if (result.is_absolute() || result.has_root_name() || result.has_root_directory())
        return false;
    for (const auto& component : result)
        if (component == "..")
            return false;
    return true;
}
static bool index_textures(const std::filesystem::path& root, std::array<Bytes, 13>& textures) {
    File keyfile(root / L"chitin.key");
    if (keyfile.size < 24 || keyfile.size > 16 * 1024 * 1024)
        return false;
    Bytes key(static_cast<size_t>(keyfile.size));
    if (!keyfile.read(0, key.data(), static_cast<DWORD>(key.size())) ||
        std::memcmp(key.data(), "KEY V1  ", 8))
        return false;
    const uint32_t files = u32(&key[8]), resources = u32(&key[12]), fo = u32(&key[16]),
                   ro = u32(&key[20]);
    if (files > 1024 || resources > 200000 || uint64_t(fo) + uint64_t(files) * 12 > key.size() ||
        uint64_t(ro) + uint64_t(resources) * 22 > key.size())
        return false;
    for (uint32_t r = 0; r < resources; ++r) {
        const unsigned char* entry = &key[ro + r * 22];
        if (u16(entry + 16) != 3017)
            continue;
        char name[17]{};
        std::memcpy(name, entry, 16);
        size_t glyph = 0;
        while (glyph < std::size(names) && _stricmp(name, names[glyph]))
            ++glyph;
        if (glyph == std::size(names))
            continue;
        const uint32_t id = u32(entry + 18), archive = id >> 20, index = id & 0xfffff;
        if (archive >= files)
            continue;
        const unsigned char* fileentry = &key[fo + archive * 12];
        const uint32_t offset = u32(fileentry + 4), length = u16(fileentry + 8);
        if (!length || length > 260 || uint64_t(offset) + length > key.size())
            continue;
        std::string filename(reinterpret_cast<const char*>(&key[offset]), length);
        while (!filename.empty() && !filename.back())
            filename.pop_back();
        if (filename.find('\0') != std::string::npos)
            continue;
        std::filesystem::path relative;
        if (!relative_archive(filename, relative))
            continue;
        File bif(root / relative);
        unsigned char header[20]{}, item[16]{};
        if (!bif.read(0, header, sizeof header) || std::memcmp(header, "BIFFV1  ", 8) ||
            index >= u32(header + 8))
            continue;
        if (!bif.read(uint64_t(u32(header + 16)) + uint64_t(index) * 16, item, sizeof item) ||
            u32(item + 12) != 3017)
            continue;
        texture(bif, u32(item + 4), u32(item + 8), textures[glyph]);
    }
    return true;
}

static RGBA rgb565(uint16_t c) {
    const unsigned r = (c >> 11) & 31, g = (c >> 5) & 63, b = c & 31;
    return {(r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2), 255};
}
bool decode(const Bytes& txb, std::vector<RGBA>& output, unsigned& width, unsigned& height) {
    if (txb.size() < 128)
        return false;
    width = u16(&txb[8]);
    height = u16(&txb[10]);
    if (!width || !height || width > 1024 || height > 1024 || (txb[12] != 10 && txb[12] != 12))
        return false;
    const bool dxt5 = txb[12] == 12;
    const unsigned stride = dxt5 ? 16u : 8u, bx = (width + 3) / 4, by = (height + 3) / 4;
    const size_t required = static_cast<size_t>(bx) * by * stride;
    if (required > txb.size() - 128 || u32(txb.data()) < required ||
        u32(txb.data()) > txb.size() - 128)
        return false;
    output.resize(static_cast<size_t>(width) * height);
    for (unsigned yb = 0; yb < by; ++yb)
        for (unsigned xb = 0; xb < bx; ++xb) {
            const unsigned char* block = txb.data() + 128 + (yb * bx + xb) * stride;
            const unsigned char* color = block + (dxt5 ? 8 : 0);
            const uint16_t c0 = u16(color), c1 = u16(color + 2);
            RGBA palette[4]{rgb565(c0), rgb565(c1), {}, {}};
            const unsigned divisor = dxt5 || c0 > c1 ? 3u : 2u;
            for (unsigned channel = 0; channel < 3; ++channel) {
                const unsigned a = channel == 0   ? palette[0].r
                                   : channel == 1 ? palette[0].g
                                                  : palette[0].b;
                const unsigned b = channel == 0   ? palette[1].r
                                   : channel == 1 ? palette[1].g
                                                  : palette[1].b;
                const unsigned p = ((divisor - 1) * a + b) / divisor,
                               q = divisor == 3 ? (a + 2 * b) / 3 : 0;
                if (!channel) {
                    palette[2].r = p;
                    palette[3].r = q;
                } else if (channel == 1) {
                    palette[2].g = p;
                    palette[3].g = q;
                } else {
                    palette[2].b = p;
                    palette[3].b = q;
                }
            }
            if (divisor == 2)
                palette[3].a = 0;
            unsigned alpha[8]{block[0], block[1]};
            uint64_t alpha_bits{};
            if (dxt5) {
                for (unsigned i = 0; i < 6; ++i)
                    alpha_bits |= uint64_t(block[2 + i]) << (i * 8);
                if (alpha[0] > alpha[1])
                    for (unsigned i = 2; i < 8; ++i)
                        alpha[i] = ((8 - i) * alpha[0] + (i - 1) * alpha[1]) / 7;
                else {
                    for (unsigned i = 2; i < 6; ++i)
                        alpha[i] = ((6 - i) * alpha[0] + (i - 1) * alpha[1]) / 5;
                    alpha[6] = 0;
                    alpha[7] = 255;
                }
            }
            const uint32_t bits = u32(color + 4);
            for (unsigned i = 0; i < 16; ++i) {
                const unsigned x = xb * 4 + i % 4, y = yb * 4 + i / 4;
                if (x >= width || y >= height)
                    continue;
                RGBA pixel = palette[(bits >> (i * 2)) & 3];
                if (dxt5)
                    pixel.a = alpha[(alpha_bits >> (i * 3)) & 7];
                output[(height - 1 - y) * width + x] = pixel; // TXB rows are bottom-up.
            }
        }
    return true;
}
static double lanczos(double x) {
    x = std::abs(x);
    if (x < 1e-10)
        return 1;
    if (x >= 3)
        return 0;
    constexpr double pi = 3.14159265358979323846;
    return std::sin(pi * x) * std::sin(pi * x / 3) / (pi * pi * x * x / 3);
}
static unsigned rounded(double x) {
    return static_cast<unsigned>((std::max)(0.0, (std::min)(255.0, std::floor(x + 0.5))));
}
bool rasterize(const Bytes& txb, Pixels& result) {
    unsigned width{}, height{};
    std::vector<RGBA> decoded;
    if (!decode(txb, decoded, width, height))
        return false;
    // Separable Lanczos in premultiplied color avoids dark/colored fringes at
    // transparent edges. Round and clamp each filtering pass.
    using Sample = std::array<unsigned, 4>;
    std::vector<Sample> horizontal(static_cast<size_t>(height) * 20);
    const double sx = width / 20.0, sy = height / 20.0;
    for (unsigned y = 0; y < height; ++y)
        for (unsigned x = 0; x < 20; ++x) {
            double total = 0, channels[4]{};
            for (unsigned ix = 0; ix < width; ++ix) {
                const double w = lanczos((ix + 0.5 - (x + 0.5) * sx) / (std::max)(1.0, sx));
                const RGBA p = decoded[y * width + ix];
                const unsigned values[]{(p.r * p.a + 127) / 255, (p.g * p.a + 127) / 255,
                                        (p.b * p.a + 127) / 255, p.a};
                for (unsigned c = 0; c < 4; ++c)
                    channels[c] += w * values[c];
                total += w;
            }
            for (unsigned c = 0; c < 4; ++c)
                horizontal[y * 20 + x][c] = rounded(channels[c] / total);
        }
    for (unsigned y = 0; y < 20; ++y)
        for (unsigned x = 0; x < 20; ++x) {
            double total = 0, channels[4]{};
            for (unsigned iy = 0; iy < height; ++iy) {
                const double w = lanczos((iy + 0.5 - (y + 0.5) * sy) / (std::max)(1.0, sy));
                for (unsigned c = 0; c < 4; ++c)
                    channels[c] += w * horizontal[iy * 20 + x][c];
                total += w;
            }
            const unsigned a = rounded(channels[3] / total);
            unsigned rgb[3]{};
            for (unsigned c = 0; c < 3; ++c) {
                // The font cache requires premultiplied ARGB bytes.
                const unsigned p = rounded(channels[c] / total);
                rgb[c] = a ? (std::min)(255u, p * 255 / a) * a / 255 : 0;
            }
            result[y * 20 + x] = (a << 24) | (rgb[0] << 16) | (rgb[1] << 8) | rgb[2];
        }
    return true;
}
void load(const std::filesystem::path& root) {
    ready = false;
    pixels = {};
    unsigned loaded = 0;
    std::array<Bytes, 13> textures{};
    index_textures(root, textures);
    for (size_t i = 0; i < std::size(names); ++i) {
        File override_file(root / L"override" / (std::string(names[i]) + ".txb"));
        Bytes override_data;
        if (override_file.handle != INVALID_HANDLE_VALUE) {
            if (texture(override_file, 0,
                        static_cast<uint32_t>((std::min)(override_file.size, uint64_t(UINT32_MAX))),
                        override_data) &&
                rasterize(override_data, pixels[i])) {
                ++loaded;
                continue;
            }
        }
        if (rasterize(textures[i], pixels[i]))
            ++loaded;
    }
    ready = loaded == std::size(names);
}
} // namespace glyph_assets
