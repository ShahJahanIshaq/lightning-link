#include "client/client.hpp"
#include "common/constants.hpp"

#include <cstdio>
#include <cstdlib>
#include <string>

using namespace ll;
using namespace ll::cl;

static void usage(const char* argv0) {
    std::fprintf(stderr,
        "Usage: %s [--server=HOST] [--port=N] [--tcp-port=N] [--mode=optimized|baseline]\n"
        "          [--run-label=STR] [--log-dir=PATH] [--duration=SECONDS]\n"
        "          [--bot=circle|sine|random|zigzag] [--bot-seed=N] [--headless]\n"
        "          [--add-latency=MS] [--jitter=MS] [--loss=PCT] [--seed=N]\n",
        argv0);
}

static bool parse_kv(const std::string& a, const char* key, std::string& out) {
    std::string prefix = std::string("--") + key + "=";
    if (a.rfind(prefix, 0) == 0) { out = a.substr(prefix.size()); return true; }
    return false;
}

int main(int argc, char** argv) {
    ClientConfig cfg{};
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        std::string v;
        if (parse_kv(a, "server", v))      cfg.server_host = v;
        else if (parse_kv(a, "port", v))       cfg.udp_port = static_cast<std::uint16_t>(std::stoi(v));
        else if (parse_kv(a, "tcp-port", v))   cfg.tcp_port = static_cast<std::uint16_t>(std::stoi(v));
        else if (parse_kv(a, "mode", v))       cfg.mode = v;
        else if (parse_kv(a, "run-label", v))  cfg.run_label = v;
        else if (parse_kv(a, "log-dir", v))    cfg.log_dir = v;
        else if (parse_kv(a, "duration", v))   cfg.duration_sec = std::stod(v);
        else if (parse_kv(a, "bot", v))        cfg.bot_pattern = v;
        else if (parse_kv(a, "bot-seed", v))   cfg.bot_seed = std::stoull(v);
        else if (a == "--headless")            cfg.headless = true;
        else if (parse_kv(a, "add-latency", v)) {
            int ms = std::stoi(v);
            cfg.lagcfg.add_latency_ms_send = ms / 2;
            cfg.lagcfg.add_latency_ms_recv = ms - (ms / 2);
        }
        else if (parse_kv(a, "jitter", v))     cfg.lagcfg.jitter_ms = std::stoi(v);
        else if (parse_kv(a, "loss", v))       cfg.lagcfg.loss_prob = std::stod(v) / 100.0;
        else if (parse_kv(a, "seed", v))       cfg.lagcfg.seed = std::stoull(v);
        else if (a == "--help" || a == "-h")   { usage(argv[0]); return 0; }
        else { std::fprintf(stderr, "unknown arg: %s\n", a.c_str()); usage(argv[0]); return 2; }
    }

    if (cfg.mode == "baseline") return run_baseline_client(cfg);
    return run_optimized_client(cfg);
}
