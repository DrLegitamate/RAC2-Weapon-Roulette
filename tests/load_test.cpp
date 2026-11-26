// SPDX-License-Identifier: MIT
//
// The mod's library as the port loads it, against a port table made here (openrac_mod.h), in the
// way openrac-mods' dualsense/tests/load_test.cpp does: no game runs. The test plays the port's
// part: it answers play_status and hero_held, keeps a game memory with the owned bytes, the hero
// and dying mobys laid out as discs.hpp reads them, calls the mod's tick callback and its
// rac2.moby.record_death replacement, and records the equips the mod asks for through
// hook_entry("rac2.item.equip") and the lines it puts in the HUD.
//
// What this shows: the library loads, refuses what it should, takes the hooks its manifest lists,
// and drives the rotation end to end through its real glue code. What it cannot show: that the
// game's addresses, its Item_Equip and its HUD behave as the comments say. That is the in-game
// testing README.md lists.
//
// Run with the library's path: load_test <weapon_roulette.so|.dylib|.dll>.

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <map>
#include <string>
#include <vector>

#if defined(_WIN32)
#include <windows.h>

void* open_library(const char* path) {
    return reinterpret_cast<void*>(LoadLibraryA(path));
}

void* symbol(void* lib, const char* name) {
    return reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(lib), name));
}
#else
#include <dlfcn.h>

void* open_library(const char* path) {
    return dlopen(path, RTLD_NOW | RTLD_LOCAL);
}

void* symbol(void* lib, const char* name) {
    return dlsym(lib, name);
}
#endif

#include "openrac/openrac_mod.h"

#define CHECK(c)                                                                                   \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            std::fprintf(stderr, "%s:%d: CHECK(%s)\n", __FILE__, __LINE__, #c);                    \
            std::exit(1);                                                                          \
        }                                                                                          \
    } while (0)

namespace {

using QueryFn = void (*)(openrac_mod_info*);
using LoadFn = int (*)(const openrac_api*, openrac_mod*);
using DeathFn = void (*)(std::uint32_t, std::int32_t, std::int32_t);

// ---- the game, as the test keeps it ----

// The discs' addresses, written out here from discs.hpp's sources (not included from it, so a
// slip there shows here).
struct DiscLayout {
    const char* version;
    std::uint32_t owned;
    std::uint32_t hero;
    int rate;
};
constexpr DiscLayout kNtsc{"ntsc", 0x001A7AF8u, 0x00189E20u, 60};
constexpr DiscLayout kPal{"pal", 0x001A7B80u, 0x00189EA0u, 50};
constexpr DiscLayout kNtsc2{"ntsc2", 0x001A7B00u, 0x00189E20u, 60};

constexpr std::uint32_t kHeroMobyAddress = 0x00300000u;  // Where the test puts the hero's moby.
constexpr std::uint32_t kEnemyClassRecord = 0x00310000u;
constexpr std::uint32_t kCrateClassRecord = 0x00310100u;

std::vector<std::uint8_t> g_memory(0x02000000u);

struct Host {
    std::string game = "rac2";
    DiscLayout disc = kNtsc;
    int phase = OPENRAC_PLAY_PLAYING;
    std::uint32_t loads = 1;
    int hand = 30;  // The item in Ratchet's hand.
    bool equip_refuses = false;

    std::map<std::string, bool> bools;
    std::map<std::string, std::int64_t> ints;
    std::map<std::string, std::string> strings;

