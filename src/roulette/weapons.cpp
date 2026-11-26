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

bool can_fire(int id, int ammo) {
    return ammo > 0
           || std::any_of(std::begin(kAmmoFreeWeapons), std::end(kAmmoFreeWeapons),
                          [id](int free) { return free == id; });
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

    // Weapons that can fire first: the others that can, or else the one in hand if it can.
    if (choice.prefer_ammo && choice.loaded) {
        std::vector<int> armed;
        std::copy_if(candidates.begin(), candidates.end(), std::back_inserter(armed),
                     [&](int id) { return id != choice.current && choice.loaded(id); });
        if (!armed.empty()) {
            candidates = armed;
        } else if (std::find(candidates.begin(), candidates.end(), choice.current)
                       != candidates.end()
                   && choice.loaded(choice.current)) {
            return choice.current;
        }
        // Nothing can fire: chosen as without the preference.
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
