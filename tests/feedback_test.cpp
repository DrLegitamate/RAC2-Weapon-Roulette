// SPDX-License-Identifier: MIT
//
// The HUD's lines (src/roulette/feedback.hpp).

#include <string>

#include "check.hpp"
#include "roulette/feedback.hpp"

namespace {

using namespace weapon_roulette;

void test_texts() {
    CHECK(rotated_text("Plasma Coil") == "WEAPON ROTATED: PLASMA COIL");
    CHECK(rotated_text("Bouncer") == "WEAPON ROTATED: BOUNCER");
    CHECK(kills_text(2, 3) == "WEAPON ROTATION: 2 / 3 KILLS");
    CHECK(kills_text(0, 1) == "WEAPON ROTATION: 0 / 1 KILL");
    CHECK(countdown_text(3) == "NEXT WEAPON IN 3");
}

void test_texts_fit_the_font_and_the_line() {
    // Characters the font lacks or draws as icons go; the line is cut to 63.
    CHECK(rotated_text("A&B*C=D[E]F{G}H~I#J%K@L") == "WEAPON ROTATED: ABCDEFGHIJKL");
    const std::string long_name(200, 'x');
    CHECK(rotated_text(long_name).size() == kMaxLineLength);
    CHECK(kills_text(1000, 1000).size() <= kMaxLineLength);
}

void test_notifications_off_shows_nothing() {
    Feedback fb;
    RotationConfig c;
    c.mode = Mode::Kills;
    RotationClock clock(c);
    fb.announce("Lancer", 60);
    CHECK(fb.lines(clock, true, false).empty());
}

void test_notice_lasts_three_seconds() {
    Feedback fb;
    RotationClock clock;  // Time mode, 30 s: no countdown yet.
    fb.announce("Lancer", 60);
    for (int t = 0; t < kNoticeSeconds * 60; ++t) {
        const auto lines = fb.lines(clock, true, true);
        CHECK(lines.size() == 1);
        CHECK(lines[0].text == "WEAPON ROTATED: LANCER");
        CHECK(lines[0].style == LineStyle::Notice);
        fb.tick();
    }
    CHECK(fb.lines(clock, true, true).empty());
}

void test_level_change_clears_notice() {
    Feedback fb;
    RotationClock clock;
    fb.announce("Lancer", 50);
    fb.clear();
    CHECK(fb.lines(clock, true, true).empty());
}

void test_kill_progress_shown_in_play() {
    Feedback fb;
    RotationConfig c;
    c.mode = Mode::Kills;
    c.kills_per_rotation = 3;
    RotationClock clock(c);
    clock.add_kill();
    clock.add_kill();
    auto lines = fb.lines(clock, true, true);
    CHECK(lines.size() == 1);
    CHECK(lines[0].text == "WEAPON ROTATION: 2 / 3 KILLS");
    CHECK(lines[0].style == LineStyle::Progress);
    CHECK(fb.lines(clock, false, true).empty());  // Not over a menu or a scene, not as Clank.
}

void test_countdown_only_in_the_last_seconds() {
    Feedback fb;
    RotationConfig c;
    c.interval_seconds = 10;
    RotationClock clock(c);
    for (int t = 0; t < 5 * 60 - 1; ++t) {
        clock.tick(true, 1);
    }
    CHECK(fb.lines(clock, true, true).empty());  // A tick over 5 s left: no countdown yet.
    clock.tick(true, 1);                         // 5 s left exactly: it shows 5.
    auto lines = fb.lines(clock, true, true);
    CHECK(lines.size() == 1 && lines[0].text == "NEXT WEAPON IN 5");
    for (int t = 0; t < 4 * 60; ++t) {
        clock.tick(true, 1);
    }
    lines = fb.lines(clock, true, true);
    CHECK(lines.size() == 1 && lines[0].text == "NEXT WEAPON IN 1");
}

void test_no_countdown_under_the_notice() {
    Feedback fb;
    RotationConfig c;
    c.interval_seconds = 4;  // Shorter than the countdown's window: it would show at once.
    RotationClock clock(c);
    fb.announce("Lancer", 60);
    auto lines = fb.lines(clock, true, true);
    CHECK(lines.size() == 1 && lines[0].text == "WEAPON ROTATED: LANCER");
    for (int t = 0; t < kNoticeSeconds * 60; ++t) {
        fb.tick();
    }
    lines = fb.lines(clock, true, true);
    CHECK(lines.size() == 1 && lines[0].text == "NEXT WEAPON IN 4");
}

void test_notice_and_progress_together() {
    Feedback fb;
    RotationConfig c;
    c.mode = Mode::Kills;
    RotationClock clock(c);
    fb.announce("RYNO II", 60);
    const auto lines = fb.lines(clock, true, true);
    CHECK(lines.size() == 2);
    CHECK(lines[0].text == "WEAPON ROTATED: RYNO II");
    CHECK(lines[1].text == "WEAPON ROTATION: 0 / 3 KILLS");
}

}  // namespace

int main() {
    RUN(test_texts);
    RUN(test_texts_fit_the_font_and_the_line);
    RUN(test_notifications_off_shows_nothing);
    RUN(test_notice_lasts_three_seconds);
    RUN(test_level_change_clears_notice);
    RUN(test_kill_progress_shown_in_play);
    RUN(test_countdown_only_in_the_last_seconds);
    RUN(test_no_countdown_under_the_notice);
    RUN(test_notice_and_progress_together);
    return 0;
}
