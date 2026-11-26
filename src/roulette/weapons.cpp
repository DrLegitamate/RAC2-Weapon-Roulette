// SPDX-License-Identifier: MIT
//
// Weapon Roulette: the weapon table's lookups and the choice of the next weapon (weapons.hpp).

#include "weapons.hpp"

#include <algorithm>
#include <iterator>

namespace weapon_roulette {
namespace {

// Returns `from` without `id`, or `from` itself when that would leave nothing.
std::vector<int> without(const std::vector<int>& from, int id) {
    std::vector<int> rest;
    rest.reserve(from.size());
    std::copy_if(from.begin(), from.end(), std::back_inserter(rest), [id](int w) { return w != id; });
    return rest.empty() ? from : rest;
}

}  // namespace

bool is_roulette_weapon(int id) {
    return std::any_of(std::begin(kWeapons), std::end(kWeapons), [id](const Weapon& w) {
        return w.id == id;
    });
}

std::string_view weapon_name(int id) {
    for (const Weapon& w : kWeapons) {
        if (w.id == id) {
            return w.name;
        }
    }
    return {};
}

std::vector<int> owned_weapons(const std::function<bool(int)>& owned) {
    std::vector<int> pool;
    for (const Weapon& w : kWeapons) {
        if (owned(w.id)) {
            pool.push_back(w.id);
        }
    }
    return pool;
}

std::optional<int> choose_weapon(const Choice& choice, const Pick& pick) {
    // Only roulette weapons, whatever the caller passed.
    std::vector<int> candidates;
    for (int id : choice.pool) {
        if (is_roulette_weapon(id)
            && std::find(candidates.begin(), candidates.end(), id) == candidates.end()) {
            candidates.push_back(id);
        }
    }
    if (candidates.empty()) {
        return std::nullopt;
    }

    // Never the weapon in hand while there is another; then, if asked, not the last choice.
    candidates = without(candidates, choice.current);
    if (choice.avoid_repeats) {
        candidates = without(candidates, choice.previous_pick);
    }

    const std::size_t index = pick(candidates.size());
    return candidates[index < candidates.size() ? index : 0];
}

}  // namespace weapon_roulette
