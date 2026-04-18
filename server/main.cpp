#include "server/server.hpp"
#include "common/constants.hpp"

#include <atomic>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

using namespace ll;
using namespace ll::srv;

static void usage(const char* argv0) {
    std::fprintf(stderr,
        "Usage: %s [--mode=optimized|baseline|both] [--port=N] [--tcp-port=N]\n"
        "          [--duration=SECONDS] [--run-label=STR] [--log-dir=PATH]\n"
        "          [--add-latency=MS] [--jitter=MS] [--loss=PCT] [--seed=N]\n",
        argv0);
}

static bool parse_kv(const std::string& a, const char* key, std::string& out) {
    std::string prefix = std::string("--") + key + "=";
    if (a.rfind(prefix, 0) == 0) {
        out = a.substr(prefix.size());
        return true;
    }
    return false;
}

int main(int argc, char** argv) {
    ServerConfig cfg{};
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        std::string v;
        if (parse_kv(a, "mode", v))       cfg.mode = v;
        else if (parse_kv(a, "port", v))      cfg.udp_port = static_cast<std::uint16_t>(std::stoi(v));
        else if (parse_kv(a, "tcp-port", v))  cfg.tcp_port = static_cast<std::uint16_t>(std::stoi(v));
        else if (parse_kv(a, "duration", v))  cfg.duration_sec = std::stod(v);
        else if (parse_kv(a, "run-label", v)) cfg.run_label = v;
        else if (parse_kv(a, "log-dir", v))   cfg.log_dir = v;
        else if (parse_kv(a, "add-latency", v)) {
            int ms = std::stoi(v);
            cfg.lagcfg.add_latency_ms_send = ms / 2;
            cfg.lagcfg.add_latency_ms_recv = ms - (ms / 2);
        }
        else if (parse_kv(a, "jitter", v))    cfg.lagcfg.jitter_ms = std::stoi(v);
        else if (parse_kv(a, "loss", v))      cfg.lagcfg.loss_prob = std::stod(v) / 100.0;
        else if (parse_kv(a, "seed", v))      cfg.lagcfg.seed = std::stoull(v);
        else if (a == "--help" || a == "-h") { usage(argv[0]); return 0; }
        else { std::fprintf(stderr, "unknown arg: %s\n", a.c_str()); usage(argv[0]); return 2; }
    }

    std::signal(SIGINT,  [](int){ Server::stop_.store(true); });
    std::signal(SIGTERM, [](int){ Server::stop_.store(true); });

    if (cfg.mode == "baseline") {
        return run_baseline_server(cfg);
    }
    if (cfg.mode == "both") {
        std::fprintf(stderr, "[server] 'both' mode runs sequentially: optimized first, then baseline.\n");
        Server s(cfg);
        int rc = s.run();
        if (rc != 0) return rc;
        Server::stop_.store(false);
        return run_baseline_server(cfg);
    }
    Server s(cfg);
    return s.run();
}
