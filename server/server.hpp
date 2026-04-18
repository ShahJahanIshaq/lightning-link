#pragma once

#include "common/lagged_socket.hpp"
#include "common/logging.hpp"
#include "server/session_manager.hpp"
#include "server/udp_snapshot_broadcaster.hpp"
#include "server/world_state.hpp"

#include <atomic>
#include <string>

namespace ll::srv {

struct ServerConfig {
    std::string   log_dir        = "logs";
    std::string   run_label      = "default";
    std::uint16_t udp_port       = DEFAULT_UDP_PORT;
    std::uint16_t tcp_port       = DEFAULT_TCP_PORT;
    double        duration_sec   = 0.0;           // 0 = run forever
    std::string   mode           = "optimized";   // "optimized" | "baseline" | "both"
    LaggedSocketConfig lagcfg{};
};

class Server {
public:
    explicit Server(const ServerConfig& cfg);
    ~Server();

    // Runs until SIGINT or duration_sec elapses. Returns exit status.
    int run();

    // Signal handler hook.
    static void request_stop();

    static std::atomic<bool> stop_;

private:
    void do_periodic_log(double now_sec);
    void handle_inbound_udp();
    void tick_and_maybe_broadcast();

    ServerConfig          cfg_;
    LaggedSocket          sock_;
    WorldState            world_;
    SessionManager        sessions_;
    UdpSnapshotBroadcaster broadcaster_;
    CsvLog                log_;

    std::uint64_t         inputs_received_total_ = 0;
    std::uint64_t         last_log_ms_           = 0;
};

// Implemented in tcp_baseline.cpp so baseline mode stays isolated.
int run_baseline_server(const ServerConfig& cfg);

} // namespace ll::srv
