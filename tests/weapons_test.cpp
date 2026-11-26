// SPDX-License-Identifier: MIT
//
// The weapon table and the choice of the next weapon (src/roulette/weapons.hpp).

#include <algorithm>
#include <array>
#include <cstdint>
#include <initializer_list>
#include <set>
#include <vector>

#include "check.hpp"
#include "roulette/weapons.hpp"

namespace {

using namespace weapon_roulette;

// An inventory as the owned bytes hold it, plus the progression the roulette must not touch.
struct Inventory {
    std::array<std::uint8_t, kItemCount> owned{};
    std::array<std::int32_t, kItemCount> ammo{};
    std::array<std::int32_t, kItemCount> experience{};
    std::array<std::uint8_t, kItemCount> record{};  // The version's row (V1 = the id).

    bool has(int id) const { return id >= 0 && id < kItemCount && owned[id] != 0; }
    std::vector<int> pool() const { return owned_weapons([this](int id) { return has(id); }); }
};

// A pick that always takes index `i` (cut to the range).
Pick fixed(std::size_t i) {
    return [i](std::size_t n) { return i < n ? i : n - 1; };
}

// Every index in turn, to see every candidate a choice can make.
std::set<int> every_choice(const Choice& choice) {
    std::set<int> seen;
    for (std::size_t i = 0; i < 32; ++i) {
        if (auto w = choose_weapon(choice, fixed(i))) {
            seen.insert(*w);
        }
    }
    return seen;
}

void test_table_holds_only_held_combat_weapons() {
    // Gadgets, packs, boots, the helmet and armour are not in it.
    for (int gadget : {2, 3, 4, 5, 6, 7, 8, 13, 19, 20, 21, 36, 38, 39, 46, 47, 49, 50, 51, 52, 54, 55}) {
        CHECK(!is_roulette_weapon(gadget));
    }
    // The weapons the game never holds (equipping fires them) and Clank's own.
    CHECK(!is_roulette_weapon(9));   // Clank Zapper
    CHECK(!is_roulette_weapon(31));  // Synthenoid
    CHECK(!is_roulette_weapon(45));  // Shield Charger
    // Ids out of range.
    CHECK(!is_roulette_weapon(-1));
    CHECK(!is_roulette_weapon(0));
    CHECK(!is_roulette_weapon(kItemCount));

    // A sample of the weapons, and their names.
    CHECK(is_roulette_weapon(30) && weapon_name(30) == "Lancer");
    CHECK(is_roulette_weapon(28) && weapon_name(28) == "Plasma Coil");
    CHECK(is_roulette_weapon(37) && weapon_name(37) == "Bouncer");
    CHECK(is_roulette_weapon(44) && weapon_name(44) == "RYNO II");
    CHECK(weapon_name(13).empty());

    // Ids unique and inside the owned bytes.
    std::set<int> ids;
    for (const Weapon& w : kWeapons) {
        CHECK(w.id > 0 && w.id < kItemCount);
        CHECK(!w.name.empty());
        CHECK(ids.insert(w.id).second);
    }
    CHECK(ids.size() == 21);
}

void test_unowned_and_gadgets_excluded_from_pool() {
    Inventory inv;
    inv.owned[30] = 1;  // Lancer
    inv.owned[42] = 1;  // Gravity Bomb
    inv.owned[13] = 1;  // Swingshot (gadget)
    inv.owned[2] = 1;   // Heli-Pack
    inv.owned[31] = 1;  // Synthenoid (never held)
    const std::vector<int> pool = inv.pool();
    CHECK((pool == std::vector<int>{30, 42}));

    // A pool the caller fills by hand is still filtered.
    Choice c;
    c.pool = {13, 2, 31, 9};
    CHECK(!choose_weapon(c, fixed(0)));
}

void test_empty_pool_gives_nothing() {
    Choice c;
    CHECK(!choose_weapon(c, fixed(0)));
}

void test_single_owned_weapon_is_kept() {
    Choice c;
    c.pool = {30};
    c.current = 30;
    c.previous_pick = 30;
    const auto w = choose_weapon(c, fixed(5));
    CHECK(w && *w == 30);  // The caller sees it is the one in hand and equips nothing.

    // Not in hand (the wrench out, or nothing reported): it is the choice.
    c.current = -1;
    const auto v = choose_weapon(c, fixed(0));
    CHECK(v && *v == 30);
}

void test_multiple_weapons_all_reachable() {
    Choice c;
    c.pool = {12, 22, 30, 37};
    c.current = -1;
    c.previous_pick = -1;
    CHECK((every_choice(c) == std::set<int>{12, 22, 30, 37}));
}

void test_current_weapon_avoided() {
    Choice c;
    c.pool = {12, 22, 30, 37};
    c.current = 22;
    c.avoid_repeats = false;
    CHECK((every_choice(c) == std::set<int>{12, 30, 37}));

    // Two owned: always the other one.
    c.pool = {30, 42};
    c.current = 30;
    CHECK((every_choice(c) == std::set<int>{42}));
}

void test_previous_pick_avoided() {
    Choice c;
    c.pool = {12, 22, 30, 37};
    c.current = 12;
    c.previous_pick = 30;  // The player switched to 12 by hand after the roulette gave 30.
    c.avoid_repeats = true;
    CHECK((every_choice(c) == std::set<int>{22, 37}));

    // Off: the previous choice may come again (not the one in hand).
    c.avoid_repeats = false;
    CHECK((every_choice(c) == std::set<int>{22, 30, 37}));

    // With two owned the one in hand is avoided first, even if that repeats the previous choice.
    c.pool = {30, 42};
    c.current = 30;
    c.previous_pick = 42;
    c.avoid_repeats = true;
    CHECK((every_choice(c) == std::set<int>{42}));
}

void test_newly_acquired_weapon_enters_pool() {
    Inventory inv;
    inv.owned[30] = 1;
    inv.owned[42] = 1;
    CHECK((inv.pool() == std::vector<int>{30, 42}));

    inv.owned[28] = 1;  // Plasma Coil bought at a vendor.
    CHECK((inv.pool() == std::vector<int>{28, 30, 42}));

    Choice c;
    c.pool = inv.pool();
    c.current = 30;
    c.previous_pick = 42;
    CHECK((every_choice(c) == std::set<int>{28}));
}

void test_choice_leaves_progression_alone() {
    Inventory inv;
    for (int id : {12, 22, 28, 30, 44}) {
        inv.owned[id] = 1;
        inv.ammo[id] = id * 3;
        inv.experience[id] = id << 5;
        inv.record[id] = static_cast<std::uint8_t>(id + 50);  // A later version's row.
    }
    const Inventory before = inv;

    Choice c;
    c.pool = inv.pool();
    c.current = 30;
    for (std::size_t i = 0; i < 100; ++i) {
        c.previous_pick = *choose_weapon(c, fixed(i));
    }

    // The choice reads the pool only: nothing of the inventory changed.
    CHECK(inv.owned == before.owned);
    CHECK(inv.ammo == before.ammo);
    CHECK(inv.experience == before.experience);
    CHECK(inv.record == before.record);
}

void test_bad_pick_index_is_safe() {
    Choice c;
    c.pool = {12, 30};
    const auto w = choose_weapon(c, [](std::size_t) { return std::size_t{999}; });
    CHECK(w && (*w == 12 || *w == 30));
}

}  // namespace

int main() {
    RUN(test_table_holds_only_held_combat_weapons);
    RUN(test_unowned_and_gadgets_excluded_from_pool);
    RUN(test_empty_pool_gives_nothing);
    RUN(test_single_owned_weapon_is_kept);
    RUN(test_multiple_weapons_all_reachable);
    RUN(test_current_weapon_avoided);
    RUN(test_previous_pick_avoided);
    RUN(test_newly_acquired_weapon_enters_pool);
    RUN(test_choice_leaves_progression_alone);
    RUN(test_bad_pick_index_is_safe);
    return 0;
}
