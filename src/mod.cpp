// SPDX-License-Identifier: MIT
//
// Weapon Roulette for Ratchet & Clank: Going Commando on OpenRAC: puts a random owned weapon in
// Ratchet's hand every N seconds of play or every N enemy kills (README.md).
//
// How it reaches the game, all through the SDK:
//   - openrac_api.on_frame, once after each game tick: the clock, the rotation and the HUD.
//   - openrac_api.play_status: the phase. Time counts only in OPENRAC_PLAY_PLAYING (not in a menu,
//     at a vendor, in a scene or while loading); `loads` changing means a new level load or a
//     death's reload, which starts the count again.
//   - rac2.moby.record_death (taken): each recorded death, judged by the kill filter (kills.hpp)
//     before the game's own function runs, which then runs unchanged.
//   - rac2.item.equip (called, not taken), through openrac_api.hook_entry: the game's own request
//     to put an item in the hero's hand, carried out by his update. Called from on_frame, never from
//     inside a hook, so the game is between ticks; the entry runs other mods' replacements first,
//     so a mod that watches equips (the item randomizer) sees the roulette's as the game's.
//     The mod does not take this hook point: the player's own weapon changes go through the game
//     untouched and never reset the count.
//   - openrac_api.hero_held: the item in Ratchet's hand.
//   - Reads of game memory (discs.hpp): the owned bytes, the hero's moby and its class, and the
//     dying moby's reward fields. The mod writes no game memory.

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <random>
#include <string>
#include <vector>

#include "discs.hpp"
#include "openrac/games/rac2.hpp"
#include "openrac/mod.hpp"
#include "roulette/feedback.hpp"
#include "roulette/kills.hpp"
#include "roulette/rotation.hpp"
#include "roulette/weapons.hpp"

namespace {

using namespace weapon_roulette;
using openrac::rac2::Item_Equip;
using openrac::rac2::Moby_RecordDeath;

// How long a rotation retries an equip the game refused, before it gives that one up: one second,
// in the disc's ticks.
int equip_retry_ticks(const Disc& disc) {
    return disc.ticks_per_second;
}

// How long the mod waits for an accepted equip to reach the hand: half a second, in the disc's
// ticks. The rotation is announced once it does; if it never does, it is logged and not announced.
int equip_confirm_ticks(const Disc& disc) {
    return disc.ticks_per_second / 2;
}

// The mod's state. One, made at load; the game's thread is the only one that uses it.
class Roulette {
public:
    Roulette(openrac::Mod& mod, const Disc& disc, Item_Equip::fn equip)
        : m_mod(mod),
          m_disc(disc),
          m_equip(equip),
          m_rng(seed()) {}

    // Reads the settings; a changed rotation setting starts the count again.
    void read_settings() {
        const bool was_enabled = m_enabled;
        m_enabled = m_mod.setting<bool>("enabled");
        if (m_configured && m_enabled != was_enabled) {
            // Switched off or on while the game runs: what was counted before does not carry over.
            m_clock.rotated();
            clear_pending();
        }
        m_avoid_repeats = m_mod.setting<bool>("avoid_repeats");
        m_notify = m_mod.setting<bool>("show_notifications");

        RotationConfig config;
        config.mode = m_mod.setting<std::string>("rotation_mode") == "kills" ? Mode::Kills : Mode::Time;
        config.interval_seconds = m_mod.setting<int>("interval_seconds");
        config.kills_per_rotation = m_mod.setting<int>("kills_per_rotation");
        config.ticks_per_second = m_disc.ticks_per_second;
        config = sanitised(config);

        if (!m_configured || config.mode != m_config.mode
            || config.interval_seconds != m_config.interval_seconds
            || config.kills_per_rotation != m_config.kills_per_rotation) {
            m_config = config;
            m_clock.configure(config);
            m_configured = true;
        }

        m_mod.info(std::string(m_enabled ? "on" : "off") + ", "
                   + (config.mode == Mode::Kills
                          ? "every " + std::to_string(config.kills_per_rotation) + " kills"
                          : "every " + std::to_string(config.interval_seconds) + " s")
                   + (m_avoid_repeats ? ", no repeats" : "") + (m_notify ? ", notifications" : ""));
    }

    // A death about to be recorded (rac2.moby.record_death, before the game's own function).
    void record_death(std::uint32_t moby) {
        if (!m_enabled) {
            return;
        }
        const openrac_play_status status = m_mod.play_status();
        sync(status.loads);

        const KillVerdict verdict = m_kills.judge(read_death(moby), in_play(status));
        if (verdict == KillVerdict::Counted) {
            m_clock.add_kill();
        }
    }