    std::string failed;
    std::vector<std::string> hooks;
    std::vector<std::string> hook_signatures;
    DeathFn death = nullptr;
    std::vector<std::pair<openrac_frame_fn, void*>> frames;
    std::vector<std::pair<openrac_frame_fn, void*>> settings_changed;
    std::vector<int> equips;
    std::vector<std::vector<std::string>> huds;  // Each hud_text_set call's lines.
    std::vector<std::uint32_t> originals;        // record_death's original calls (moby).
} g;

void reset_host(const DiscLayout& disc) {
    std::fill(g_memory.begin(), g_memory.end(), 0);
    g = Host{};
    g.disc = disc;
    g.bools = {{"enabled", true}, {"avoid_repeats", true}, {"show_notifications", true}};
    g.ints = {{"interval_seconds", 30}, {"kills_per_rotation", 3}};
    g.strings = {{"rotation_mode", "time"}};

    // The hero record names his moby; the moby's class is Ratchet's.
    std::memcpy(&g_memory[disc.hero + 0x2290u], &kHeroMobyAddress, 4);
    const std::uint16_t ratchet = 0;
    std::memcpy(&g_memory[kHeroMobyAddress + 0xAAu], &ratchet, 2);

    // Class records: an enemy's kind is 5, a crate's another.
    const std::int16_t enemy = 5, crate = 2;
    std::memcpy(&g_memory[kEnemyClassRecord + 0x46u], &enemy, 2);
    std::memcpy(&g_memory[kCrateClassRecord + 0x46u], &crate, 2);
}

void own(int item, bool owned = true) {
    g_memory[g.disc.owned + static_cast<std::uint32_t>(item)] = owned ? 1 : 0;
}

void set_hero_class(std::uint16_t cls) {
    std::memcpy(&g_memory[kHeroMobyAddress + 0xAAu], &cls, 2);
}

// A moby at `address` that SetDeathBits would see: its class record, save id and reward flags.
void put_moby(std::uint32_t address, std::uint32_t class_record, std::int16_t save_id,
              std::uint8_t flags = 0) {
    std::memcpy(&g_memory[address + 0x24u], &class_record, 4);
    g_memory[address + 0xB1u] = flags;
    std::memcpy(&g_memory[address + 0xB2u], &save_id, 2);
}

// ---- the port's table ----

void host_log(openrac_mod*, int, const char* text) {
    std::printf("  [mod] %s\n", text);
}

void host_fail(openrac_mod*, const char* reason) {
    g.failed = reason;
}

int host_setting_bool(openrac_mod*, const char* key) {
    return g.bools.count(key) && g.bools[key] ? 1 : 0;
}

std::int64_t host_setting_int(openrac_mod*, const char* key) {
    return g.ints.count(key) ? g.ints[key] : 0;
}

double host_setting_float(openrac_mod*, const char*) {
    return 0.0;
}

const char* host_setting_string(openrac_mod*, const char* key) {
    static std::string value;
    value = g.strings.count(key) ? g.strings[key] : "";
    return value.c_str();
}

void original_death(std::uint32_t moby, std::int32_t, std::int32_t) {
    g.originals.push_back(moby);
}

int host_hook(openrac_mod*, const char* name, const char* signature, void* replacement,
              void** original) {
    g.hooks.emplace_back(name);
    g.hook_signatures.emplace_back(signature);
    if (std::strcmp(name, "rac2.moby.record_death") == 0) {
        g.death = reinterpret_cast<DeathFn>(replacement);
        *original = reinterpret_cast<void*>(&original_death);
        return OPENRAC_HOOK_OK;
    }
    return OPENRAC_HOOK_UNKNOWN;
}

int host_on_frame(openrac_mod*, openrac_frame_fn fn, void* user) {
    g.frames.emplace_back(fn, user);
    return 0;
}

int host_on_settings(openrac_mod*, openrac_frame_fn fn, void* user) {
    g.settings_changed.emplace_back(fn, user);
    return OPENRAC_HOOK_OK;
}

openrac_gaddr host_relocate(openrac_gaddr address) {
    return address;
}

int host_play_status(openrac_play_status* out) {
    out->phase = g.phase;
    out->level = 1;
    out->levels = 31;
    out->loads = g.loads;
    return OPENRAC_HOOK_OK;
}

int host_hero_held(openrac_held* out) {
    out->count = 0;
    if (g.hand > 0) {
        out->count = 1;
        out->pieces[0].slot = 0;
        out->pieces[0].item = g.hand;
    }
    return OPENRAC_HOOK_OK;
}

int host_hud_text_set(openrac_mod*, const openrac_hud_text* lines, int count) {
    std::vector<std::string> texts;
    for (int i = 0; i < count; ++i) {
        CHECK(std::memchr(lines[i].text, 0, sizeof lines[i].text) != nullptr);
        CHECK(lines[i].x >= 0.0f && lines[i].x <= 1.0f && lines[i].y >= 0.0f && lines[i].y <= 1.0f);
        CHECK(lines[i].scale >= 0.25f && lines[i].scale <= 4.0f);
        texts.emplace_back(lines[i].text);
    }
    g.huds.push_back(texts);
    return g.phase == OPENRAC_PLAY_LOADING ? OPENRAC_HOOK_UNKNOWN : OPENRAC_HOOK_OK;
}

// The game's Item_Equip, as the hook point's entry: the request is carried out at once here.
std::int32_t fake_equip(std::int32_t id) {
    g.equips.push_back(id);
    if (g.equip_refuses) {
        return 0;
    }
    g.hand = id;
    return 1;
}

void* host_hook_entry(const char* name, const char* signature) {
    if (std::strcmp(name, "rac2.item.equip") == 0 && std::strcmp(signature, "i32(i32)") == 0) {
        return reinterpret_cast<void*>(&fake_equip);
    }
    return nullptr;
}

openrac_api make_api() {
    openrac_api api{};
    api.size = sizeof api;
    api.api_major = OPENRAC_MOD_API_MAJOR;
    api.api_minor = OPENRAC_MOD_API_MINOR;
    api.game = g.game.c_str();
    api.version = g.disc.version;
    api.openrac_version = "test";
    api.platform = "test";
    api.memory = g_memory.data();
    api.log = host_log;
    api.fail = host_fail;
    api.setting_bool = host_setting_bool;
    api.setting_int = host_setting_int;
    api.setting_float = host_setting_float;
    api.setting_string = host_setting_string;
    api.hook = host_hook;
    api.on_frame = host_on_frame;
    api.on_settings = host_on_settings;
    api.relocate_data = host_relocate;
    api.play_status = host_play_status;
    api.hero_held = host_hero_held;
    api.hud_text_set = host_hud_text_set;
    api.hook_entry = host_hook_entry;
    return api;
}

LoadFn g_load = nullptr;
openrac_api g_api{};

// Loads the mod into the host as it stands; returns load's result.
int load() {
    g_api = make_api();
    return g_load(&g_api, reinterpret_cast<openrac_mod*>(&g));
}

void ticks(int n) {
    for (int i = 0; i < n; ++i) {
        for (auto& [fn, user] : g.frames) {
            fn(user);
        }
    }
}

std::vector<std::string> last_hud() {
    return g.huds.empty() ? std::vector<std::string>{} : g.huds.back();
}

// ---- the tests ----

void test_query(void* lib) {
    auto query = reinterpret_cast<QueryFn>(symbol(lib, "openrac_mod_query"));
    CHECK(query != nullptr);
    openrac_mod_info info{};
    info.size = sizeof info;
    query(&info);
    CHECK(std::string(info.id) == "weapon-roulette");
    CHECK(std::string(info.version) == "0.1.0");
    CHECK(info.api_major == OPENRAC_MOD_API_MAJOR);
    CHECK(info.api_minor == 21);  // The manifest's "api".
}

void test_refuses_other_games_and_discs() {
    reset_host(kNtsc);
    g.game = "rac1";
    CHECK(load() != 0);
    CHECK(g.failed.find("Going Commando") != std::string::npos);

    reset_host(kNtsc);
    g.disc.version = "jp";
    CHECK(load() != 0);
    CHECK(g.failed.find("disc") != std::string::npos);
}

void test_loads_on_every_disc_and_takes_only_its_hook() {
    for (const DiscLayout& disc : {kNtsc, kPal, kNtsc2}) {
        reset_host(disc);
        CHECK(load() == 0);
        CHECK(g.failed.empty());
        CHECK(g.hooks == std::vector<std::string>{"rac2.moby.record_death"});
        CHECK(g.hook_signatures == std::vector<std::string>{"void(u32,i32,i32)"});
        CHECK(g.frames.size() == 1);
        CHECK(g.settings_changed.size() == 1);
    }
}

void test_time_rotation_on_each_disc() {
    for (const DiscLayout& disc : {kNtsc, kPal, kNtsc2}) {
        reset_host(disc);
        g.ints["interval_seconds"] = 2;
        own(30);
        own(42);
        own(13);  // Swingshot: a gadget, never chosen.
        g.hand = 30;
        CHECK(load() == 0);

        ticks(2 * disc.rate - 1);
        CHECK(g.equips.empty());
        ticks(1);
        CHECK(g.equips == std::vector<int>{42});
        CHECK(g.hand == 42);
        CHECK((last_hud() == std::vector<std::string>{"WEAPON ROTATED: GRAVITY BOMB"}));

        // And again two seconds later: back to the Lancer (the only other one).
        ticks(2 * disc.rate);
        CHECK((g.equips == std::vector<int>{42, 30}));
    }
}

void test_time_pauses_outside_play_and_for_clank() {
    reset_host(kNtsc);
    g.ints["interval_seconds"] = 1;
    own(30);
    own(42);
    CHECK(load() == 0);

    ticks(30);
    for (int phase : {OPENRAC_PLAY_MENU, OPENRAC_PLAY_SCENE, OPENRAC_PLAY_LOADING,
                      OPENRAC_PLAY_FRONT_END}) {
        g.phase = phase;
        ticks(600);
    }
    g.phase = OPENRAC_PLAY_PLAYING;
    set_hero_class(87);  // Clank.
    ticks(600);
    CHECK(g.equips.empty());

    set_hero_class(0);
    ticks(29);
    CHECK(g.equips.empty());
    ticks(1);
    CHECK(g.equips.size() == 1);
}

void test_level_change_resets_time() {
    reset_host(kNtsc);
    g.ints["interval_seconds"] = 1;
    own(30);
    own(42);
    CHECK(load() == 0);
    ticks(59);
    g.loads = 2;  // A new level, or a death's reload.
    ticks(59);
    CHECK(g.equips.empty());
    ticks(1);
    CHECK(g.equips.size() == 1);
}

void test_manual_switch_does_not_reset_the_clock() {
    reset_host(kNtsc);
    g.ints["interval_seconds"] = 1;
    own(30);
    own(42);
    own(28);
    CHECK(load() == 0);
    ticks(30);
    g.hand = 28;  // The player picks the Plasma Coil from the quick select.
    ticks(29);
    CHECK(g.equips.empty());
    ticks(1);
    CHECK(g.equips.size() == 1);
    CHECK(g.equips[0] != 28);  // Never the one in hand.
}

void test_kill_rotation() {
    reset_host(kPal);
    g.strings["rotation_mode"] = "kills";
    g.ints["kills_per_rotation"] = 2;
    own(30);
    own(42);
    CHECK(load() == 0);
    CHECK(g.death != nullptr);
    ticks(1);
    CHECK((last_hud() == std::vector<std::string>{"WEAPON ROTATION: 0 / 2 KILLS"}));

    put_moby(0x00400000u, kEnemyClassRecord, 10);
    put_moby(0x00400100u, kCrateClassRecord, 11);
    put_moby(0x00400200u, kEnemyClassRecord, -1);  // spawned, no reward
    put_moby(0x00400300u, kEnemyClassRecord, 12, 0x80);
    put_moby(0x00400400u, kEnemyClassRecord, 13);

    g.death(0x00400000u, 0, 3);
    g.death(0x00400100u, 0, 3);  // crate
    g.death(0x00400000u, 0, 3);  // the same enemy again
    g.death(0x00400200u, 0, 3);
    g.death(0x00400300u, 0, 3);
    // The game's own function ran for every one, with the arguments it was given.
    CHECK(g.originals.size() == 5);
    ticks(1);
    CHECK(g.equips.empty());
    CHECK((last_hud() == std::vector<std::string>{"WEAPON ROTATION: 1 / 2 KILLS"}));

    // Time alone does nothing in this mode.
    ticks(50 * 600);
    CHECK(g.equips.empty());

    g.death(0x00400400u, 0, 3);
    ticks(1);
    CHECK(g.equips == std::vector<int>{42});
    CHECK((last_hud() == std::vector<std::string>{"WEAPON ROTATED: GRAVITY BOMB",
                                                    "WEAPON ROTATION: 0 / 2 KILLS"}));
}

void test_kills_out_of_play_do_not_count() {
    reset_host(kNtsc);
    g.strings["rotation_mode"] = "kills";
    g.ints["kills_per_rotation"] = 1;
    own(30);
    own(42);
    CHECK(load() == 0);
    put_moby(0x00400000u, kEnemyClassRecord, 1);
    g.phase = OPENRAC_PLAY_SCENE;
    g.death(0x00400000u, 0, 0);
    g.phase = OPENRAC_PLAY_PLAYING;
    ticks(1);
    CHECK(g.equips.empty());
}

void test_level_change_resets_kills() {
    reset_host(kNtsc);
    g.strings["rotation_mode"] = "kills";
    g.ints["kills_per_rotation"] = 2;
    own(30);
    own(42);
    CHECK(load() == 0);
    put_moby(0x00400000u, kEnemyClassRecord, 1);
    put_moby(0x00400100u, kEnemyClassRecord, 2);
    g.death(0x00400000u, 0, 0);
    g.loads = 2;
    g.death(0x00400100u, 0, 0);
    ticks(1);
    CHECK(g.equips.empty());
}

void test_single_weapon_kept_and_none_owned() {
    reset_host(kNtsc);
    g.ints["interval_seconds"] = 1;
    g.hand = 30;
    own(30);
    CHECK(load() == 0);
    ticks(60 * 5);
    CHECK(g.equips.empty());  // Nothing else to give: the Lancer stays, nothing is asked.

    reset_host(kNtsc);
    g.ints["interval_seconds"] = 1;
    g.hand = -1;
    CHECK(load() == 0);
    ticks(60 * 5);
    CHECK(g.equips.empty());

    // A weapon bought meanwhile joins at the next rotation.
    own(37);
    ticks(60);
    CHECK(g.equips == std::vector<int>{37});
}

void test_refused_equip_is_retried_then_skipped() {
    reset_host(kNtsc);
    g.ints["interval_seconds"] = 1;
    own(30);
    own(42);
    g.equip_refuses = true;
    CHECK(load() == 0);
    ticks(60);
    CHECK(g.equips.size() == 1);
    ticks(59);
    CHECK(g.equips.size() == 60);  // Retried each tick for a second...
    ticks(10);
    CHECK(g.equips.size() == 60);  // ...then given up until the next rotation,
    ticks(49);
    CHECK(g.equips.size() == 60);
    ticks(1);                      // a full interval after it was given up.
    CHECK(g.equips.size() == 61);
}

void test_notifications_off() {
    reset_host(kNtsc);
    g.ints["interval_seconds"] = 1;
    g.bools["show_notifications"] = false;
    g.strings["rotation_mode"] = "time";
    own(30);
    own(42);
    CHECK(load() == 0);
    ticks(60 * 10);
    CHECK(!g.equips.empty());
    for (const auto& lines : g.huds) {
        CHECK(lines.empty());
    }
}

void test_disabled_does_nothing() {
    reset_host(kNtsc);
    g.ints["interval_seconds"] = 1;
    g.bools["enabled"] = false;
    own(30);
    own(42);
    CHECK(load() == 0);
    put_moby(0x00400000u, kEnemyClassRecord, 1);
    g.death(0x00400000u, 0, 0);
    ticks(60 * 10);
    CHECK(g.equips.empty());
    CHECK(g.originals.size() == 1);  // The game's own function still runs.
    for (const auto& lines : g.huds) {
        CHECK(lines.empty());
    }
}

void test_settings_changed_while_running() {
    reset_host(kNtsc);
    g.ints["interval_seconds"] = 100;
    g.ints["kills_per_rotation"] = 1;
    own(30);
    own(42);
    CHECK(load() == 0);
    ticks(60);
    // The launcher switches the mode while the game runs; the kill count is the one kept.
    g.strings["rotation_mode"] = "kills";
    for (auto& [fn, user] : g.settings_changed) {
        fn(user);
    }
    put_moby(0x00400000u, kEnemyClassRecord, 1);
    g.death(0x00400000u, 0, 0);
    ticks(1);
    CHECK(g.equips.size() == 1);
}

void test_countdown_and_hud_outside_level() {
    reset_host(kNtsc);
    g.ints["interval_seconds"] = 10;
    own(30);
    own(42);
    CHECK(load() == 0);
    ticks(5 * 60);
    CHECK((last_hud() == std::vector<std::string>{"NEXT WEAPON IN 5"}));
    // The port refuses HUD lines outside a level; the mod asks again once it can.
    g.phase = OPENRAC_PLAY_LOADING;
    const std::size_t calls = g.huds.size();
    ticks(3);
    CHECK(g.huds.size() >= calls + 3);
}

void test_writes_no_game_memory() {
    reset_host(kNtsc);
    g.strings["rotation_mode"] = "kills";
    g.ints["kills_per_rotation"] = 1;
    own(30);
    own(42);
    own(44);
    for (std::uint32_t i = 0; i < 8; ++i) {
        put_moby(0x00400000u + i * 0x100u, i % 2 ? kCrateClassRecord : kEnemyClassRecord,
                 static_cast<std::int16_t>(i));
    }
    // Some progression the mod must leave as it is: ammo, experience, the versions' records.
    for (std::uint32_t i = 0; i < 0x1000u; ++i) {
        g_memory[0x00139500u + i] = static_cast<std::uint8_t>(i * 7);
    }
    CHECK(load() == 0);
    const std::vector<std::uint8_t> before = g_memory;
    for (std::uint32_t i = 0; i < 8; ++i) {
        g.death(0x00400000u + i * 0x100u, 0, 0);
        ticks(30);
    }
    CHECK(g.equips.size() == 4);
    CHECK(g_memory == before);
}

}  // namespace

