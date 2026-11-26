// SPDX-License-Identifier: MIT
//
// Weapon Roulette: the kill filter (kills.hpp).

#include "kills.hpp"

#include <algorithm>

namespace weapon_roulette {

KillVerdict KillFilter::judge(const DeathRecord& death, bool in_play) {
    // SetDeathBits's own early return: no reward, no kill.
    if (death.moby == 0 || death.save_id < 0 || (death.reward_flags & kNoRewardFlag) != 0) {
        return KillVerdict::NoReward;
    }

    // Only the enemy kind is counted by the game, and only as a kill event.
    if (!death.has_class || death.class_kind != kEnemyKind || death.event_type != kKillEvent) {
        return KillVerdict::NotEnemy;
    }

    // One count per enemy per load, whatever runs its death again.
    const std::pair<std::uint32_t, std::int16_t> key{death.moby, death.save_id};
    if (std::find(m_seen.begin(), m_seen.end(), key) != m_seen.end()) {
        return KillVerdict::Duplicate;
    }
    if (m_seen.size() >= kMaxRemembered) {
        m_seen.erase(m_seen.begin());
    }
    m_seen.push_back(key);

    // Remembered either way, so a death replayed once play resumes is not counted then.
    return in_play ? KillVerdict::Counted : KillVerdict::NotInPlay;
}

}  // namespace weapon_roulette
