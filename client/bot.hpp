#pragma once

#include "client/input.hpp"

#include <cstdint>
#include <random>
#include <string>

namespace ll::cl {

// Scripted, seeded input sources for automated multi-client stress runs.
enum class BotPattern {
    None,
    Circle,
    Sine,
    Random,
    Zigzag,
};

BotPattern parse_bot_pattern(const std::string& s);

class BotInput : public InputSource {
public:
    BotInput(BotPattern p, std::uint64_t seed);
    InputSnapshot poll(double now_sec) override;

private:
    BotPattern       pattern_;
    std::mt19937_64  rng_;
    double           last_switch_ = 0.0;
    std::int8_t      last_mx_ = 0;
    std::int8_t      last_my_ = 0;
};

} // namespace ll::cl
