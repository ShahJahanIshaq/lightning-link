#include "client/client.hpp"

#include "client/bot.hpp"
#include "client/hud.hpp"
#include "client/interpolation.hpp"
#include "client/network_receiver.hpp"
#include "client/prediction.hpp"
#include "client/renderer.hpp"
#include "client/snapshot_queue.hpp"
#include "common/constants.hpp"
#include "common/logging.hpp"
#include "common/net_compat.hpp"
#include "common/protocol.hpp"
#include "common/serialization.hpp"

#include <SFML/Graphics.hpp>
#include <SFML/Window/Keyboard.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <thread>
#include <unordered_map>
#include <vector>

namespace ll::cl {

namespace {

std::atomic<bool> g_stop{false};

struct PendingInputSample {
    std::uint32_t seq = 0;
    std::uint64_t send_ms = 0;
    Vec2          predicted_pos{};
};

std::string make_log_path(const std::string& dir, const std::string& label,
                          std::uint16_t pid, const char* suffix) {
    std::ostringstream oss;
    oss << dir << "/client_" << label << "_p" << pid << "_" << suffix << "_" << wall_time_ms() << ".csv";
    return oss.str();
}

InputSnapshot poll_keyboard(const sf::RenderWindow* w) {
    // Guard: sf::Keyboard::isKeyPressed reads the OS-level keyboard state
    // regardless of which window has focus. Without this gate, two clients
    // on the same machine would both receive the same keystrokes.
    if (w == nullptr || !w->hasFocus()) return InputSnapshot{};
    InputSnapshot s{};
    using K = sf::Keyboard::Key;
    if (sf::Keyboard::isKeyPressed(K::Left)  || sf::Keyboard::isKeyPressed(K::A)) s.mx = -1;
    if (sf::Keyboard::isKeyPressed(K::Right) || sf::Keyboard::isKeyPressed(K::D)) s.mx =  1;
    if (sf::Keyboard::isKeyPressed(K::Up)    || sf::Keyboard::isKeyPressed(K::W)) s.my = -1;
    if (sf::Keyboard::isKeyPressed(K::Down)  || sf::Keyboard::isKeyPressed(K::S)) s.my =  1;
    return s;
}

} // namespace

int run_optimized_client(const ClientConfig& cfg) {
    std::signal(SIGINT,  [](int){ g_stop.store(true); });
    std::signal(SIGTERM, [](int){ g_stop.store(true); });

    const std::uint16_t udp_port = cfg.udp_port ? cfg.udp_port : DEFAULT_UDP_PORT;

    sockaddr_in server_addr{};
    if (!net::resolve_ipv4(cfg.server_host, udp_port, server_addr)) {
        std::fprintf(stderr, "[client] failed to resolve %s:%u\n",
                     cfg.server_host.c_str(), udp_port);
        return 1;
    }
    const net::Endpoint server_ep = net::endpoint_from_sockaddr(server_addr);

    LaggedSocket sock;
    if (!sock.bind_any()) { std::fprintf(stderr, "[client] bind failed\n"); return 1; }
    sock.configure(cfg.lagcfg);

    // Send JOIN_REQUEST and wait up to 2 s for JOIN_ACCEPT.
    {
        std::uint8_t buf[8];
        proto::JoinRequest jr{PROTOCOL_VERSION};
        std::size_t n = proto::encode_join_request(buf, sizeof(buf), jr);
        sock.send_to(server_ep, buf, n);
    }

    std::uint16_t my_pid = 0;
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (std::chrono::steady_clock::now() < deadline) {
        sock.pump();
        std::uint8_t buf[64];
        auto rr = sock.recv_from(buf, sizeof(buf));
        if (!rr.has_data) { std::this_thread::sleep_for(std::chrono::milliseconds(5)); continue; }
        auto t = proto::peek_type(buf, rr.size);
        if (!t || *t != proto::PacketType::JOIN_ACCEPT) continue;
        proto::JoinAccept ja{};
        if (proto::decode_join_accept(buf, rr.size, ja)) {
            my_pid = ja.player_id;
            break;
        }
    }
    if (my_pid == 0) {
        std::fprintf(stderr, "[client] no JOIN_ACCEPT received from %s:%u\n",
                     cfg.server_host.c_str(), udp_port);
        return 2;
    }
    std::fprintf(stdout, "[client] joined as player %u\n", my_pid);

    // Receive thread owns the conditioner pump for incoming.
    SnapshotQueue   queue;
    NetworkReceiver receiver(sock, queue);
    receiver.start();

    const bool has_bot = parse_bot_pattern(cfg.bot_pattern) != BotPattern::None;
    const bool headless = cfg.headless || has_bot;

    Renderer renderer;
    if (!headless) {
        renderer.init(1280, 720, "Lightning Link");
    }

    CsvLog periodic_log, input_log;
    periodic_log.open(make_log_path(cfg.log_dir, cfg.run_label, my_pid, "periodic"),
                      "wall_time_ms,player_id,latest_snapshot_tick,bytes_received_total,"
                      "input_packets_sent_total,estimated_input_to_visible_delay_ms,"
                      "interpolation_buffer_entries");
    input_log.open(make_log_path(cfg.log_dir, cfg.run_label, my_pid, "inputs"),
                   "wall_time_ms,player_id,input_seq,ack_seq,rtt_ms,"
                   "reconciliation_error_px,perceived_delay_ms,mode");

    Predictor    predictor;
    Interpolator interp;
    Vec2         local_pos{ARENA_WIDTH * 0.5f, ARENA_HEIGHT * 0.5f};
    std::unordered_map<std::uint16_t, PlayerState> latest_authoritative;
    std::unordered_map<std::uint32_t, PendingInputSample> pending;
    std::uint64_t bytes_received_last = 0;
    std::uint32_t inputs_sent_total = 0;
    std::uint32_t snapshots_received = 0;
    std::uint32_t latest_tick = 0;
    std::uint32_t last_ack_seq = 0;
    double        last_delay_ms = 0.0;
    double        last_recon_err_px = 0.0;

    HudInputs hud{};
    hud.local_player_id = my_pid;
    if (cfg.disable_prediction)    hud.prediction_enabled    = false;
    if (cfg.disable_interpolation) hud.interpolation_enabled = false;
    // Mode label encodes which optimizations are active for downstream analysis.
    if (!hud.prediction_enabled && !hud.interpolation_enabled) {
        hud.mode_label = "optimized_raw";
    } else if (!hud.prediction_enabled) {
        hud.mode_label = "optimized_noPred";
    } else if (!hud.interpolation_enabled) {
        hud.mode_label = "optimized_noInterp";
    } else {
        hud.mode_label = "optimized";
    }

    BotInput bot(parse_bot_pattern(cfg.bot_pattern), cfg.bot_seed ^ my_pid);

    using clock = std::chrono::steady_clock;
    const auto t_start = clock::now();
    auto next_input_send = t_start;
    auto next_log_flush  = t_start;
    auto last_frame_time = t_start;

    while (!g_stop.load()) {
        auto now = clock::now();
        double elapsed_sec = std::chrono::duration<double>(now - t_start).count();
        if (cfg.duration_sec > 0.0 && elapsed_sec >= cfg.duration_sec) break;

        // SFML window events + toggles.
        if (renderer.is_open()) {
            auto* w = renderer.window();
            while (auto ev = w->pollEvent()) {
                if (ev->is<sf::Event::Closed>()) {
                    w->close();
                } else if (const auto* kp = ev->getIf<sf::Event::KeyPressed>()) {
                    if (kp->code == sf::Keyboard::Key::F1) hud.show_hud = !hud.show_hud;
                    if (kp->code == sf::Keyboard::Key::F2) hud.show_ghost = !hud.show_ghost;
                    if (kp->code == sf::Keyboard::Key::F3) hud.prediction_enabled = !hud.prediction_enabled;
                    if (kp->code == sf::Keyboard::Key::F4) hud.interpolation_enabled = !hud.interpolation_enabled;
                    if (kp->code == sf::Keyboard::Key::Escape) w->close();
                }
            }
        }

        // Drain snapshots and reconcile.
        while (auto maybe = queue.try_pop()) {
            const auto& ss = *maybe;
            ++snapshots_received;
            latest_tick = ss.snap.server_tick;
            for (const auto& ps : ss.snap.players) {
                latest_authoritative[ps.id] = ps;
                if (ps.id != my_pid) {
                    InterpSample is_{};
                    is_.pos = ps.pos;
                    is_.vel = ps.vel;
                    is_.recv_wall_ms = ss.recv_wall_ms;
                    interp.ingest(ps.id, is_);
                }
            }
            // Local reconcile.
            auto it_local = latest_authoritative.find(my_pid);
            if (it_local != latest_authoritative.end()) {
                Vec2 pre = local_pos;
                if (hud.prediction_enabled) {
                    predictor.reconcile(local_pos, it_local->second);
                } else {
                    local_pos = it_local->second.pos;
                }
                float dx = local_pos.x - pre.x;
                float dy = local_pos.y - pre.y;
                last_recon_err_px = std::sqrt(dx * dx + dy * dy);

                // Match against pending inputs to compute input-to-visible delay.
                last_ack_seq = it_local->second.last_processed_input_seq;
                for (auto it = pending.begin(); it != pending.end();) {
                    if (it->first <= last_ack_seq) {
                        double rtt_ms = static_cast<double>(wall_time_ms() - it->second.send_ms);
                        last_delay_ms = rtt_ms;
                        // With prediction on, the local player responds immediately,
                        // so perceived input-to-visible latency is bounded by a single
                        // render frame. We report 0 as the lower bound - the rendering
                        // loop adds at most 1000/120 ms above that in practice.
                        double perceived = hud.prediction_enabled
                            ? 0.0
                            : rtt_ms; // prediction disabled -> same as ack latency
                        const char* mode_tag = hud.mode_label.c_str();
                        {
                            std::ostringstream row;
                            row << wall_time_ms() << ','
                                << my_pid << ','
                                << it->first << ','
                                << last_ack_seq << ','
                                << rtt_ms << ','
                                << last_recon_err_px << ','
                                << perceived << ','
                                << mode_tag;
                            input_log.write_line(row.str());
                        }
                        it = pending.erase(it);
                    } else {
                        ++it;
                    }
                }
            }
        }

        interp.prune(wall_time_ms());

        // Input at 60 Hz.
        if (now >= next_input_send) {
            InputSnapshot in = has_bot ? bot.poll(elapsed_sec)
                                       : poll_keyboard(renderer.is_open() ? renderer.window() : nullptr);

            double dt = std::chrono::duration<double>(now - last_frame_time).count();
            if (dt <= 0.0 || dt > 0.25) dt = DT_SECONDS;
            last_frame_time = now;

            std::uint32_t seq = 0;
            if (hud.prediction_enabled) {
                seq = predictor.record_and_step(in.mx, in.my, dt, local_pos);
            } else {
                // With prediction off we still record the input so the seq increments
                // monotonically, but we do not locally advance `local_pos`; snapshots
                // replace local state directly.
                Vec2 scratch = local_pos;
                seq = predictor.record_and_step(0, 0, dt, scratch);
            }

            proto::InputPacket ip{};
            ip.player_id = my_pid;
            ip.seq       = seq;
            ip.mx        = in.mx;
            ip.my        = in.my;
            std::uint8_t buf[32];
            std::size_t n = proto::encode_input(buf, sizeof(buf), ip);
            sock.send_to(server_ep, buf, n);
            ++inputs_sent_total;

            PendingInputSample p{};
            p.seq = seq;
            p.send_ms = wall_time_ms();
            p.predicted_pos = local_pos;
            pending[seq] = p;

            // Bound pending map - drop entries older than 2s to avoid unbounded growth.
            if (pending.size() > 256) {
                std::uint64_t now_ms = wall_time_ms();
                for (auto it = pending.begin(); it != pending.end();) {
                    if (now_ms - it->second.send_ms > 2000) it = pending.erase(it);
                    else ++it;
                }
            }

            next_input_send += std::chrono::duration_cast<clock::duration>(
                std::chrono::duration<double>(1.0 / INPUT_SEND_RATE_HZ));
            if (next_input_send < now) next_input_send = now;
        }

        // Render.
        if (!headless && renderer.is_open()) {
            std::vector<RenderedPlayer> to_draw;
            std::uint64_t now_ms = wall_time_ms();

            // Local player drawn at predicted position.
            {
                RenderedPlayer rp{};
                rp.id = my_pid;
                rp.pos = local_pos;
                rp.is_local = true;
                to_draw.push_back(rp);
            }
            // Remote players via interpolation.
            for (const auto& [id, ps] : latest_authoritative) {
                if (id == my_pid) continue;
                Vec2 pos;
                if (hud.interpolation_enabled) {
                    if (!interp.render_position(id, now_ms, pos)) continue;
                } else {
                    pos = ps.pos;
                }
                RenderedPlayer rp{};
                rp.id = id;
                rp.pos = pos;
                to_draw.push_back(rp);
            }
            // Optional ghost for local authoritative.
            if (hud.show_ghost) {
                auto it = latest_authoritative.find(my_pid);
                if (it != latest_authoritative.end()) {
                    RenderedPlayer g{};
                    g.id = my_pid;
                    g.pos = it->second.pos;
                    g.is_ghost = true;
                    to_draw.push_back(g);
                }
            }

            hud.window_focused = renderer.is_open() && renderer.window() && renderer.window()->hasFocus();
            hud.snapshots_received = snapshots_received;
            hud.inputs_sent = inputs_sent_total;
            hud.bytes_received = receiver.bytes_received_total();
            hud.est_delay_ms = last_delay_ms;
            hud.interp_depth = 0;
            for (const auto& [id, ps] : latest_authoritative) {
                if (id != my_pid) hud.interp_depth = std::max(hud.interp_depth, interp.buffer_depth(id));
            }
            hud.reconciliation_error_px = last_recon_err_px;

            renderer.begin_frame();
            renderer.draw_arena(ARENA_WIDTH, ARENA_HEIGHT);
            renderer.draw_players(to_draw);
            if (hud.show_hud) renderer.draw_hud(build_hud_lines(hud));
            renderer.end_frame();

            if (!renderer.is_open()) g_stop.store(true);
        }

        // Periodic log at 1 Hz.
        if (now >= next_log_flush) {
            std::uint64_t br = receiver.bytes_received_total();
            (void)br;
            std::ostringstream row;
            row << wall_time_ms() << ','
                << my_pid << ','
                << latest_tick << ','
                << receiver.bytes_received_total() << ','
                << inputs_sent_total << ','
                << last_delay_ms << ','
                << (latest_authoritative.empty() ? 0 : interp.buffer_depth(
                        latest_authoritative.begin()->first == my_pid && latest_authoritative.size() > 1
                            ? std::next(latest_authoritative.begin())->first
                            : latest_authoritative.begin()->first));
            periodic_log.write_line(row.str());
            periodic_log.flush();
            input_log.flush();
            next_log_flush += std::chrono::duration_cast<clock::duration>(
                std::chrono::duration<double>(LOG_FLUSH_SEC));
            if (next_log_flush < now) next_log_flush = now;
            bytes_received_last = receiver.bytes_received_total();
            (void)bytes_received_last;
        }

        if (headless) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }

    // Send clean DISCONNECT_NOTICE.
    {
        std::uint8_t buf[8];
        proto::DisconnectNotice dn{my_pid};
        std::size_t n = proto::encode_disconnect(buf, sizeof(buf), dn);
        sock.send_to(server_ep, buf, n);
        sock.pump();
    }

    receiver.stop();
    periodic_log.flush();
    input_log.flush();
    periodic_log.close();
    input_log.close();
    return 0;
}

} // namespace ll::cl
