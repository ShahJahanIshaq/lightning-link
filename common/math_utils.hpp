#pragma once

#include "common/types.hpp"

#include <algorithm>
#include <cmath>

namespace ll {

inline float clampf(float v, float lo, float hi) {
    return std::max(lo, std::min(v, hi));
}

inline Vec2 clamp_to_arena(Vec2 p, float w, float h) {
    return {clampf(p.x, 0.0f, w), clampf(p.y, 0.0f, h)};
}

inline Vec2 normalize_dir(std::int8_t mx, std::int8_t my) {
    float x = static_cast<float>(mx);
    float y = static_cast<float>(my);
    float m = std::sqrt(x * x + y * y);
    if (m > 0.0f) {
        return {x / m, y / m};
    }
    return {0.0f, 0.0f};
}

inline Vec2 lerp(Vec2 a, Vec2 b, float t) {
    return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t};
}

} // namespace ll
