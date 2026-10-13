#pragma once

struct Vec3 {
    float x, y, z;
};
struct Quaternion {
    float w, x, y, z;
};
struct Movement {
    Vec3 facing, velocity, local;
};

float length(Vec3 v);
Vec3 camera_forward(Quaternion q);
// Uses processed backward/right axes; retains native speed and idle facing.
bool plan_movement(Quaternion q, Vec3 facing, Vec3 velocity, float backward, float right,
                   Movement& result);