    // After each game tick.
    void frame() {
        const openrac_play_status status = m_mod.play_status();
        sync(status.loads);

        const bool play = m_enabled && in_play(status);
        m_clock.tick(play, status.loads);
        if (play) {
            // The notice's time runs only while the HUD can show it.
            m_feedback.tick();
            confirm_equip();
            if (m_clock.due()) {
                const int hand = in_hand();
                if (is_roulette_weapon(hand)) {
                    rotate(hand);
                } else {
                    hold(hand);
                }
            }
        }
        show(play);
    }

private:
    // Starts again on a new level load (or the level made again after a death).
    void sync(std::uint32_t loads) {
        if (!m_clock.sync_load(loads)) {
            return;
        }
        m_kills.reset();
        m_feedback.clear();
        m_shown_valid = false;  // The HUD's lines went with the level.
        clear_pending();
    }

    // Forgets a rotation under way: its retries, its chosen weapon, its awaited confirmation.
    void clear_pending() {
        m_retry_ticks = 0;
        m_pending_pick = -1;
        m_expected = -1;
        m_held_logged = -2;
    }

    // A rotation is due but Ratchet's hand holds no roulette weapon: a gadget in use (the Glider,
    // the Swingshot...), which the game put there and will take out itself, or nothing the port can
    // name (the wrench out, the hand between items). Taking it out could drop him mid-glide or
    // mid-swing, so the rotation waits, due, until a weapon is back in his hand.
    void hold(int hand) {
        if (hand != m_held_logged) {
            m_held_logged = hand;
            m_mod.debug("rotation held: item " + std::to_string(hand) + " in hand");
        }
    }

    // Whether the player is in control of Ratchet: a level in play (no menu, vendor, scene or
    // load), and the hero is Ratchet (not Clank, a ship or another body).
    bool in_play(const openrac_play_status& status) const {
        if (status.phase != OPENRAC_PLAY_PLAYING) {
            return false;
        }
        const auto& memory = m_mod.memory();
        const openrac::gaddr hero = m_mod.relocate(m_disc.hero);
        if (hero == 0) {
            return false;
        }
        const auto moby = memory.read<std::uint32_t>(hero + kHeroMoby);
        if (!in_main_ram(moby, kMobyFieldsEnd)) {
            return false;
        }
        const auto hero_class = memory.read<std::uint16_t>(moby + kMobyClass);
        if (hero_class != kRatchetClass && hero_class != m_logged_class) {
            // Once per class, for in-game testing: why the clock is held.
            m_logged_class = hero_class;
            m_mod.debug("held: the hero is class " + std::to_string(hero_class) + ", not Ratchet");
        }
        return hero_class == kRatchetClass;
    }

    // The dying moby's fields the kill filter needs.
    DeathRecord read_death(std::uint32_t moby) const {
        DeathRecord record;
        if (!in_main_ram(moby, kMobyFieldsEnd)) {
            return record;  // moby 0: no reward.
        }
        const auto& memory = m_mod.memory();
        record.moby = moby;
        record.save_id = memory.read<std::int16_t>(moby + kMobySaveId);
        record.reward_flags = memory.read<std::uint8_t>(moby + kMobyRewardFlags);
        const auto cls = memory.read<std::uint32_t>(moby + kMobyClassRecord);
        record.has_class = cls != 0;
        if (in_main_ram(cls, kClassKind + 2)) {
            record.class_kind = memory.read<std::int16_t>(cls + kClassKind);
        }
        return record;
    }

    // Whether the owned byte of item `id` is set.
    bool owned(int id) const {
        const openrac::gaddr bytes = m_mod.relocate(m_disc.owned);
        return bytes != 0 && id >= 0 && id < kItemCount
               && m_mod.memory().read<std::uint8_t>(bytes + static_cast<std::uint32_t>(id)) != 0;
    }

    // The item in Ratchet's hand, -1 when the port cannot say.
    int in_hand() const {
        openrac_held held{};
        held.size = sizeof held;
        if (!m_mod.hero_held(held) || held.count < 1) {
            return -1;
        }
        return held.pieces[0].item;
    }

    // Puts the next weapon in Ratchet's hand; `current` is the roulette weapon he holds now.
    void rotate(int current) {
        m_held_logged = -2;

        // A retry keeps the weapon its rotation chose, while it is still owned and not in hand.
        int next = m_pending_pick;
        if (next < 0 || next == current || !owned(next)) {
            Choice choice;
            choice.pool = owned_weapons([this](int id) { return owned(id); });
            choice.current = current;
            choice.previous_pick = m_previous_pick;
            choice.avoid_repeats = m_avoid_repeats;
            const auto chosen = choose_weapon(choice, [this](std::size_t n) {
                return std::uniform_int_distribution<std::size_t>(0, n - 1)(m_rng);
            });
            next = chosen ? *chosen : -1;
        }

        // Nothing to change to: only the one in hand is owned. Keep it.
        if (next < 0 || next == current) {
            m_mod.debug("rotation: one weapon owned, kept");
            m_clock.rotated();
            clear_pending();
            return;
        }

        if (m_equip(next) != 0) {
            // Asked for: announced once it is in his hand (confirm_equip).
            m_mod.debug("rotation: item " + std::to_string(next) + " asked for (was "
                        + std::to_string(current) + ")");
            m_clock.rotated();
            clear_pending();
            m_expected = next;
            m_expected_ticks = equip_confirm_ticks(m_disc);
            return;
        }

        // Refused: the same weapon again next tick, for a second, then this rotation is given up.
        m_pending_pick = next;
        if (++m_retry_ticks >= equip_retry_ticks(m_disc)) {
            m_mod.warn("rotation: the game refused item " + std::to_string(next)
                       + " for a second; skipped");
            m_clock.rotated();
            clear_pending();
        }
    }

