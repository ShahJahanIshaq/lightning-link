#include "common/serialization.hpp"

#include <arpa/inet.h>
#include <cstring>

namespace ll::proto {

namespace {

std::uint32_t bits_from_float(float v) {
    std::uint32_t u;
    std::memcpy(&u, &v, sizeof(u));
    return u;
}
float float_from_bits(std::uint32_t u) {
    float v;
    std::memcpy(&v, &u, sizeof(v));
    return v;
}

} // namespace

// ---- BufferWriter -----------------------------------------------------------
bool BufferWriter::put_u8(std::uint8_t v) {
    if (!ok_ || pos_ + 1 > cap_) { ok_ = false; return false; }
    data_[pos_++] = v;
    return true;
}
bool BufferWriter::put_u16(std::uint16_t v) {
    if (!ok_ || pos_ + 2 > cap_) { ok_ = false; return false; }
    std::uint16_t n = htons(v);
    std::memcpy(data_ + pos_, &n, 2);
    pos_ += 2;
    return true;
}
bool BufferWriter::put_u32(std::uint32_t v) {
    if (!ok_ || pos_ + 4 > cap_) { ok_ = false; return false; }
    std::uint32_t n = htonl(v);
    std::memcpy(data_ + pos_, &n, 4);
    pos_ += 4;
    return true;
}
bool BufferWriter::put_i8(std::int8_t v) {
    return put_u8(static_cast<std::uint8_t>(v));
}
bool BufferWriter::put_f32(float v) {
    return put_u32(bits_from_float(v));
}

// ---- BufferReader -----------------------------------------------------------
bool BufferReader::get_u8(std::uint8_t& v) {
    if (!ok_ || pos_ + 1 > size_) { ok_ = false; return false; }
    v = data_[pos_++];
    return true;
}
bool BufferReader::get_u16(std::uint16_t& v) {
    if (!ok_ || pos_ + 2 > size_) { ok_ = false; return false; }
    std::uint16_t n;
    std::memcpy(&n, data_ + pos_, 2);
    v = ntohs(n);
    pos_ += 2;
    return true;
}
bool BufferReader::get_u32(std::uint32_t& v) {
    if (!ok_ || pos_ + 4 > size_) { ok_ = false; return false; }
    std::uint32_t n;
    std::memcpy(&n, data_ + pos_, 4);
    v = ntohl(n);
    pos_ += 4;
    return true;
}
bool BufferReader::get_i8(std::int8_t& v) {
    std::uint8_t u;
    if (!get_u8(u)) return false;
    v = static_cast<std::int8_t>(u);
    return true;
}
bool BufferReader::get_f32(float& v) {
    std::uint32_t u;
    if (!get_u32(u)) return false;
    v = float_from_bits(u);
    return true;
}

// ---- typed encoders ---------------------------------------------------------
std::size_t encode_join_request(std::uint8_t* out, std::size_t cap, const JoinRequest& p) {
    BufferWriter w(out, cap);
    w.put_u8(static_cast<std::uint8_t>(PacketType::JOIN_REQUEST));
    w.put_u16(p.client_version);
    return w.ok() ? w.size() : 0;
}

std::size_t encode_join_accept(std::uint8_t* out, std::size_t cap, const JoinAccept& p) {
    BufferWriter w(out, cap);
    w.put_u8(static_cast<std::uint8_t>(PacketType::JOIN_ACCEPT));
    w.put_u16(p.player_id);
    w.put_f32(p.arena_w);
    w.put_f32(p.arena_h);
    return w.ok() ? w.size() : 0;
}

std::size_t encode_input(std::uint8_t* out, std::size_t cap, const InputPacket& p) {
    BufferWriter w(out, cap);
    w.put_u8(static_cast<std::uint8_t>(PacketType::INPUT_COMMAND));
    w.put_u16(p.player_id);
    w.put_u32(p.seq);
    w.put_i8(p.mx);
    w.put_i8(p.my);
    return w.ok() ? w.size() : 0;
}

std::size_t encode_snapshot(std::uint8_t* out, std::size_t cap, const Snapshot& p) {
    BufferWriter w(out, cap);
    w.put_u8(static_cast<std::uint8_t>(PacketType::WORLD_SNAPSHOT));
    w.put_u32(p.server_tick);
    w.put_u16(static_cast<std::uint16_t>(p.players.size()));
    for (const auto& ps : p.players) {
        w.put_u16(ps.id);
        w.put_f32(ps.pos.x);
        w.put_f32(ps.pos.y);
        w.put_f32(ps.vel.x);
        w.put_f32(ps.vel.y);
        w.put_u32(ps.last_processed_input_seq);
    }
    return w.ok() ? w.size() : 0;
}

std::size_t encode_disconnect(std::uint8_t* out, std::size_t cap, const DisconnectNotice& p) {
    BufferWriter w(out, cap);
    w.put_u8(static_cast<std::uint8_t>(PacketType::DISCONNECT_NOTICE));
    w.put_u16(p.player_id);
    return w.ok() ? w.size() : 0;
}

// ---- typed decoders ---------------------------------------------------------
std::optional<PacketType> peek_type(const std::uint8_t* data, std::size_t size) {
    if (size == 0) return std::nullopt;
    std::uint8_t t = data[0];
    if (t < 1 || t > 5) return std::nullopt;
    return static_cast<PacketType>(t);
}

bool decode_join_request(const std::uint8_t* data, std::size_t size, JoinRequest& out) {
    BufferReader r(data, size);
    std::uint8_t t;
    if (!r.get_u8(t) || t != static_cast<std::uint8_t>(PacketType::JOIN_REQUEST)) return false;
    return r.get_u16(out.client_version);
}

bool decode_join_accept(const std::uint8_t* data, std::size_t size, JoinAccept& out) {
    BufferReader r(data, size);
    std::uint8_t t;
    if (!r.get_u8(t) || t != static_cast<std::uint8_t>(PacketType::JOIN_ACCEPT)) return false;
    return r.get_u16(out.player_id)
        && r.get_f32(out.arena_w)
        && r.get_f32(out.arena_h);
}

bool decode_input(const std::uint8_t* data, std::size_t size, InputPacket& out) {
    BufferReader r(data, size);
    std::uint8_t t;
    if (!r.get_u8(t) || t != static_cast<std::uint8_t>(PacketType::INPUT_COMMAND)) return false;
    return r.get_u16(out.player_id)
        && r.get_u32(out.seq)
        && r.get_i8(out.mx)
        && r.get_i8(out.my);
}

bool decode_snapshot(const std::uint8_t* data, std::size_t size, Snapshot& out) {
    BufferReader r(data, size);
    std::uint8_t t;
    if (!r.get_u8(t) || t != static_cast<std::uint8_t>(PacketType::WORLD_SNAPSHOT)) return false;
    if (!r.get_u32(out.server_tick)) return false;
    std::uint16_t n;
    if (!r.get_u16(n)) return false;
    if (n > MAX_PLAYERS) return false;
    out.players.clear();
    out.players.reserve(n);
    for (std::uint16_t i = 0; i < n; ++i) {
        PlayerState ps{};
        if (!r.get_u16(ps.id) ||
            !r.get_f32(ps.pos.x) || !r.get_f32(ps.pos.y) ||
            !r.get_f32(ps.vel.x) || !r.get_f32(ps.vel.y) ||
            !r.get_u32(ps.last_processed_input_seq)) {
            return false;
        }
        out.players.push_back(ps);
    }
    return true;
}

bool decode_disconnect(const std::uint8_t* data, std::size_t size, DisconnectNotice& out) {
    BufferReader r(data, size);
    std::uint8_t t;
    if (!r.get_u8(t) || t != static_cast<std::uint8_t>(PacketType::DISCONNECT_NOTICE)) return false;
    return r.get_u16(out.player_id);
}

} // namespace ll::proto
