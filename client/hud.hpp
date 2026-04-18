#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace ll::cl {

struct HudInputs {
    bool          show_hud          = true;
    bool          show_ghost        = false;
    std::uint16_t local_player_id   = 0;
    std::uint32_t snapshots_received = 0;
    std::uint32_t inputs_sent       = 0;
    std::uint64_t bytes_received    = 0;
    double        est_delay_ms      = 0.0;
    std::size_t   interp_depth      = 0;
    double        reconciliation_error_px = 0.0;
    std::string   mode_label        = "optimized";
    bool          prediction_enabled = true;
    bool          interpolation_enabled = true;
};

std::vector<std::string> build_hud_lines(const HudInputs& h);

} // namespace ll::cl
