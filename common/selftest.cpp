// Minimal self-tests exercised via CTest. Uses a manual CHECK macro so that
// failures still abort even under -DNDEBUG.

#include "common/serialization.hpp"
#include "common/constants.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#define CHECK(expr) do { \
    if (!(expr)) { \
        std::fprintf(stderr, "CHECK failed: %s at %s:%d\n", #expr, __FILE__, __LINE__); \
        std::exit(1); \
    } \
} while (0)

#define assert CHECK

using namespace ll;
using namespace ll::proto;

static void test_join_roundtrip() {
    std::uint8_t buf[16];
    JoinRequest jr{PROTOCOL_VERSION};
    std::size_t n = encode_join_request(buf, sizeof(buf), jr);
    assert(n == 3);
    auto t = peek_type(buf, n);
    assert(t && *t == PacketType::JOIN_REQUEST);
    JoinRequest out{};
    assert(decode_join_request(buf, n, out));
    assert(out.client_version == PROTOCOL_VERSION);
}

static void test_input_roundtrip() {
    std::uint8_t buf[16];
    InputPacket p{};
    p.player_id = 7;
    p.seq       = 0xDEADBEEFu;
    p.mx        = -1;
    p.my        = 1;
    std::size_t n = encode_input(buf, sizeof(buf), p);
    assert(n == 1 + 2 + 4 + 1 + 1);
    InputPacket out{};
    assert(decode_input(buf, n, out));
    assert(out.player_id == 7);
    assert(out.seq == 0xDEADBEEFu);
    assert(out.mx == -1);
    assert(out.my == 1);
}

static void test_snapshot_roundtrip() {
    Snapshot s{};
    s.server_tick = 12345;
    PlayerState a{}; a.id = 1; a.pos = {10.f, 20.f}; a.vel = {0.f, -1.f}; a.last_processed_input_seq = 99;
    PlayerState b{}; b.id = 2; b.pos = {-5.f, 3.14f}; b.vel = {1.f, 0.f}; b.last_processed_input_seq = 100;
    s.players.push_back(a);
    s.players.push_back(b);

    std::vector<std::uint8_t> buf(MAX_SNAPSHOT_BYTES);
    std::size_t n = encode_snapshot(buf.data(), buf.size(), s);
    assert(n > 0);
    assert(n == 1 + 4 + 2 + 2 * (2 + 4 + 4 + 4 + 4 + 4));

    Snapshot out{};
    assert(decode_snapshot(buf.data(), n, out));
    assert(out.server_tick == 12345);
    assert(out.players.size() == 2);
    assert(out.players[0].id == 1 && out.players[0].pos.x == 10.f);
    assert(out.players[1].id == 2 && out.players[1].last_processed_input_seq == 100);
}

static void test_bounds_fail() {
    std::uint8_t buf[2]; // too small for JOIN_ACCEPT
    JoinAccept ja{1, 100.f, 100.f};
    std::size_t n = encode_join_accept(buf, sizeof(buf), ja);
    assert(n == 0);

    // Truncated decode must fail cleanly.
    std::uint8_t good[64];
    std::size_t full = encode_join_accept(good, sizeof(good), ja);
    assert(full > 0);
    JoinAccept out{};
    assert(!decode_join_accept(good, full - 1, out));
}

static void test_disconnect_roundtrip() {
    std::uint8_t buf[8];
    DisconnectNotice d{42};
    std::size_t n = encode_disconnect(buf, sizeof(buf), d);
    assert(n == 3);
    DisconnectNotice out{};
    assert(decode_disconnect(buf, n, out));
    assert(out.player_id == 42);
}

int main() {
    test_join_roundtrip();
    test_input_roundtrip();
    test_snapshot_roundtrip();
    test_bounds_fail();
    test_disconnect_roundtrip();
    std::puts("ll_selftest OK");
    return 0;
}
