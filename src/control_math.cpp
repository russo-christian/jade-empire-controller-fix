#include "control_math.hpp"

#include <cmath>

float length(Vec3 v) {
    return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
}

Vec3 camera_forward(Quaternion q) {
    return {-2 * (q.w * q.y + q.x * q.z), -2 * (q.z * q.y - q.x * q.w),
            2 * (q.x * q.x + q.y * q.y) - 1};
}

// Inputs are the game's processed axes: +0x10 is backward, +0x14 is right.
// Preserve the game's speed (including its analog scaling and walk modifier).
bool plan_movement(Quaternion q, Vec3 facing, Vec3 velocity, float backward, float right,
                   Movement& result) {
    if (!std::isfinite(backward) || !std::isfinite(right))
        return false;
    if (backward == 0 && right == 0) {
        result = {facing, velocity, {0, 0, 0}};
        return true;
    }
    Vec3 forward = camera_forward(q);
    forward.z = 0;
    const float horizontal = length(forward);
    if (!std::isfinite(horizontal) || horizontal < 0.001f)
        return false;
    forward.x /= horizontal;
    forward.y /= horizontal;
    Vec3 direction{-backward * forward.x + right * forward.y,
                   -backward * forward.y - right * forward.x, 0};
    const float magnitude = length(direction);
    const float speed = length(velocity);
    if (!std::isfinite(magnitude) || magnitude < 0.000001f || !std::isfinite(speed))
        return false;
    direction.x /= magnitude;
    direction.y /= magnitude;
    result = {direction, {direction.x * speed, direction.y * speed, 0}, {0, 1, 0}};
    return true;
}
