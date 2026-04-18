#pragma once

#include "common/lagged_socket.hpp"

#include <cstdint>
#include <string>

namespace ll::cl {

struct ClientConfig {
    std::string server_host = "127.0.0.1";
    std::uint16_t udp_port  = 0;          // 0 = default
    std::uint16_t tcp_port  = 0;          // 0 = default
    std::string log_dir     = "logs";
    std::string run_label   = "default";
    std::string mode        = "optimized";  // "optimized" | "baseline"
    std::string bot_pattern = "none";        // triggers headless mode when != "none"
    std::uint64_t bot_seed  = 0xBADC0DEULL;
    double duration_sec     = 0.0;            // 0 = run until window closed or signal
    bool  headless          = false;          // force headless (no window) regardless of bot
    LaggedSocketConfig lagcfg{};
};

int run_optimized_client(const ClientConfig& cfg);
int run_baseline_client(const ClientConfig& cfg);

} // namespace ll::cl
