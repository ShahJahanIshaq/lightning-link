#include "client/renderer.hpp"

#include "common/constants.hpp"

#include <array>
#include <cstdio>

namespace ll::cl {

namespace {

// Candidate font paths; first that loads wins.
const std::array<const char*, 5> kFontCandidates = {
    "/System/Library/Fonts/Supplemental/Menlo.ttc",
    "/System/Library/Fonts/Supplemental/Helvetica.ttc",
    "/System/Library/Fonts/Supplemental/Arial.ttf",
    "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
    "/usr/share/fonts/TTF/DejaVuSans.ttf",
};

} // namespace

sf::Color Renderer::color_for(std::uint16_t id) {
    static const std::array<sf::Color, 8> palette = {
        sf::Color(230, 80, 80),    // red
        sf::Color(80, 180, 250),   // blue
        sf::Color(120, 220, 120),  // green
        sf::Color(250, 200, 80),   // amber
        sf::Color(200, 120, 230),  // violet
        sf::Color(80, 220, 220),   // teal
        sf::Color(255, 140, 90),   // coral
        sf::Color(180, 180, 180),  // grey
    };
    return palette[(id - 1) % palette.size()];
}

bool Renderer::init(unsigned w, unsigned h, const std::string& title) {
    window_ = std::make_unique<sf::RenderWindow>(
        sf::VideoMode({w, h}), title, sf::Style::Titlebar | sf::Style::Close);
    window_->setFramerateLimit(120);
    window_->setKeyRepeatEnabled(false);

    for (const char* p : kFontCandidates) {
        if (font_.openFromFile(p)) { font_loaded_ = true; break; }
    }
    if (!font_loaded_) {
        std::fprintf(stderr, "[client] warning: no system font loaded; HUD will be suppressed\n");
    }
    return true;
}

void Renderer::begin_frame() {
    window_->clear(sf::Color(22, 24, 28));
}

void Renderer::draw_arena(float w, float h) {
    sf::RectangleShape bg({w, h});
    bg.setPosition({0.f, 0.f});
    bg.setFillColor(sf::Color(32, 36, 42));
    bg.setOutlineColor(sf::Color(90, 90, 100));
    bg.setOutlineThickness(2.f);
    window_->draw(bg);
}

void Renderer::draw_players(const std::vector<RenderedPlayer>& players) {
    for (const auto& p : players) {
        sf::CircleShape c(PLAYER_RADIUS);
        c.setOrigin({PLAYER_RADIUS, PLAYER_RADIUS});
        c.setPosition({p.pos.x, p.pos.y});
        sf::Color col = color_for(p.id);
        if (p.is_ghost) {
            col.a = 110;
            c.setFillColor(col);
            c.setOutlineColor(sf::Color::White);
            c.setOutlineThickness(1.f);
        } else {
            c.setFillColor(col);
            if (p.is_local) {
                c.setOutlineColor(sf::Color::White);
                c.setOutlineThickness(2.f);
            }
        }
        window_->draw(c);

        if (font_loaded_) {
            sf::Text t(font_, std::to_string(p.id), 14);
            t.setFillColor(sf::Color(240, 240, 240));
            t.setPosition({p.pos.x - 5.f, p.pos.y - 8.f});
            window_->draw(t);
        }
    }
}

void Renderer::draw_hud(const std::vector<std::string>& lines) {
    if (!font_loaded_ || lines.empty()) return;
    float y = 8.f;
    for (const auto& s : lines) {
        sf::Text t(font_, s, 13);
        t.setFillColor(sf::Color(230, 230, 230));
        t.setPosition({8.f, y});
        window_->draw(t);
        y += 16.f;
    }
}

void Renderer::end_frame() {
    window_->display();
}

} // namespace ll::cl
