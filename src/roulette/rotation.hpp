// SPDX-License-Identifier: MIT
//
// Weapon Roulette: when the next rotation is due. Time is counted in game ticks (the port calls the
// mod once after each tick: 60 a second on the NTSC-U discs, 50 on PAL, whatever the PC draws), and
// only while the player is in control; kills are counted as the kill filter passes them. A new
// level load starts both again.

#pragma once

#include <cstdint>

namespace weapon_roulette {

// The rotation's trigger: the "Rotation Mode" setting.
enum class Mode {
    Time,   // "time": every interval_seconds of play.
    Kills,  // "kills": every kills_per_rotation enemy kills.
};

// The limits of the two numeric settings (the manifest's min and max).
inline constexpr int kMinSeconds = 1;
inline constexpr int kMaxSeconds = 3600;
inline constexpr int kMinKills = 1;
inline constexpr int kMaxKills = 1000;

// What the rotation is set to.
struct RotationConfig {
    Mode mode = Mode::Time;
    int interval_seconds = 30;
    int kills_per_rotation = 3;
    int ticks_per_second = 60;  // The disc's: 60 NTSC-U, 50 PAL.
};

// Returns `config` with each number kept to its limits (the launcher already does; this guards
// against a value the mod did not get from it).
RotationConfig sanitised(RotationConfig config);

// Counts toward the next rotation.
class RotationClock {
public:
    explicit RotationClock(const RotationConfig& config = {}) { configure(config); }

    // Takes a new configuration (a setting changed): progress starts again.
    void configure(const RotationConfig& config);

    // Notes the level load the game is in (play_status.loads): a value other than the last starts
    // progress again. Returns whether it did.
    bool sync_load(std::uint32_t loads);

    // One game tick: `in_play` is whether the player is in control. Counts the tick toward a time
    // rotation (no further once one is due).
    void tick(bool in_play, std::uint32_t loads);

    // One enemy kill the filter counted. Counts toward a kill rotation (no further once one is due).
    void add_kill();

    // Returns whether a rotation is due now.
    bool due() const;

    // The rotation was carried out (or there was nothing to rotate to): progress back to 0.
    void rotated();

    // What the HUD shows.
    Mode mode() const { return m_config.mode; }
    int kills() const { return m_kills; }
    int kills_needed() const { return m_config.kills_per_rotation; }
    std::int64_t ticks() const { return m_ticks; }
    std::int64_t ticks_needed() const;
    // Whole seconds left before a time rotation, rounded up; 0 once it is due.
    int seconds_left() const;

private:
    RotationConfig m_config;
    std::int64_t m_ticks = 0;  // Ticks in play since the last rotation.
    int m_kills = 0;           // Kills since the last rotation.
    bool m_have_load = false;  // A level load has been seen.
    std::uint32_t m_loads = 0;  // The last one.
};

}  // namespace weapon_roulette
