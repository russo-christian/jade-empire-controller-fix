#include "../src/glyph_assets.hpp"

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <windows.h>

namespace fs = std::filesystem;
using namespace glyph_assets;

static Bytes solid(bool dxt5 = false) {
    Bytes txb(128 + (dxt5 ? 16 : 8));
    txb[0] = dxt5 ? 16 : 8;
    txb[8] = txb[10] = 4;
    txb[12] = dxt5 ? 12 : 10;
    const unsigned color = dxt5 ? 136 : 128;
    txb[color] = 0;
    txb[color + 1] = 0xf8; // Red RGB565 endpoint.
    txb[color + 2] = 0xe0;
    txb[color + 3] = 0x07; // Green endpoint.
    if (dxt5)
        txb[128] = 255;
    return txb;
}
static void synthetic() {
    Pixels result{};
    Bytes txb = solid();
    assert(rasterize(txb, result));
    for (unsigned p : result)
        assert(p == 0xffff0000);
    // DXT1 transparent palette and bottom-up row orientation.
    txb[128] = txb[129] = 0;
    txb[130] = txb[131] = 255;
    for (unsigned i = 132; i < 136; ++i)
        txb[i] = 255;
    assert(rasterize(txb, result));
    for (unsigned p : result)
        assert(p == 0);
    txb = solid();
    txb[132] = 0x55;
    std::vector<RGBA> rgba;
    unsigned width{}, height{};
    assert(decode(txb, rgba, width, height));
    assert(rgba[0].r == 255 && rgba[12].g == 255 && !rgba[12].r);
    // Both DXT5 alpha interpolation modes, independent of the color endpoints.
    for (unsigned mode = 0; mode < 2; ++mode) {
        txb = solid(true);
        txb[128] = mode ? 10 : 200;
        txb[129] = mode ? 200 : 100;
        uint64_t bits{};
        for (unsigned i = 0; i < 16; ++i)
            bits |= uint64_t(i % 8) << (i * 3);
        for (unsigned i = 0; i < 6; ++i)
            txb[130 + i] = static_cast<unsigned char>(bits >> (i * 8));
        assert(decode(txb, rgba, width, height));
        const unsigned expected[2][8]{{200, 100, 185, 171, 157, 142, 128, 114},
                                      {10, 200, 48, 86, 124, 162, 0, 255}};
        for (unsigned i = 0; i < 16; ++i)
            assert(rgba[(3 - i / 4) * 4 + i % 4].a == expected[mode][i % 8]);
    }
    txb = solid();
    txb.resize(130);
    assert(!rasterize(txb, result));
    txb = solid();
    txb[8] = 0;
    assert(!rasterize(txb, result));
    txb = solid();
    txb[9] = 8;
    assert(!rasterize(txb, result));
    txb = solid();
    txb[12] = 99;
    assert(!rasterize(txb, result));
    fs::path archive;
    assert(relative_archive("data\\artcreatures.bif", archive));
    for (const char* bad :
         {"../secret", "data/../../secret", "C:\\secret", "\\\\server\\share", "/secret"})
        assert(!relative_archive(bad, archive));
    std::puts("Synthetic glyph decoder checks passed: DXT1/DXT5, alpha modes, row order, scaling, "
              "malformed data and archive-path bounds");
}
int main() {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    _set_error_mode(_OUT_TO_STDERR);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    setvbuf(stdout, nullptr, _IONBF, 0);
    synthetic();
    return 0;
}
