#pragma once

#include <cstdint>

namespace ll {

struct Vec2 {
    float x = 0.0f;
    float y = 0.0f;

    Vec2() = default;
    constexpr Vec2(float ax, float ay) : x(ax), y(ay) {}

    Vec2 operator+(const Vec2& r) const { return {x + r.x, y + r.y}; }
    Vec2 operator-(const Vec2& r) const { return {x - r.x, y - r.y}; }
    Vec2 operator*(float s)       const { return {x * s, y * s}; }
};

struct PlayerState {
    std::uint16_t id   = 0;
    Vec2          pos{};
    Vec2          vel{};
    std::uint32_t last_processed_input_seq = 0;
};

struct InputCommand {
    std::uint16_t player_id = 0;
    std::uint32_t seq       = 0;
    std::int8_t   mx        = 0; // -1, 0, +1
    std::int8_t   my        = 0;
};

} // namespace ll
