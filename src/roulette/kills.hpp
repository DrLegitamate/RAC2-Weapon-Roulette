// SPDX-License-Identifier: MIT
//
// Weapon Roulette: which recorded deaths are enemy kills.
//
// Going Commando records a death in SetDeathBits (rac2.moby.record_death, func_002A6C58 on NTSC-U
// v1.01; the same code on PAL and NTSC-U v2.00). Enemies and crates both call it. In the rac2
// decompilation (src/game/002A0430.c, src/pal/game/002A7650-cc-g8.c, src/us2/game/002A7AA8-g8.c)
// it:
//
//   1. returns at once for a moby with no reward: its save id (s16 at +0xB2) below 0, or bit 0x80
//      of its reward flags (u8 at +0xB1) set;
//   2. counts a kill (moby_count_kill_or_event(moby, 0): rac2.combat.count_kill, type 0) only when
//      the moby's class record (u32 at +0x24) is non-null and its kind (s16 at +0x46) is 5, the
//      enemy kind; a crate's is not.
//
// moby_count_kill_or_event has one caller on each disc, that one, always with type 0 (the bestiary's
// kill column; type 1 is its other column, never a kill here). So "the game counted an enemy kill"
// is exactly: a record_death call that passes 1 and 2. The filter applies those rules to what the
// mod reads of the moby, then refuses a second death of the same moby in one level load, and a
// kill while the player is not in control (a cutscene's scripted deaths).

#pragma once

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace weapon_roulette {

// The fields of a dying moby the filter reads (offsets above).
struct DeathRecord {
    std::uint32_t moby = 0;          // The moby's game address.
    std::int16_t save_id = -1;       // +0xB2: the level's id of the moby, -1 for none.
    std::uint8_t reward_flags = 0;   // +0xB1: 0x80 set means no reward.
    bool has_class = false;          // +0x24 is non-null.
    std::int16_t class_kind = 0;     // The class record's +0x46.
    int event_type = 0;              // count_kill's type: 0 for a kill (record_death always).
};

// The kind of class record that is an enemy (SetDeathBits's test).
inline constexpr std::int16_t kEnemyKind = 5;
// SetDeathBits's "no reward" bit of +0xB1.
inline constexpr std::uint8_t kNoRewardFlag = 0x80;
// count_kill's type for a kill.
inline constexpr int kKillEvent = 0;

// What the filter made of one death.
enum class KillVerdict {
    Counted,      // An enemy kill, the first of this moby: counts toward the rotation.
    NoReward,     // The game gives it nothing and counts no kill (rule 1).
    NotEnemy,     // A crate or another object (rule 2), or an event that is not a kill.
    Duplicate,    // The same moby died before in this level load.
    NotInPlay,    // A real kill, but while the player was not in control.
};

// Judges recorded deaths, remembering the ones counted in the current level load.
class KillFilter {
public:
    // Forgets the mobys seen: a new level load (or the level made again after a death).
    void reset() { m_seen.clear(); }

    // Judges the death `death`; `in_play` is whether the player is in control now.
    KillVerdict judge(const DeathRecord& death, bool in_play);

    // How many deaths are remembered now (for the tests).
    std::size_t remembered() const { return m_seen.size(); }

private:
    // The most deaths remembered in one load; past it the oldest are forgotten. A level has far
    // fewer rewarding mobys than this.
    static constexpr std::size_t kMaxRemembered = 4096;

    // The deaths of this load: the moby's address and its save id. A placed moby's slot is never
    // given out again in a load, and a copy of one (the difficulty mod's extra enemies) has its
    // own address, so the pair names one enemy.
    std::vector<std::pair<std::uint32_t, std::int16_t>> m_seen;
};

}  // namespace weapon_roulette
