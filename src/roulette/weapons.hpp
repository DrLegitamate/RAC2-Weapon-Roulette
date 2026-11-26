// SPDX-License-Identifier: MIT
//
// Weapon Roulette: which of Going Commando's items the roulette may put in Ratchet's hand, and the
// choice of the next one. Nothing here touches the game: the mod reads the owned bytes and the
// item in hand, and hands them to choose_weapon(), so the rules can be tested on their own.
//
// The item ids are GiveInventoryItem's (func_00288308 on NTSC-U v1.01), the index into the owned
// bytes (0x1A7AF8 on NTSC-U v1.01). Source for the table: the OpenRAC launcher's save editor
// (launcher/core/src/save_edit_rac2.rs, ITEMS and NEVER_HELD) and the item randomizer's names
// (openrac-mods item-randomizer/src/rac2/logic2.c). Both read them from the retail record table.

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string_view>
#include <vector>

namespace weapon_roulette {

// How many owned bytes the game keeps: item ids 0 to 55.
inline constexpr int kItemCount = 56;

// One weapon the roulette may choose.
struct Weapon {
    int id;                 // The game's item id.
    std::string_view name;  // Its base name, as the game's weapon list names its first version.
};

// The weapons Ratchet holds in his hand, in item-id order. Left out on purpose:
//
//   - the gadgets, packs, boots, helmets and armour (not combat weapons);
//   - the Clank Zapper (9), Clank's own weapon;
//   - the Synthenoid (31) and the Shield Charger (45): the game never holds them. Equipping one
//     fires it at once and spends its ammo (ApplyHeroEquipmentRequest, func_L00_002AFBC0, clears
//     requests 9, 0x1F and 0x2D after using them), which the roulette must not do.
//
// The wrench is not an inventory item in Going Commando (it is "out" by a flag, progress +0x10),
// so Item_Equip cannot put it in the hand; there is no wrench option for that reason.
inline constexpr Weapon kWeapons[] = {
    {12, "Bomb Glove"},
    {14, "Visibomb Gun"},
    {16, "Sheepinator"},
    {17, "Decoy Glove"},
    {18, "Tesla Claw"},
    {22, "Chopper"},
    {23, "Pulse Rifle"},
    {24, "Seeker Gun"},
    {25, "Hoverbomb Gun"},
    {26, "Blitz Gun"},
    {27, "Minirocket Tube"},
    {28, "Plasma Coil"},
    {29, "Lava Gun"},
    {30, "Lancer"},
    {32, "Spiderbot Glove"},
    {37, "Bouncer"},
    {41, "Miniturret Glove"},
    {42, "Gravity Bomb"},
    {43, "Zodiac"},
    {44, "RYNO II"},
    {53, "Walloper"},
};

// The roulette weapons with no ammo at all: every version's capacity is 0 (the save editor's
// table), and the game's ammo take treats a weapon with no capacity as endless
// (rac2.weapon.take_ammo). They can always be used, whatever their ammo word holds.
inline constexpr int kAmmoFreeWeapons[] = {16, 53};  // Sheepinator, Walloper

// Returns whether item `id` is one the roulette may put in Ratchet's hand.
bool is_roulette_weapon(int id);

// Returns whether weapon `id` can fire with `ammo` rounds: any round, or a weapon with no ammo.
bool can_fire(int id, int ammo);

// Returns the weapon's base name, or "" for an id that is not a roulette weapon.
std::string_view weapon_name(int id);

// Returns the roulette weapons the player owns, in item-id order: `owned(id)` answers whether the
// owned byte of item `id` is set. Called afresh at each rotation, so a weapon bought or given
// since the last one is in the pool.
std::vector<int> owned_weapons(const std::function<bool(int)>& owned);

// What a rotation chooses from.
struct Choice {
    std::vector<int> pool;     // The owned roulette weapons (owned_weapons()).
    int current = -1;          // The item in Ratchet's hand now, -1 when unknown.
    int previous_pick = -1;    // The roulette's last choice, -1 for none yet.
    bool avoid_repeats = true;  // The "Avoid consecutive repeats" setting.
    bool prefer_ammo = true;    // The "Prefer weapons with ammo" setting.
    // Whether weapon `id` can fire now (can_fire with its ammo); null: every weapon can.
    std::function<bool(int)> loaded;
};

// Picks an index in [0, n) for n > 0: the mod's random source, a fixed one in the tests.
using Pick = std::function<std::size_t(std::size_t n)>;

// Returns the next weapon, or none when the pool is empty.
//
// It is never the weapon in hand while another is owned. With avoid_repeats it is not the
// roulette's previous choice either, while another candidate remains. With one weapon owned the
// answer is that weapon (the caller keeps it in hand and equips nothing).
//
// With prefer_ammo the choice is among the other weapons that can fire, while there are any. When
// none can but the one in hand, the answer is the one in hand (kept: better than an empty one).
// When none can at all, it is made as without prefer_ammo.
std::optional<int> choose_weapon(const Choice& choice, const Pick& pick);

}  // namespace weapon_roulette
