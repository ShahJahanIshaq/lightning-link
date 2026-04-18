#pragma once

#include "common/constants.hpp"
#include "common/types.hpp"

#include <cstdint>
#include <vector>

namespace ll::proto {

enum class PacketType : std::uint8_t {
    JOIN_REQUEST      = 1,
    JOIN_ACCEPT       = 2,
    INPUT_COMMAND     = 3,
    WORLD_SNAPSHOT    = 4,
    DISCONNECT_NOTICE = 5,
};

// --- packet field layouts ----------------------------------------------------
// JOIN_REQUEST        : u8 type | u16 client_version
// JOIN_ACCEPT         : u8 type | u16 player_id | f32 arena_w | f32 arena_h
// INPUT_COMMAND       : u8 type | u16 player_id | u32 seq | i8 mx | i8 my
// PLAYER_STATE (rec)  : u16 id  | f32 x | f32 y | f32 vx | f32 vy | u32 last_seq
// WORLD_SNAPSHOT      : u8 type | u32 server_tick | u16 player_count | PLAYER_STATE[n]
// DISCONNECT_NOTICE   : u8 type | u16 player_id
//
// All multi-byte integers are transmitted in network byte order.
// Floats are transmitted as IEEE-754 binary32 re-interpreted as u32 in network byte order.

struct JoinRequest  { std::uint16_t client_version = PROTOCOL_VERSION; };
struct JoinAccept   { std::uint16_t player_id = 0; float arena_w = 0.f; float arena_h = 0.f; };
struct InputPacket  {
    std::uint16_t player_id = 0;
    std::uint32_t seq       = 0;
    std::int8_t   mx        = 0;
    std::int8_t   my        = 0;
};
struct Snapshot     {
    std::uint32_t server_tick = 0;
    std::vector<PlayerState> players;
};
struct DisconnectNotice { std::uint16_t player_id = 0; };

// Upper bound for encoded snapshot size (type + tick + count + MAX_PLAYERS records).
inline constexpr std::size_t MAX_SNAPSHOT_BYTES =
    1 + 4 + 2 + MAX_PLAYERS * (2 + 4 + 4 + 4 + 4 + 4);

} // namespace ll::proto
