// Baseline TCP + text-protocol server. Intentionally unoptimized to serve as an
// apples-to-apples comparator for the UDP+binary+prediction+interpolation path.
//
// Protocol (newline-delimited ASCII):
//   client -> server : "IN <seq> <mx> <my>\n"
//   server -> client : "SS <tick> <n> <id> <x> <y> <vx> <vy> <last_seq> ...\n"
//   server -> client : first line after accept is "JA <player_id> <arena_w> <arena_h>\n"
//
// The server accepts any client, assigns a player id, and blocks on a per-tick loop.
// Same simulation cadence and snapshot cadence as the UDP path, so the A/B difference
// measures transport+serialization+prediction/interpolation, not framing rate.

#include "server/server.hpp"
#include "server/world_state.hpp"
#include "server/session_manager.hpp"
#include "common/constants.hpp"
#include "common/logging.hpp"
#include "common/net_compat.hpp"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace ll::srv {

namespace {

struct TcpClient {
    int           fd = -1;
    std::uint16_t player_id = 0;
    std::string   rx_buffer;
    std::chrono::steady_clock::time_point last_input;
};

std::uint16_t alloc_id(const std::unordered_map<int, TcpClient>& clients) {
    for (std::uint16_t i = 1; i <= MAX_PLAYERS; ++i) {
        bool taken = false;
        for (const auto& [fd, c] : clients) if (c.player_id == i) { taken = true; break; }
        if (!taken) return i;
    }
    return 0;
}

void send_line(int fd, const std::string& s) {
    ::send(fd, s.data(), s.size(), 0);
}

std::string format_snapshot_line(const WorldState& w) {
    std::ostringstream oss;
    auto states = w.collect_player_states();
    oss << "SS " << w.tick() << ' ' << states.size();
    for (const auto& p : states) {
        oss << ' ' << p.id
            << ' ' << p.pos.x
            << ' ' << p.pos.y
            << ' ' << p.vel.x
            << ' ' << p.vel.y
            << ' ' << p.last_processed_input_seq;
    }
    oss << '\n';
    return oss.str();
}

void process_lines(TcpClient& c, WorldState& world, std::uint64_t& inputs_received) {
    std::size_t nl;
    while ((nl = c.rx_buffer.find('\n')) != std::string::npos) {
        std::string line = c.rx_buffer.substr(0, nl);
        c.rx_buffer.erase(0, nl + 1);
        if (line.empty()) continue;

        std::istringstream iss(line);
        std::string cmd;
        iss >> cmd;
        if (cmd == "IN") {
            std::uint32_t seq = 0;
            int mx = 0, my = 0;
            iss >> seq >> mx >> my;
            world.apply_input(c.player_id, seq,
                              static_cast<std::int8_t>(mx),
                              static_cast<std::int8_t>(my));
            ++inputs_received;
            c.last_input = std::chrono::steady_clock::now();
        } else if (cmd == "BYE") {
            c.fd = -1; // marked for removal
        }
    }
}

} // namespace

int run_baseline_server(const ServerConfig& cfg) {
    int lfd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (lfd < 0) { std::perror("socket"); return 1; }
    int one = 1;
    ::setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port        = htons(cfg.tcp_port);
    if (::bind(lfd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::perror("bind"); return 1;
    }
    if (::listen(lfd, 16) < 0) { std::perror("listen"); return 1; }
    net::set_nonblocking(lfd);

    CsvLog log;
    std::ostringstream path;
    path << cfg.log_dir << "/server_" << cfg.run_label << "_" << wall_time_ms() << ".csv";
    log.open(path.str(),
             "wall_time_ms,server_tick,connected_players,bytes_sent_total,"
             "snapshots_sent_total,input_packets_received_total");

    std::fprintf(stdout, "[server-baseline] TCP listening on :%u (run=%s)\n",
                 cfg.tcp_port, cfg.run_label.c_str());
    std::fflush(stdout);

    WorldState world;
    std::unordered_map<int, TcpClient> clients;
    std::uint64_t bytes_sent     = 0;
    std::uint64_t snapshots_sent = 0;
    std::uint64_t inputs_received = 0;
    std::uint64_t last_log_ms    = 0;

    using clock = std::chrono::steady_clock;
    const auto t0 = clock::now();
    auto next_tick = t0;

    while (!Server::stop_.load()) {
        // Accept loop.
        for (;;) {
            sockaddr_in ca{};
            socklen_t   cl = sizeof(ca);
            int cfd = ::accept(lfd, reinterpret_cast<sockaddr*>(&ca), &cl);
            if (cfd < 0) break;
            net::set_nonblocking(cfd);
            TcpClient c{};
            c.fd         = cfd;
            c.player_id  = alloc_id(clients);
            c.last_input = clock::now();
            if (c.player_id == 0) { ::close(cfd); continue; }
            world.add_player(c.player_id);
            std::ostringstream ja;
            ja << "JA " << c.player_id << ' ' << ARENA_WIDTH << ' ' << ARENA_HEIGHT << '\n';
            send_line(cfd, ja.str());
            clients[cfd] = std::move(c);
            std::fprintf(stdout, "[server-baseline] join -> id=%u\n", clients[cfd].player_id);
        }

        // Read loop.
        for (auto it = clients.begin(); it != clients.end();) {
            char tmp[2048];
            for (;;) {
                ssize_t n = ::recv(it->second.fd, tmp, sizeof(tmp), 0);
                if (n > 0) it->second.rx_buffer.append(tmp, tmp + n);
                else if (n == 0) { it->second.fd = -1; break; }
                else break;
            }
            if (it->second.fd < 0) {
                world.remove_player(it->second.player_id);
                ::close(it->first);
                it = clients.erase(it);
                continue;
            }
            process_lines(it->second, world, inputs_received);
            ++it;
        }

        auto now = clock::now();
        if (now >= next_tick) {
            world.step_tick();
            if (world.tick() % TICKS_PER_SNAPSHOT == 0) {
                std::string line = format_snapshot_line(world);
                ++snapshots_sent;
                for (auto& [fd, c] : clients) {
                    ::send(fd, line.data(), line.size(), 0);
                    bytes_sent += line.size();
                }
            }
            next_tick += std::chrono::duration_cast<clock::duration>(
                std::chrono::duration<double>(DT_SECONDS));
            if (next_tick < now) next_tick = now;
        }

        double elapsed = std::chrono::duration<double>(now - t0).count();
        if (cfg.duration_sec > 0.0 && elapsed >= cfg.duration_sec) break;

        std::uint64_t wm = wall_time_ms();
        if (last_log_ms == 0 || wm - last_log_ms >= static_cast<std::uint64_t>(LOG_FLUSH_SEC * 1000.0)) {
            last_log_ms = wm;
            std::ostringstream row;
            row << wm << ',' << world.tick() << ',' << clients.size() << ','
                << bytes_sent << ',' << snapshots_sent << ',' << inputs_received;
            log.write_line(row.str());
            log.flush();
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    for (auto& [fd, c] : clients) ::close(fd);
    ::close(lfd);
    log.flush();
    log.close();
    return 0;
}

} // namespace ll::srv
