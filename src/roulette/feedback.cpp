// SPDX-License-Identifier: MIT
//
// Weapon Roulette: the HUD's lines (feedback.hpp).

#include "feedback.hpp"

#include <cctype>

namespace weapon_roulette {
namespace {

// Upper case and only what the game's font draws (printable ASCII less the glyphs it lacks and the
// three it draws as button icons), cut to the HUD's length.
std::string hud_safe(std::string_view text) {
    std::string out;
    for (char c : text) {
        const auto u = static_cast<unsigned char>(c);
        if (u < 32 || u > 126) {
            continue;
        }
        switch (c) {
            case '&': case '*': case '=': case '[': case ']': case '{': case '}': case '~':
            case '#': case '%': case '@':
                continue;
            default:
                break;
        }
        out.push_back(static_cast<char>(std::toupper(u)));
        if (out.size() == kMaxLineLength) {
            break;
        }
    }
    return out;
}

}  // namespace

std::string rotated_text(std::string_view weapon) {
    return hud_safe("Weapon rotated: " + std::string(weapon));
}

std::string kills_text(int kills, int needed) {
    return hud_safe("Weapon rotation: " + std::to_string(kills) + " / " + std::to_string(needed)
                    + (needed == 1 ? " kill" : " kills"));
}

std::string countdown_text(int seconds) {
    return hud_safe("Next weapon in " + std::to_string(seconds));
}

void Feedback::announce(std::string_view weapon, int ticks_per_second) {
    m_notice = rotated_text(weapon);
    m_notice_ticks = kNoticeSeconds * (ticks_per_second > 0 ? ticks_per_second : 60);
}

void Feedback::tick() {
    if (m_notice_ticks > 0) {
        --m_notice_ticks;
    }
}

std::vector<HudLine> Feedback::lines(const RotationClock& clock, bool in_play, bool enabled) const {
    std::vector<HudLine> out;
    if (!enabled) {
        return out;
    }
    if (m_notice_ticks > 0) {
        out.push_back({m_notice, LineStyle::Notice});
    }
    if (!in_play) {
        return out;
    }
    if (clock.mode() == Mode::Kills) {
        out.push_back({kills_text(clock.kills(), clock.kills_needed()), LineStyle::Progress});
    } else if (m_notice_ticks == 0) {
        // Not under the notice: with a short interval the two would crowd the screen.
        const int left = clock.seconds_left();
        if (left > 0 && left <= kCountdownSeconds) {
            out.push_back({countdown_text(left), LineStyle::Progress});
        }
    }
    return out;
}

}  // namespace weapon_roulette
