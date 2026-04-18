#pragma once

#include <cstdint>

namespace ll {

// --- simulation --------------------------------------------------------------
inline constexpr int    TICK_RATE_HZ          = 60;
inline constexpr double DT_SECONDS            = 1.0 / TICK_RATE_HZ;
inline constexpr int    SNAPSHOT_RATE_HZ      = 20;
inline constexpr int    TICKS_PER_SNAPSHOT    = TICK_RATE_HZ / SNAPSHOT_RATE_HZ; // 3
inline constexpr int    INPUT_SEND_RATE_HZ    = 60;

// --- arena -------------------------------------------------------------------
inline constexpr float  ARENA_WIDTH           = 1280.0f;
inline constexpr float  ARENA_HEIGHT          = 720.0f;
inline constexpr float  PLAYER_SPEED          = 240.0f;      // world units / second
inline constexpr float  PLAYER_RADIUS         = 18.0f;

// --- networking --------------------------------------------------------------
inline constexpr std::uint16_t DEFAULT_UDP_PORT = 54321;
inline constexpr std::uint16_t DEFAULT_TCP_PORT = 54322;
inline constexpr std::uint16_t PROTOCOL_VERSION = 2;          // v2 adds last_processed_input_seq
inline constexpr int           MAX_PLAYERS      = 8;
inline constexpr int           MIN_PLAYERS      = 2;
inline constexpr int           MAX_PACKET_BYTES = 1200;

// --- prediction / interpolation ---------------------------------------------
inline constexpr int    INPUT_HISTORY_SIZE    = 120;
inline constexpr int    SNAPSHOT_HISTORY_SIZE = 32;
inline constexpr double INTERP_DELAY_SEC      = 0.100;         // 100 ms

// --- liveness ----------------------------------------------------------------
inline constexpr double CLIENT_TIMEOUT_SEC    = 3.0;
inline constexpr double ENTITY_STALE_SEC      = 2.0;

// --- logging -----------------------------------------------------------------
inline constexpr double LOG_FLUSH_SEC         = 1.0;           // 1 Hz periodic logging

} // namespace ll
