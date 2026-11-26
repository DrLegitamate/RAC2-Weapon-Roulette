// SPDX-License-Identifier: MIT
//
// The kill filter (src/roulette/kills.hpp), with the clock it feeds: the rules of SetDeathBits for
// what the game counts as an enemy kill, no double counts, and nothing while out of play.

#include <cstdint>
#include <initializer_list>

#include "check.hpp"
#include "roulette/kills.hpp"
#include "roulette/rotation.hpp"

namespace {

using namespace weapon_roulette;

// A placed enemy as SetDeathBits sees it.
DeathRecord enemy(std::uint32_t moby, std::int16_t id) {
    DeathRecord d;
    d.moby = moby;
    d.save_id = id;
    d.reward_flags = 0;
    d.has_class = true;
    d.class_kind = kEnemyKind;
    return d;
}

// A crate: it calls SetDeathBits (bolts, its save id marked) but its class is not an enemy's.
DeathRecord crate(std::uint32_t moby, std::int16_t id) {
    DeathRecord d = enemy(moby, id);
    d.class_kind = 2;
    return d;
}

void test_enemy_kill_counts() {
    KillFilter f;
    CHECK(f.judge(enemy(0x300000, 4), true) == KillVerdict::Counted);
}

void test_crates_do_not_count() {
    KillFilter f;
    CHECK(f.judge(crate(0x300100, 9), true) == KillVerdict::NotEnemy);
    CHECK(f.remembered() == 0);
}

void test_non_enemy_objects_do_not_count() {
    KillFilter f;
    DeathRecord no_class = enemy(0x300200, 3);
    no_class.has_class = false;
    CHECK(f.judge(no_class, true) == KillVerdict::NotEnemy);

    for (std::int16_t kind : {0, 1, 3, 4, 6, 7, -1}) {
        DeathRecord other = enemy(0x300300, 3);
        other.class_kind = kind;
        CHECK(f.judge(other, true) == KillVerdict::NotEnemy);
    }
}

void test_unrewarded_and_scripted_deaths_do_not_count() {
    KillFilter f;
    // No save id: a spawned moby the game gives no reward for (SetDeathBits returns at once).
    CHECK(f.judge(enemy(0x300400, -1), true) == KillVerdict::NoReward);
    // The no-reward bit.
    DeathRecord flagged = enemy(0x300500, 12);
    flagged.reward_flags = kNoRewardFlag;
    CHECK(f.judge(flagged, true) == KillVerdict::NoReward);
    flagged.reward_flags = 0x7F;  // The other bits do not matter.
    CHECK(f.judge(flagged, true) == KillVerdict::Counted);
    // No moby.
    CHECK(f.judge(DeathRecord{}, true) == KillVerdict::NoReward);
}

void test_non_kill_events_do_not_count() {
    // count_kill's type 1 is the bestiary's other column, not a kill.
    KillFilter f;
    DeathRecord event = enemy(0x300600, 5);
    event.event_type = 1;
    CHECK(f.judge(event, true) == KillVerdict::NotEnemy);
}

void test_duplicate_deaths_ignored() {
    KillFilter f;
    CHECK(f.judge(enemy(0x300700, 20), true) == KillVerdict::Counted);
    CHECK(f.judge(enemy(0x300700, 20), true) == KillVerdict::Duplicate);
    CHECK(f.judge(enemy(0x300700, 20), true) == KillVerdict::Duplicate);
    // Another enemy, and a copy of the first at another address, each count once.
    CHECK(f.judge(enemy(0x300800, 21), true) == KillVerdict::Counted);
    CHECK(f.judge(enemy(0x309000, 20), true) == KillVerdict::Counted);
}

void test_kills_out_of_play_do_not_count_and_are_not_counted_later() {
    KillFilter f;
    CHECK(f.judge(enemy(0x300900, 30), false) == KillVerdict::NotInPlay);
    CHECK(f.judge(enemy(0x300900, 30), true) == KillVerdict::Duplicate);
}

void test_level_change_forgets_deaths() {
    KillFilter f;
    CHECK(f.judge(enemy(0x300A00, 40), true) == KillVerdict::Counted);
    f.reset();
    CHECK(f.remembered() == 0);
    // The same placed enemy alive again after a reload (a death, the level again) counts again.
    CHECK(f.judge(enemy(0x300A00, 40), true) == KillVerdict::Counted);
}

void test_memory_is_bounded() {
    KillFilter f;
    for (std::uint32_t i = 0; i < 5000; ++i) {
        f.judge(enemy(0x400000 + i * 0x100, static_cast<std::int16_t>(i & 0x3FFF)), true);
    }
    CHECK(f.remembered() <= 4096);
}

// The filter and the clock together, as the mod joins them.
void test_kill_rotation_end_to_end() {
    KillFilter f;
    RotationConfig config;
    config.mode = Mode::Kills;
    config.kills_per_rotation = 3;
    RotationClock clock(config);
    clock.sync_load(1);

    auto death = [&](const DeathRecord& d, bool in_play = true) {
        if (f.judge(d, in_play) == KillVerdict::Counted) {
            clock.add_kill();
        }
    };

    death(enemy(0x500000, 1));
    death(crate(0x500100, 2));
    death(enemy(0x500000, 1));          // the same enemy again
    death(enemy(0x500200, -1));         // no reward
    death(enemy(0x500300, 3), false);   // in a cutscene
    CHECK(clock.kills() == 1);
    death(enemy(0x500400, 4));
    CHECK(!clock.due());
    death(enemy(0x500500, 5));
    CHECK(clock.due());
    clock.rotated();
    CHECK(clock.kills() == 0);

    // A new level load: both start again.
    death(enemy(0x500600, 6));
    CHECK(clock.sync_load(2));
    f.reset();
    CHECK(clock.kills() == 0);
    death(enemy(0x500600, 6));
    CHECK(clock.kills() == 1);
}

}  // namespace

int main() {
    RUN(test_enemy_kill_counts);
    RUN(test_crates_do_not_count);
    RUN(test_non_enemy_objects_do_not_count);
    RUN(test_unrewarded_and_scripted_deaths_do_not_count);
    RUN(test_non_kill_events_do_not_count);
    RUN(test_duplicate_deaths_ignored);
    RUN(test_kills_out_of_play_do_not_count_and_are_not_counted_later);
    RUN(test_level_change_forgets_deaths);
    RUN(test_memory_is_bounded);
    RUN(test_kill_rotation_end_to_end);
    return 0;
}
