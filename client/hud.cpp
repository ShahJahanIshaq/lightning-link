#include "client/hud.hpp"

#include <sstream>

namespace ll::cl {

std::vector<std::string> build_hud_lines(const HudInputs& h) {
    std::vector<std::string> out;
    auto line = [&](const std::string& s){ out.push_back(s); };

    {
        std::ostringstream o;
        o << "Lightning Link  |  mode=" << h.mode_label
          << "  pid=" << h.local_player_id;
        line(o.str());
    }
    {
        std::ostringstream o;
        o << "pred=" << (h.prediction_enabled ? "on" : "OFF")
          << "  interp=" << (h.interpolation_enabled ? "on" : "OFF")
          << "  ghost=" << (h.show_ghost ? "on" : "off")
          << "  (F1 hud, F2 ghost, F3 pred, F4 interp)";
        line(o.str());
    }
    {
        std::ostringstream o;
        o << "snap rx: " << h.snapshots_received
          << "  bytes rx: " << h.bytes_received
          << "  inputs tx: " << h.inputs_sent;
        line(o.str());
    }
    {
        std::ostringstream o;
        o.precision(1);
        o << std::fixed;
        o << "input->visible: " << h.est_delay_ms << " ms"
          << "   reconciliation err: " << h.reconciliation_error_px << " px"
          << "   interp depth: " << h.interp_depth;
        line(o.str());
    }
    return out;
}

} // namespace ll::cl
