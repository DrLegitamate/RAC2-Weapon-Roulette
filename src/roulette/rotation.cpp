// SPDX-License-Identifier: MIT
//
// Weapon Roulette: the rotation clock (rotation.hpp).

#include "rotation.hpp"

#include <algorithm>

namespace weapon_roulette {

RotationConfig sanitised(RotationConfig config) {
    config.interval_seconds = std::clamp(config.interval_seconds, kMinSeconds, kMaxSeconds);
    config.kills_per_rotation = std::clamp(config.kills_per_rotation, kMinKills, kMaxKills);
    // A disc runs at 50 or 60; anything else would be a bad table row, so take NTSC's.
    if (config.ticks_per_second <= 0) {
        config.ticks_per_second = 60;
    }
    return config;
}

void RotationClock::configure(const RotationConfig& config) {
    m_config = sanitised(config);
    rotated();
}

bool RotationClock::sync_load(std::uint32_t loads) {
    if (m_have_load && loads == m_loads) {
        return false;
    }
    m_have_load = true;
    m_loads = loads;
    rotated();
    return true;
}

void RotationClock::tick(bool in_play, std::uint32_t loads) {
    sync_load(loads);
    if (in_play && m_config.mode == Mode::Time && !due()) {
        ++m_ticks;
    }
}

void RotationClock::add_kill() {
    if (m_config.mode == Mode::Kills && !due()) {
        ++m_kills;
    }
}

bool RotationClock::due() const {
    return m_config.mode == Mode::Time ? m_ticks >= ticks_needed()
                                       : m_kills >= m_config.kills_per_rotation;
}

void RotationClock::rotated() {
    m_ticks = 0;
    m_kills = 0;
}

std::int64_t RotationClock::ticks_needed() const {
    return static_cast<std::int64_t>(m_config.interval_seconds) * m_config.ticks_per_second;
}

int RotationClock::seconds_left() const {
    const std::int64_t left = ticks_needed() - m_ticks;
    if (left <= 0) {
        return 0;
    }
    const std::int64_t rate = m_config.ticks_per_second;
    return static_cast<int>((left + rate - 1) / rate);
}

}  // namespace weapon_roulette
