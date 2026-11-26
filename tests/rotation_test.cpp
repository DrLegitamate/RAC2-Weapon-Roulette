// SPDX-License-Identifier: MIT
//
// The rotation clock (src/roulette/rotation.hpp): time in game ticks while in play, kills as
// counted, and the restarts on a new level load.

#include <cstdint>
#include <initializer_list>

#include "check.hpp"
#include "roulette/rotation.hpp"

namespace {

using namespace weapon_roulette;

RotationConfig time_config(int seconds, int rate = 60) {
    RotationConfig c;
    c.mode = Mode::Time;
    c.interval_seconds = seconds;
    c.ticks_per_second = rate;
    return c;
}

RotationConfig kill_config(int kills) {
    RotationConfig c;
    c.mode = Mode::Kills;
    c.kills_per_rotation = kills;
    return c;
}

// Runs `n` ticks in play (or not) in level load `loads`.
void run(RotationClock& clock, std::int64_t n, bool in_play = true, std::uint32_t loads = 1) {
    for (std::int64_t i = 0; i < n; ++i) {
        clock.tick(in_play, loads);
    }
}

// ---- time ----

void test_rotates_after_exactly_the_interval() {
    RotationClock clock(time_config(30));
    run(clock, 30 * 60 - 1);
    CHECK(!clock.due());
    CHECK(clock.seconds_left() == 1);
    run(clock, 1);
    CHECK(clock.due());
    CHECK(clock.seconds_left() == 0);
}

void test_pal_counts_fifty_ticks_a_second() {
    RotationClock clock(time_config(30, 50));
    run(clock, 30 * 50 - 1);
    CHECK(!clock.due());
    run(clock, 1);
    CHECK(clock.due());
}

void test_no_rotation_before_the_interval() {
    RotationClock clock(time_config(45));
    for (int t = 0; t < 45 * 60 - 1; ++t) {
        clock.tick(true, 1);
        CHECK(!clock.due());
    }
}

void test_repeated_rotations() {
    RotationClock clock(time_config(15));
    for (int round = 0; round < 5; ++round) {
        run(clock, 15 * 60 - 1);
        CHECK(!clock.due());
        run(clock, 1);
        CHECK(clock.due());
        clock.rotated();
        CHECK(!clock.due());
        CHECK(clock.ticks() == 0);
    }
}

void test_custom_intervals() {
    for (int seconds : {1, 15, 45, 75, 120, 180, 3600}) {
        RotationClock clock(time_config(seconds));
        CHECK(clock.ticks_needed() == static_cast<std::int64_t>(seconds) * 60);
        run(clock, clock.ticks_needed() - 1);
        CHECK(!clock.due());
        run(clock, 1);
        CHECK(clock.due());
    }
}

void test_due_holds_until_rotated() {
    RotationClock clock(time_config(1));
    run(clock, 60);
    CHECK(clock.due());
    run(clock, 500);  // The equip could not happen yet: still due, the count does not run on.
    CHECK(clock.due());
    CHECK(clock.ticks() == 60);
    clock.rotated();
    CHECK(!clock.due());
}

void test_timer_pauses_outside_play() {
    RotationClock clock(time_config(10));
    run(clock, 5 * 60);
    // A menu, a vendor, a cutscene, a load: phase not PLAYING, the same level.
    run(clock, 100 * 60, false);
    CHECK(clock.ticks() == 5 * 60);
    CHECK(!clock.due());
    CHECK(clock.seconds_left() == 5);
}

void test_timer_resumes_after_pause() {
    RotationClock clock(time_config(10));
    run(clock, 4 * 60);
    run(clock, 30 * 60, false);
    run(clock, 6 * 60 - 1);
    CHECK(!clock.due());
    run(clock, 1);
    CHECK(clock.due());
}

void test_level_change_resets_timer() {
    RotationClock clock(time_config(10));
    run(clock, 9 * 60, true, 1);
    CHECK(clock.ticks() == 9 * 60);
    clock.tick(true, 2);  // A new level load (or the level made again after a death).
    CHECK(clock.ticks() == 1);
    run(clock, 10 * 60 - 2, true, 2);
    CHECK(!clock.due());
    run(clock, 1, true, 2);
    CHECK(clock.due());
}

void test_level_change_clears_a_due_rotation() {
    RotationClock clock(time_config(1));
    run(clock, 60, true, 7);
    CHECK(clock.due());
    CHECK(clock.sync_load(8));
    CHECK(!clock.due());
    CHECK(!clock.sync_load(8));
}

void test_first_load_seen_is_not_a_change_of_progress() {
    RotationClock clock(time_config(10));
    CHECK(clock.sync_load(42));  // The first load seen starts the count (at 0 already).
    CHECK(clock.ticks() == 0);
}

// ---- kills ----

void test_rotates_after_exactly_the_kill_count() {
    RotationClock clock(kill_config(3));
    clock.sync_load(1);
    clock.add_kill();
    clock.add_kill();
    CHECK(!clock.due());
    CHECK(clock.kills() == 2);
    clock.add_kill();
    CHECK(clock.due());
}

void test_every_kill_with_threshold_one() {
    RotationClock clock(kill_config(1));
    clock.add_kill();
    CHECK(clock.due());
    clock.rotated();
    CHECK(!clock.due());
    clock.add_kill();
    CHECK(clock.due());
}

void test_kill_counter_resets_after_rotation() {
    RotationClock clock(kill_config(5));
    for (int i = 0; i < 5; ++i) {
        clock.add_kill();
    }
    CHECK(clock.due());
    clock.add_kill();  // More kills before the rotation happens do not carry over.
    CHECK(clock.kills() == 5);
    clock.rotated();
    CHECK(clock.kills() == 0);
    CHECK(!clock.due());
}

void test_time_does_not_count_in_kill_mode() {
    RotationClock clock(kill_config(3));
    run(clock, 100000);
    CHECK(!clock.due());
    CHECK(clock.ticks() == 0);
}

void test_kills_do_not_count_in_time_mode() {
    RotationClock clock(time_config(30));
    for (int i = 0; i < 100; ++i) {
        clock.add_kill();
    }
    CHECK(clock.kills() == 0);
    CHECK(!clock.due());
}

void test_level_change_resets_kills() {
    RotationClock clock(kill_config(3));
    clock.sync_load(1);
    clock.add_kill();
    clock.add_kill();
    clock.tick(true, 2);
    CHECK(clock.kills() == 0);
    clock.add_kill();
    CHECK(!clock.due());
}

// ---- settings ----

void test_limits_are_kept() {
    RotationConfig c;
    c.interval_seconds = 0;
    c.kills_per_rotation = -5;
    c.ticks_per_second = 0;
    c = sanitised(c);
    CHECK(c.interval_seconds == kMinSeconds);
    CHECK(c.kills_per_rotation == kMinKills);
    CHECK(c.ticks_per_second == 60);

    c.interval_seconds = 1000000;
    c.kills_per_rotation = 1000000;
    c = sanitised(c);
    CHECK(c.interval_seconds == kMaxSeconds);
    CHECK(c.kills_per_rotation == kMaxKills);
}

void test_switching_mode_keeps_both_numbers() {
    RotationConfig c;
    c.interval_seconds = 75;
    c.kills_per_rotation = 7;
    c.mode = Mode::Kills;
    RotationClock clock(c);
    CHECK(clock.kills_needed() == 7);
    c.mode = Mode::Time;
    clock.configure(c);
    CHECK(clock.ticks_needed() == 75 * 60);
    CHECK(clock.kills_needed() == 7);
}

void test_reconfigure_starts_again() {
    RotationClock clock(time_config(10));
    run(clock, 9 * 60);
    clock.configure(time_config(20));
    CHECK(clock.ticks() == 0);
}

}  // namespace

int main() {
    RUN(test_rotates_after_exactly_the_interval);
    RUN(test_pal_counts_fifty_ticks_a_second);
    RUN(test_no_rotation_before_the_interval);
    RUN(test_repeated_rotations);
    RUN(test_custom_intervals);
    RUN(test_due_holds_until_rotated);
    RUN(test_timer_pauses_outside_play);
    RUN(test_timer_resumes_after_pause);
    RUN(test_level_change_resets_timer);
    RUN(test_level_change_clears_a_due_rotation);
    RUN(test_first_load_seen_is_not_a_change_of_progress);
    RUN(test_rotates_after_exactly_the_kill_count);
    RUN(test_every_kill_with_threshold_one);
    RUN(test_kill_counter_resets_after_rotation);
    RUN(test_time_does_not_count_in_kill_mode);
    RUN(test_kills_do_not_count_in_time_mode);
    RUN(test_level_change_resets_kills);
    RUN(test_limits_are_kept);
    RUN(test_switching_mode_keeps_both_numbers);
    RUN(test_reconfigure_starts_again);
    return 0;
}
