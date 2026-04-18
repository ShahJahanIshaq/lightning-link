#include "server/server.hpp"

#include "common/serialization.hpp"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <thread>

namespace ll::srv {

std::atomic<bool> Server::stop_{false};

void Server::request_stop() { stop_.store(true); }

Server::Server(const ServerConfig& cfg)
    : cfg_(cfg), broadcaster_(sock_) {}

Server::~Server() = default;

static std::string make_log_path(const std::string& dir, const std::string& label) {
    std::ostringstream oss;
    oss << dir << "/server_" << label << "_" << wall_time_ms() << ".csv";
    return oss.str();
}

int Server::run() {
    if (!sock_.bind(cfg_.udp_port)) {
        std::fprintf(stderr, "[server] UDP bind :%u failed\n", cfg_.udp_port);
        return 1;
    }
    sock_.configure(cfg_.lagcfg);

    log_.open(make_log_path(cfg_.log_dir, cfg_.run_label),
              "wall_time_ms,server_tick,connected_players,bytes_sent_total,"
              "snapshots_sent_total,input_packets_received_total");

    std::fprintf(stdout,
                 "[server] UDP optimized mode listening on port %u (run=%s)\n",
                 cfg_.udp_port, cfg_.run_label.c_str());
    std::fflush(stdout);

    using clock   = std::chrono::steady_clock;
    const auto    t0       = clock::now();
    const double  tick_dt  = DT_SECONDS;
    auto          next_tick = t0;

    while (!stop_.load()) {
        // Drain any matured conditioner deliveries.
        sock_.pump();

        handle_inbound_udp();

        auto now = clock::now();
        if (now >= next_tick) {
            tick_and_maybe_broadcast();
            next_tick += std::chrono::duration_cast<clock::duration>(
                std::chrono::duration<double>(tick_dt));
            if (next_tick < now) next_tick = now; // catch-up guard
        }

        // Periodic reaper and log.
        double elapsed = std::chrono::duration<double>(now - t0).count();
        if (cfg_.duration_sec > 0.0 && elapsed >= cfg_.duration_sec) break;

        do_periodic_log(elapsed);

        // Sleep until the earlier of next tick or 2ms to keep CPU low.
        auto sleep_until = std::min(next_tick, now + std::chrono::milliseconds(2));
        std::this_thread::sleep_until(sleep_until);
    }

    log_.flush();
    log_.close();
    std::fprintf(stdout, "[server] stopping.\n");
    return 0;
}

void Server::handle_inbound_udp() {
    std::uint8_t buf[MAX_PACKET_BYTES];
    for (int guard = 0; guard < 256; ++guard) {
        auto rr = sock_.recv_from(buf, sizeof(buf));
        if (!rr.has_data) break;

        auto t = proto::peek_type(buf, rr.size);
        if (!t) continue;

        switch (*t) {
            case proto::PacketType::JOIN_REQUEST: {
                proto::JoinRequest jr{};
                if (!proto::decode_join_request(buf, rr.size, jr)) break;
                auto id = sessions_.register_or_get(rr.from);
                if (!id) break;
                world_.add_player(*id);
                proto::JoinAccept ja{*id, ARENA_WIDTH, ARENA_HEIGHT};
                std::uint8_t out[32];
                std::size_t n = proto::encode_join_accept(out, sizeof(out), ja);
                sock_.send_to(rr.from, out, n);
                std::fprintf(stdout, "[server] join %s -> id=%u\n",
                             net::endpoint_to_string(rr.from).c_str(), *id);
                break;
            }
            case proto::PacketType::INPUT_COMMAND: {
                proto::InputPacket ip{};
                if (!proto::decode_input(buf, rr.size, ip)) break;
                if (!sessions_.verify_input(rr.from, ip.player_id)) break;
                sessions_.touch(rr.from);
                world_.apply_input(ip.player_id, ip.seq, ip.mx, ip.my);
                ++inputs_received_total_;
                break;
            }
            case proto::PacketType::DISCONNECT_NOTICE: {
                proto::DisconnectNotice dn{};
                if (!proto::decode_disconnect(buf, rr.size, dn)) break;
                if (auto* s = sessions_.by_endpoint(rr.from); s && s->player_id == dn.player_id) {
                    world_.remove_player(dn.player_id);
                    sessions_.remove_by_endpoint(rr.from);
                    std::fprintf(stdout, "[server] disconnect id=%u\n", dn.player_id);
                }
                break;
            }
            default:
                break;
        }
    }
}

void Server::tick_and_maybe_broadcast() {
    // Reap timed-out sessions and remove their world players.
    auto dead = sessions_.reap_timeouts();
    for (auto id : dead) {
        world_.remove_player(id);
        std::fprintf(stdout, "[server] timeout id=%u\n", id);
    }

    world_.step_tick();

    if (world_.tick() % TICKS_PER_SNAPSHOT == 0) {
        proto::Snapshot s{};
        s.server_tick = world_.tick();
        s.players     = world_.collect_player_states();
        broadcaster_.broadcast(s, sessions_.all_endpoints());
    }
}

void Server::do_periodic_log(double /*now_sec*/) {
    std::uint64_t now_ms = wall_time_ms();
    if (last_log_ms_ == 0 || now_ms - last_log_ms_ >= static_cast<std::uint64_t>(LOG_FLUSH_SEC * 1000.0)) {
        last_log_ms_ = now_ms;
        std::ostringstream row;
        row << now_ms << ','
            << world_.tick() << ','
            << sessions_.size() << ','
            << broadcaster_.total_bytes_sent() << ','
            << broadcaster_.total_snapshots() << ','
            << inputs_received_total_;
        log_.write_line(row.str());
        log_.flush();
    }
}

} // namespace ll::srv
