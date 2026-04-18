#pragma once

#include "common/types.hpp"

#include <SFML/Graphics.hpp>

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace ll::cl {

struct RenderedPlayer {
    std::uint16_t id   = 0;
    Vec2          pos  = {};
    bool          is_local = false;
    bool          is_ghost = false; // authoritative marker (debug overlay)
};

class Renderer {
public:
    // Initialize the window. Returns true on success.
    bool init(unsigned w, unsigned h, const std::string& title);

    bool is_open() const { return window_ && window_->isOpen(); }

    // Main render pass.
    void begin_frame();
    void draw_arena(float w, float h);
    void draw_players(const std::vector<RenderedPlayer>& players);
    void draw_hud(const std::vector<std::string>& hud_lines);
    void end_frame();

    sf::RenderWindow* window() { return window_.get(); }

    // Assign a unique color to a player id (deterministic palette).
    static sf::Color color_for(std::uint16_t id);

private:
    std::unique_ptr<sf::RenderWindow> window_;
    sf::Font                          font_;
    bool                              font_loaded_ = false;
};

} // namespace ll::cl
