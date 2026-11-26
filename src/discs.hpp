// SPDX-License-Identifier: MIT
//
// Weapon Roulette: what the mod reads of Going Commando on each disc. Reads only: the mod writes no
// game memory. Every address goes through relocate_data (all of these are below the level
// programs' base, where every program shares the word).
//
// Sources, each cross-checked against a second:
//   - owned bytes (g_inventoryOwned, D_001A7AF8 on NTSC-U v1.01): openrac-mods ng-plus-plus
//     src/discs.h and ammo-max src/buy_all_rac2.cpp agree on all three discs; the launcher's save
//     editor names the same table (chunk 10, 0x001A7AF8).
//   - the hero record (D_00189E20) and his moby at +0x2290: ng-plus-plus src/discs.h and
//     difficulty-mod src/rac2/discs.h.
//   - ticks a second: OpenRAC port/game/rac2-<disc>/hostgen.json "frame_rate".
//   - a moby's class (u16 at +0xAA), Clank's class 87 and Ratchet's class 0: OpenRAC's rac2
//     profile (port/game/rac2/profile.cpp) and docs/port/RAC2_NOTES.md (the vendor's preview
//     spawns class 0 as Ratchet).

#pragma once

#include <cstdint>
#include <string_view>

namespace weapon_roulette {

struct Disc {
    const char* version;     // openrac_api.version.
    std::uint32_t owned;     // u8 per item id: non-zero when owned.
    std::uint32_t hero;      // The hero record.
    int ticks_per_second;    // The game's ticks a second.
};

// clang-format off
inline constexpr Disc kDiscs[] = {
    {"ntsc",  0x001A7AF8u, 0x00189E20u, 60},  // NTSC-U v1.01
    {"pal",   0x001A7B80u, 0x00189EA0u, 50},  // PAL
    {"ntsc2", 0x001A7B00u, 0x00189E20u, 60},  // NTSC-U v2.00
};
// clang-format on

inline const Disc* disc_of(std::string_view version) {
    for (const Disc& disc : kDiscs) {
        if (version == disc.version) {
            return &disc;
        }
    }
    return nullptr;
}

// The hero record's moby, u32.
inline constexpr std::uint32_t kHeroMoby = 0x2290u;

// A moby's fields (SetDeathBits's RewardMoby in the rac2 decompilation, src/game/002A0430.c).
inline constexpr std::uint32_t kMobyClassRecord = 0x24u;  // u32: its class record.
inline constexpr std::uint32_t kMobyClass = 0xAAu;        // u16: its class id.
inline constexpr std::uint32_t kMobyRewardFlags = 0xB1u;  // u8: 0x80 no reward.
inline constexpr std::uint32_t kMobySaveId = 0xB2u;       // s16: its save id, -1 none.
inline constexpr std::uint32_t kMobyFieldsEnd = 0xB4u;    // The last field read, past its end.
// A class record's kind, s16 (5 an enemy).
inline constexpr std::uint32_t kClassKind = 0x46u;

// The hero moby's class when the player is Ratchet.
inline constexpr std::uint16_t kRatchetClass = 0;

// Main RAM: game addresses below this are the PS2's 32 MB.
inline constexpr std::uint32_t kMainRamBytes = 0x02000000u;

// Returns whether `size` bytes at `address` lie in main RAM (a moby or a class record always does).
constexpr bool in_main_ram(std::uint32_t address, std::uint32_t size) {
    return address != 0 && address < kMainRamBytes && size <= kMainRamBytes - address;
}

}  // namespace weapon_roulette
