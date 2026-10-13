#include "../src/control_math.hpp"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <limits>

static bool near(float a, float b) {
    return std::fabs(a - b) < 0.0001f;
}
int main() {
    const float s = std::sqrt(0.5f);
    const Quaternion north{s, s, 0, 0};
    const Quaternion west{s, 0, s, 0};
    const Vec3 facing{0, 1, 0}, velocity{3, 4, 0};
    Movement result{};
    // Camera motion alone must never change character facing.
    assert(plan_movement(west, facing, {0, 0, 0}, 0, 0, result));
    assert(near(result.facing.x, 0) && near(result.facing.y, 1));
    // Forward, right, backward, left are camera-relative and keep original speed.
    const float axes[][2] = {{-1, 0}, {0, 1}, {1, 0}, {0, -1}};
    const Vec3 expected[] = {{0, 1, 0}, {1, 0, 0}, {0, -1, 0}, {-1, 0, 0}};
    for (int i = 0; i < 4; ++i) {
        assert(plan_movement(north, facing, velocity, axes[i][0], axes[i][1], result));
        assert(near(result.facing.x, expected[i].x) && near(result.facing.y, expected[i].y));
        assert(near(length(result.velocity), 5));
        assert(near(result.local.x, 0) && near(result.local.y, 1));
    }
    assert(plan_movement(west, facing, velocity, -1, 0, result));
    assert(near(result.facing.x, -1) && near(result.facing.y, 0));
    assert(plan_movement(north, facing, velocity, -0.5f, 0.5f, result));
    assert(near(result.facing.x, s) && near(result.facing.y, s));
    assert(near(length(result.velocity), 5));
    assert(!plan_movement({1, 0, 0, 0}, facing, velocity, -1, 0, result));
    assert(!plan_movement(north, facing, velocity, std::numeric_limits<float>::quiet_NaN(), 0,
                          result));
    std::puts("controller direction, idle, diagonal, speed, and invalid-input checks passed");
}
