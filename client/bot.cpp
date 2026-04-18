#include "client/bot.hpp"

#include <cmath>

namespace ll::cl {

BotPattern parse_bot_pattern(const std::string& s) {
    if (s == "circle") return BotPattern::Circle;
    if (s == "sine")   return BotPattern::Sine;
    if (s == "random") return BotPattern::Random;
    if (s == "zigzag") return BotPattern::Zigzag;
    return BotPattern::None;
}

BotInput::BotInput(BotPattern p, std::uint64_t seed)
    : pattern_(p), rng_(seed) {}

InputSnapshot BotInput::poll(double now_sec) {
    InputSnapshot s{};
    switch (pattern_) {
        case BotPattern::None: break;
        case BotPattern::Circle: {
            double a = now_sec * 1.5;
            double cx = std::cos(a);
            double cy = std::sin(a);
            s.mx = static_cast<std::int8_t>(cx > 0.3 ? 1 : (cx < -0.3 ? -1 : 0));
            s.my = static_cast<std::int8_t>(cy > 0.3 ? 1 : (cy < -0.3 ? -1 : 0));
            break;
        }
        case BotPattern::Sine: {
            double a = std::sin(now_sec * 2.0);
            s.mx = static_cast<std::int8_t>(a > 0.2 ? 1 : (a < -0.2 ? -1 : 0));
            s.my = 0;
            break;
        }
        case BotPattern::Random: {
            if (now_sec - last_switch_ > 0.5) {
                std::uniform_int_distribution<int> d(-1, 1);
                last_mx_ = static_cast<std::int8_t>(d(rng_));
                last_my_ = static_cast<std::int8_t>(d(rng_));
                last_switch_ = now_sec;
            }
            s.mx = last_mx_;
            s.my = last_my_;
            break;
        }
        case BotPattern::Zigzag: {
            int phase = static_cast<int>(now_sec) % 4;
            switch (phase) {
                case 0: s.mx =  1; s.my =  0; break;
                case 1: s.mx =  0; s.my =  1; break;
                case 2: s.mx = -1; s.my =  0; break;
                default: s.mx = 0; s.my = -1; break;
            }
            break;
        }
    }
    return s;
}

} // namespace ll::cl
