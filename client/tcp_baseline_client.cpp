// Baseline TCP text-protocol client. Deliberately simple: no prediction, no
// interpolation, renders only the most recently received authoritative state.

#include "client/client.hpp"
#include "client/bot.hpp"
#include "client/renderer.hpp"
#include "client/hud.hpp"
#include "common/constants.hpp"
#include "common/logging.hpp"
#include "common/net_compat.hpp"

#include <SFML/Graphics.hpp>
#include <SFML/Window/Keyboard.hpp>

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <queue>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace ll::cl {

namespace {

std::atomic<bool> g_stop_base{false};

struct BaselinePendingInput {
    std::uint32_t seq = 0;
    std::uint64_t send_ms = 0;
};

// Software latency/loss layer applied only in the baseline client so that the
// 5-condition evaluation matrix can be exercised without platform-specific
// traffic shaping. TCP loss is simulated at the line level (a dropped input or
// snapshot is simply discarded - TCP would normally retransmit, so the effect is
// an equivalent of unrecoverable higher-layer loss which is what we want for A/B).
struct DelayedLine { std::uint64_t deliver_at_ms = 0; std::string data; };

struct Conditioner {
    int    add_latency_ms_each_direction = 0; // half of total added RTT
    double loss_prob = 0.0;
    std::mt19937_64 rng{0xCAFEULL};

    std::priority_queue<
        DelayedLine, std::vector<DelayedLine>,
        bool(*)(const DelayedLine&, const DelayedLine&)>
        out_queue{[](const DelayedLine& a, const DelayedLine& b){
            return a.deliver_at_ms > b.deliver_at_ms;
        }};
    std::priority_queue<
        DelayedLine, std::vector<DelayedLine>,
        bool(*)(const DelayedLine&, const DelayedLine&)>
        in_queue{[](const DelayedLine& a, const DelayedLine& b){
            return a.deliver_at_ms > b.deliver_at_ms;
        }};

    bool should_drop() {
        if (loss_prob <= 0.0) return false;
        std::uniform_real_distribution<double> u(0.0, 1.0);
        return u(rng) < loss_prob;
    }
};

InputSnapshot poll_keyboard_b(const sf::RenderWindow* w) {
    // Same rationale as the optimized client: without this focus check both
    // SFML client processes running on the same machine would receive identical
    // key events because sf::Keyboard::isKeyPressed is a global OS-level query.
    if (w == nullptr || !w->hasFocus()) return InputSnapshot{};
    InputSnapshot s{};
    using K = sf::Keyboard::Key;
    if (sf::Keyboard::isKeyPressed(K::Left)  || sf::Keyboard::isKeyPressed(K::A)) s.mx = -1;
    if (sf::Keyboard::isKeyPressed(K::Right) || sf::Keyboard::isKeyPressed(K::D)) s.mx =  1;
    if (sf::Keyboard::isKeyPressed(K::Up)    || sf::Keyboard::isKeyPressed(K::W)) s.my = -1;
    if (sf::Keyboard::isKeyPressed(K::Down)  || sf::Keyboard::isKeyPressed(K::S)) s.my =  1;
    return s;
}

std::string make_log_path_b(const std::string& dir, const std::string& label,
                            std::uint16_t pid, const char* suffix) {
    std::ostringstream oss;
    oss << dir << "/client_" << label << "_p" << pid << "_" << suffix << "_" << wall_time_ms() << ".csv";
    return oss.str();
}

} // namespace

int run_baseline_client(const ClientConfig& cfg) {
    std::signal(SIGINT,  [](int){ g_stop_base.store(true); });
    std::signal(SIGTERM, [](int){ g_stop_base.store(true); });

    const std::uint16_t tcp_port = cfg.tcp_port ? cfg.tcp_port : DEFAULT_TCP_PORT;

    sockaddr_in addr{};
    if (!net::resolve_ipv4(cfg.server_host, tcp_port, addr)) {
        std::fprintf(stderr, "[client-baseline] resolve failed\n");
        return 1;
    }
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return 1;
    if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::perror("connect"); ::close(fd); return 2;
    }
    net::set_nonblocking(fd);

    std::string rx_buf;
    std::uint16_t my_pid = 0;
    float arena_w = ARENA_WIDTH, arena_h = ARENA_HEIGHT;

    auto read_into_buf = [&]() {
        char tmp[2048];
        ssize_t n = ::recv(fd, tmp, sizeof(tmp), 0);
        if (n > 0) rx_buf.append(tmp, tmp + n);
    };

    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (std::chrono::steady_clock::now() < deadline && my_pid == 0) {
        read_into_buf();
        std::size_t nl = rx_buf.find('\n');
        if (nl == std::string::npos) { std::this_thread::sleep_for(std::chrono::milliseconds(5)); continue; }
        std::string line = rx_buf.substr(0, nl);
        rx_buf.erase(0, nl + 1);
        std::istringstream iss(line);
        std::string cmd;
        iss >> cmd;
        if (cmd == "JA") { iss >> my_pid >> arena_w >> arena_h; }
    }
    if (my_pid == 0) { std::fprintf(stderr, "[client-baseline] no JA\n"); ::close(fd); return 3; }
    std::fprintf(stdout, "[client-baseline] joined as %u\n", my_pid);

    const bool has_bot = parse_bot_pattern(cfg.bot_pattern) != BotPattern::None;
    const bool headless = cfg.headless || has_bot;

    Renderer renderer;
    if (!headless) renderer.init(1280, 720, "Lightning Link (baseline)");

    CsvLog periodic_log, input_log;
    periodic_log.open(make_log_path_b(cfg.log_dir, cfg.run_label, my_pid, "periodic"),
                      "wall_time_ms,player_id,latest_snapshot_tick,bytes_received_total,"
                      "input_packets_sent_total,estimated_input_to_visible_delay_ms,"
                      "interpolation_buffer_entries");
    input_log.open(make_log_path_b(cfg.log_dir, cfg.run_label, my_pid, "inputs"),
                   "wall_time_ms,player_id,input_seq,ack_seq,rtt_ms,"
                   "reconciliation_error_px,perceived_delay_ms,mode");

    BotInput bot(parse_bot_pattern(cfg.bot_pattern), cfg.bot_seed ^ my_pid);

    Conditioner cond;
    cond.add_latency_ms_each_direction =
        cfg.lagcfg.add_latency_ms_send + cfg.lagcfg.add_latency_ms_recv;
    // Halve so that total RTT added == CLI --add-latency value.
    cond.add_latency_ms_each_direction /= 2;
    cond.loss_prob = cfg.lagcfg.loss_prob;
    cond.rng.seed(cfg.lagcfg.seed);

    std::unordered_map<std::uint16_t, PlayerState> latest;
    std::unordered_map<std::uint32_t, BaselinePendingInput> pending;
    std::uint32_t latest_tick = 0;
    std::uint64_t bytes_rx = 0;
    std::uint32_t inputs_sent = 0;
    std::uint32_t next_seq = 1;
    double last_delay_ms = 0.0;

    HudInputs hud{};
    hud.mode_label = "baseline";
    hud.local_player_id = my_pid;
    hud.prediction_enabled = false;
    hud.interpolation_enabled = false;

    using clock = std::chrono::steady_clock;
    const auto t0 = clock::now();
    auto next_input = t0;
    auto next_log   = t0;

    while (!g_stop_base.load()) {
        auto now = clock::now();
        double elapsed = std::chrono::duration<double>(now - t0).count();
        if (cfg.duration_sec > 0.0 && elapsed >= cfg.duration_sec) break;

        // Events.
        if (renderer.is_open()) {
            auto* w = renderer.window();
            while (auto ev = w->pollEvent()) {
                if (ev->is<sf::Event::Closed>()) w->close();
                else if (const auto* kp = ev->getIf<sf::Event::KeyPressed>()) {
                    if (kp->code == sf::Keyboard::Key::F1) hud.show_hud = !hud.show_hud;
                    if (kp->code == sf::Keyboard::Key::Escape) w->close();
                }
            }
        }

        // Drain socket into the conditioner's inbound queue.
        char tmp[4096];
        for (;;) {
            ssize_t n = ::recv(fd, tmp, sizeof(tmp), 0);
            if (n > 0) { rx_buf.append(tmp, tmp + n); bytes_rx += static_cast<std::uint64_t>(n); }
            else break;
        }
        {
            std::size_t nl;
            while ((nl = rx_buf.find('\n')) != std::string::npos) {
                std::string line = rx_buf.substr(0, nl);
                rx_buf.erase(0, nl + 1);
                if (line.empty()) continue;
                if (cond.should_drop()) continue; // simulated loss
                DelayedLine dl;
                dl.deliver_at_ms = wall_time_ms() +
                    static_cast<std::uint64_t>(cond.add_latency_ms_each_direction);
                dl.data = std::move(line);
                cond.in_queue.push(std::move(dl));
            }
        }

        // Release matured inbound lines.
        while (!cond.in_queue.empty() && cond.in_queue.top().deliver_at_ms <= wall_time_ms()) {
            std::string line = cond.in_queue.top().data;
            cond.in_queue.pop();
            if (line.empty()) continue;
            std::istringstream iss(line);
            std::string cmd;
            iss >> cmd;
            if (cmd == "SS") {
                std::uint32_t tick = 0;
                std::size_t count = 0;
                iss >> tick >> count;
                latest_tick = tick;
                std::uint32_t my_ack_seq = 0;
                bool saw_me = false;
                for (std::size_t i = 0; i < count; ++i) {
                    PlayerState p{};
                    unsigned pid = 0;
                    unsigned long last_seq = 0;
                    iss >> pid >> p.pos.x >> p.pos.y >> p.vel.x >> p.vel.y >> last_seq;
                    p.id = static_cast<std::uint16_t>(pid);
                    p.last_processed_input_seq = static_cast<std::uint32_t>(last_seq);
                    latest[p.id] = p;
                    if (p.id == my_pid) { saw_me = true; my_ack_seq = p.last_processed_input_seq; }
                }
                // Ack-based retirement: identical semantics to optimized mode, yielding
                // a true apples-to-apples wire-RTT comparison between the two transports.
                if (saw_me) {
                    std::uint64_t now_ms = wall_time_ms();
                    for (auto it = pending.begin(); it != pending.end();) {
                        if (it->first <= my_ack_seq) {
                            double d_ms = static_cast<double>(now_ms - it->second.send_ms);
                            last_delay_ms = d_ms;
                            std::ostringstream row;
                            row << now_ms << ',' << my_pid << ',' << it->first << ','
                                << my_ack_seq << ',' << d_ms << ',' << 0.0 << ','
                                << d_ms << ',' << "baseline";
                            input_log.write_line(row.str());
                            it = pending.erase(it);
                        } else {
                            ++it;
                        }
                    }
                }
            }
        }

        // Input at 60 Hz, enqueued through the outbound conditioner.
        if (now >= next_input) {
            InputSnapshot in = has_bot ? bot.poll(elapsed)
                                       : poll_keyboard_b(renderer.is_open() ? renderer.window() : nullptr);
            std::ostringstream line;
            line << "IN " << next_seq << ' ' << int(in.mx) << ' ' << int(in.my) << '\n';
            std::string s = line.str();

            if (!cond.should_drop()) {
                DelayedLine dl;
                dl.deliver_at_ms = wall_time_ms() +
                    static_cast<std::uint64_t>(cond.add_latency_ms_each_direction);
                dl.data = std::move(s);
                cond.out_queue.push(std::move(dl));
            }

            pending[next_seq] = BaselinePendingInput{next_seq, wall_time_ms()};
            if (pending.size() > 256) pending.clear();
            ++inputs_sent;
            ++next_seq;
            next_input += std::chrono::duration_cast<clock::duration>(
                std::chrono::duration<double>(1.0 / INPUT_SEND_RATE_HZ));
            if (next_input < now) next_input = now;
        }

        // Drain matured outbound input lines.
        while (!cond.out_queue.empty() && cond.out_queue.top().deliver_at_ms <= wall_time_ms()) {
            const std::string& data = cond.out_queue.top().data;
            ::send(fd, data.data(), data.size(), 0);
            cond.out_queue.pop();
        }

        // Render.
        if (!headless && renderer.is_open()) {
            std::vector<RenderedPlayer> to_draw;
            for (const auto& [id, p] : latest) {
                RenderedPlayer rp{};
                rp.id = id; rp.pos = p.pos; rp.is_local = (id == my_pid);
                to_draw.push_back(rp);
            }
            hud.window_focused = renderer.is_open() && renderer.window() && renderer.window()->hasFocus();
            hud.snapshots_received = 0;
            hud.inputs_sent = inputs_sent;
            hud.bytes_received = bytes_rx;
            hud.est_delay_ms = last_delay_ms;
            hud.interp_depth = 0;
            hud.reconciliation_error_px = 0;

            renderer.begin_frame();
            renderer.draw_arena(arena_w, arena_h);
            renderer.draw_players(to_draw);
            if (hud.show_hud) renderer.draw_hud(build_hud_lines(hud));
            renderer.end_frame();
            if (!renderer.is_open()) g_stop_base.store(true);
        }

        // Periodic log at 1 Hz.
        if (now >= next_log) {
            std::ostringstream row;
            row << wall_time_ms() << ',' << my_pid << ',' << latest_tick << ','
                << bytes_rx << ',' << inputs_sent << ',' << last_delay_ms << ',' << 0;
            periodic_log.write_line(row.str());
            periodic_log.flush();
            input_log.flush();
            next_log += std::chrono::duration_cast<clock::duration>(
                std::chrono::duration<double>(LOG_FLUSH_SEC));
            if (next_log < now) next_log = now;
        }

        if (headless) std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    // Send BYE.
    std::string bye = "BYE\n";
    ::send(fd, bye.data(), bye.size(), 0);
    ::close(fd);
    periodic_log.flush(); input_log.flush();
    periodic_log.close(); input_log.close();
    return 0;
}

} // namespace ll::cl
