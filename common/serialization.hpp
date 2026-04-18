#pragma once

#include "common/protocol.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace ll::proto {

// ---- bounded byte-buffer writer/reader --------------------------------------
class BufferWriter {
public:
    BufferWriter(std::uint8_t* data, std::size_t cap)
        : data_(data), cap_(cap) {}

    bool put_u8(std::uint8_t v);
    bool put_u16(std::uint16_t v);
    bool put_u32(std::uint32_t v);
    bool put_i8(std::int8_t v);
    bool put_f32(float v);

    std::size_t size() const { return pos_; }
    bool        ok()   const { return ok_; }

private:
    std::uint8_t* data_ = nullptr;
    std::size_t   cap_  = 0;
    std::size_t   pos_  = 0;
    bool          ok_   = true;
};

class BufferReader {
public:
    BufferReader(const std::uint8_t* data, std::size_t size)
        : data_(data), size_(size) {}

    bool get_u8(std::uint8_t& v);
    bool get_u16(std::uint16_t& v);
    bool get_u32(std::uint32_t& v);
    bool get_i8(std::int8_t& v);
    bool get_f32(float& v);

    std::size_t remaining() const { return size_ - pos_; }
    bool        ok()        const { return ok_; }

private:
    const std::uint8_t* data_ = nullptr;
    std::size_t         size_ = 0;
    std::size_t         pos_  = 0;
    bool                ok_   = true;
};

// ---- typed encode / decode --------------------------------------------------
// Return value = encoded byte count, or 0 on failure.
std::size_t encode_join_request(std::uint8_t* out, std::size_t cap, const JoinRequest& p);
std::size_t encode_join_accept(std::uint8_t* out, std::size_t cap, const JoinAccept& p);
std::size_t encode_input(std::uint8_t* out, std::size_t cap, const InputPacket& p);
std::size_t encode_snapshot(std::uint8_t* out, std::size_t cap, const Snapshot& p);
std::size_t encode_disconnect(std::uint8_t* out, std::size_t cap, const DisconnectNotice& p);

// Peek the packet type without consuming; returns nullopt on empty/bad buffer.
std::optional<PacketType> peek_type(const std::uint8_t* data, std::size_t size);

bool decode_join_request(const std::uint8_t* data, std::size_t size, JoinRequest& out);
bool decode_join_accept(const std::uint8_t* data, std::size_t size, JoinAccept& out);
bool decode_input(const std::uint8_t* data, std::size_t size, InputPacket& out);
bool decode_snapshot(const std::uint8_t* data, std::size_t size, Snapshot& out);
bool decode_disconnect(const std::uint8_t* data, std::size_t size, DisconnectNotice& out);

} // namespace ll::proto
