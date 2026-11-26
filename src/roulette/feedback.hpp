// SPDX-License-Identifier: MIT
//
// Weapon Roulette: the lines the mod shows in the game's HUD, as text and a style; the mod turns
// them into openrac_hud_text. Kept short: the HUD takes 63 characters a line, and the game's font
// draws capitals, digits and most punctuation (not & * = [ ] { } ~; # % @ are button icons).

#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "rotation.hpp"

namespace weapon_roulette {

// How a line looks.
enum class LineStyle {
    Notice,    // The rotation's announcement: large, bright.
    Progress,  // The kill count or the last seconds: small, dimmer.
};

struct HudLine {
    std::string text;
    LineStyle style;

    bool operator==(const HudLine& other) const {
        return text == other.text && style == other.style;
    }
};

// The longest line the HUD takes (openrac_hud_text.text is 64 bytes with its ending 0).
inline constexpr std::size_t kMaxLineLength = 63;

// How long the rotation's notice stays up, in seconds.
inline constexpr int kNoticeSeconds = 3;

// From how many seconds before a time rotation the countdown shows.
inline constexpr int kCountdownSeconds = 5;

// "WEAPON ROTATED: PLASMA COIL".
std::string rotated_text(std::string_view weapon);
// "WEAPON ROTATION: 2 / 3 KILLS".
std::string kills_text(int kills, int needed);
// "NEXT WEAPON IN 3".
std::string countdown_text(int seconds);

// The HUD's state: the notice and its time left.
class Feedback {
public:
    // The rotation put `weapon` in Ratchet's hand: show it for kNoticeSeconds.
    void announce(std::string_view weapon, int ticks_per_second);

    // One game tick: the notice's time runs down.
    void tick();

    // A new level load: the notice goes with the level.
    void clear() { m_notice_ticks = 0; }

    // The lines to show now: none with notifications off; the notice while it lasts; in play, the
    // kill count (kill mode) or the last seconds (time mode, and not while the notice is up).
    std::vector<HudLine> lines(const RotationClock& clock, bool in_play, bool enabled) const;

private:
    std::string m_notice;
    int m_notice_ticks = 0;
};

}  // namespace weapon_roulette
