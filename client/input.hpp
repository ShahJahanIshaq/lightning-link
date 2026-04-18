#pragma once

#include <cstdint>

namespace sf { class Event; }

namespace ll::cl {

struct InputSnapshot {
    std::int8_t mx = 0;
    std::int8_t my = 0;
};

// Non-SFML input source (e.g., bot). Keeps the client testable headless.
struct InputSource {
    virtual ~InputSource() = default;
    virtual InputSnapshot poll(double now_sec) = 0;
};

} // namespace ll::cl