int main(int argc, char** argv) {
    CHECK(argc == 2);
    void* lib = open_library(argv[1]);
    CHECK(lib != nullptr);
    g_load = reinterpret_cast<LoadFn>(symbol(lib, "openrac_mod_load"));
    CHECK(g_load != nullptr);

    struct {
        const char* name;
        void (*fn)();
    } tests[] = {
        {"refuses_other_games_and_discs", test_refuses_other_games_and_discs},
        {"loads_on_every_disc_and_takes_only_its_hook", test_loads_on_every_disc_and_takes_only_its_hook},
        {"time_rotation_on_each_disc", test_time_rotation_on_each_disc},
        {"time_pauses_outside_play_and_for_clank", test_time_pauses_outside_play_and_for_clank},
        {"level_change_resets_time", test_level_change_resets_time},
        {"manual_switch_does_not_reset_the_clock", test_manual_switch_does_not_reset_the_clock},
        {"kill_rotation", test_kill_rotation},
        {"kills_out_of_play_do_not_count", test_kills_out_of_play_do_not_count},
        {"level_change_resets_kills", test_level_change_resets_kills},
        {"single_weapon_kept_and_none_owned", test_single_weapon_kept_and_none_owned},
        {"refused_equip_is_retried_then_skipped", test_refused_equip_is_retried_then_skipped},
        {"notifications_off", test_notifications_off},
        {"disabled_does_nothing", test_disabled_does_nothing},
        {"settings_changed_while_running", test_settings_changed_while_running},
        {"countdown_and_hud_outside_level", test_countdown_and_hud_outside_level},
        {"writes_no_game_memory", test_writes_no_game_memory},
    };
    test_query(lib);
    std::printf("ok query\n");
    for (const auto& t : tests) {
        t.fn();
        std::printf("ok %s\n", t.name);
    }
    return 0;
}