    // Announces an asked-for weapon once it is in Ratchet's hand; logs one that never arrives.
    void confirm_equip() {
        if (m_expected < 0) {
            return;
        }
        if (in_hand() == m_expected) {
            m_previous_pick = m_expected;
            m_feedback.announce(weapon_name(m_expected), m_disc.ticks_per_second);
            m_expected = -1;
        } else if (--m_expected_ticks <= 0) {
            m_mod.warn("rotation: item " + std::to_string(m_expected)
                       + " was asked for but never reached the hand; not announced");
            m_expected = -1;
        }
    }

    // Shows the HUD's lines when they changed (or the level took the last ones).
    void show(bool play) {
        const std::vector<HudLine> lines = m_feedback.lines(m_clock, play, m_enabled && m_notify);
        if (m_shown_valid && lines == m_shown) {
            return;
        }
        std::vector<openrac_hud_text> texts;
        float y = 0.16f;
        for (const HudLine& line : lines) {
            openrac_hud_text t{};
            const std::size_t n = (std::min)(line.text.size(), sizeof t.text - 1);
            line.text.copy(t.text, n);
            t.text[n] = '\0';
            t.x = 0.5f;
            t.y = y;
            if (line.style == LineStyle::Notice) {
                t.scale = 0.9f;
                t.rgba[0] = 160; t.rgba[1] = 128; t.rgba[2] = 40; t.rgba[3] = 255;
                t.flags = OPENRAC_HUD_SHADOW;
                y += 0.055f;
            } else {
                t.scale = 1.0f;
                t.rgba[0] = 128; t.rgba[1] = 128; t.rgba[2] = 128; t.rgba[3] = 220;
                t.flags = OPENRAC_HUD_SMALL | OPENRAC_HUD_SHADOW;
                y += 0.04f;
            }
            texts.push_back(t);
        }
        // Refused outside a level: tried again next tick.
        m_shown_valid = m_mod.hud_text_set(texts);
        m_shown = lines;
    }

    static std::uint32_t seed() {
        auto value = static_cast<std::uint32_t>(
            std::chrono::steady_clock::now().time_since_epoch().count());
        try {
            value ^= std::random_device{}();
        } catch (...) {
            // No random device on this system: the clock alone.
        }
        return value;
    }

    openrac::Mod& m_mod;
    const Disc& m_disc;
    Item_Equip::fn m_equip;
    std::mt19937 m_rng;

    bool m_enabled = true;
    bool m_avoid_repeats = true;
    bool m_notify = true;
    bool m_configured = false;
    RotationConfig m_config;

    RotationClock m_clock;
    KillFilter m_kills;
    Feedback m_feedback;

    int m_previous_pick = -1;    // The roulette's last weapon.
    int m_retry_ticks = 0;       // Ticks the current rotation's equip has been refused.
    int m_pending_pick = -1;     // The weapon a refused rotation retries.
    int m_expected = -1;         // An equip asked for, not yet seen in hand.
    int m_expected_ticks = 0;
    int m_held_logged = -2;      // The hand item last logged as holding a rotation (-2 none).
    mutable int m_logged_class = -1;  // The last non-Ratchet hero class logged.

    std::vector<HudLine> m_shown;  // The lines the HUD has now.
    bool m_shown_valid = false;
};

void load(openrac::Mod& mod) {
    if (mod.game() != "rac2") {
        throw openrac::Error("Weapon Roulette is for Ratchet & Clank: Going Commando");
    }
    const Disc* disc = disc_of(mod.version());
    if (disc == nullptr) {
        throw openrac::Error("Weapon Roulette does not know this disc (" + std::string(mod.version())
                             + "): NTSC-U v1.01, PAL and NTSC-U v2.00 only");
    }
    if (!mod.has_play()) {
        throw openrac::Error("this OpenRAC reports no play status for Going Commando");
    }
    const Item_Equip::fn equip = mod.hook_entry<Item_Equip>();
    if (equip == nullptr) {
        throw openrac::Error("this OpenRAC has no rac2.item.equip entry (needs mod interface 1.21)");
    }

    // Lives as long as the program: the port keeps the callbacks below.
    auto* roulette = new Roulette(mod, *disc, equip);
    roulette->read_settings();

    mod.hook<Moby_RecordDeath>([roulette](auto original, std::uint32_t moby, std::int32_t flags,
                                          std::int32_t arg) {
        roulette->record_death(moby);
        original(moby, flags, arg);
    });
    mod.on_frame([roulette] { roulette->frame(); });
    if (mod.has_settings_set()) {
        mod.on_settings([roulette] { roulette->read_settings(); });
    }
}

}  // namespace

OPENRAC_MOD_CPP("weapon-roulette", "0.1.0", load)
