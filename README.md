# Weapon Roulette

A native mod for [OpenRAC](https://github.com/OpenRAC/OpenRAC)'s PC port of
**Ratchet & Clank: Going Commando**. Every so often the game takes the weapon
out of Ratchet's hand and gives him another one he owns, at random. You choose
what "every so often" means: a number of seconds of play, or a number of
enemies defeated.

Nothing about your arsenal changes. Weapon levels, experience, ammo and Mega
versions are left exactly as they were; the mod only asks the game to put a
different weapon in Ratchet's hand, the same way the game does when you pick
one up. You can still switch weapons yourself between rotations.

> **Status: 0.1.0, untested in the game.** The mod builds and passes its
> automated tests on Linux (see [Testing](#testing)), but it has not yet been
> run in OpenRAC against a real disc. Treat it as a preview until the checks
> under [Outstanding in-game tests](#outstanding-in-game-tests) are done.

## Installing

**From the launcher**: open **Mods**, choose **Add from a link**, and paste
`https://github.com/DrLegitamate/RAC2-Weapon-Roulette`. The launcher installs
the build for your computer from the latest release. (This needs a published
release: push a tag, see [Releasing](#releasing).)

**By hand**: download `weapon-roulette-<version>.zip` from the repository's
Releases, then in the launcher's **Mods** choose **Add a mod** and pick it.

To try a local build without packaging, add the `build/package/weapon_roulette`
folder in the launcher's Mods tab.

Switch the mod on for Going Commando, set the options below, and press Play.
Settings changed in the launcher while the game is running are picked up
straight away (OpenRAC tells the mod), and a changed rotation setting starts
the count again.

## Settings

| Setting | Type | Default | What it does |
|---|---|---|---|
| Enable Weapon Roulette | on/off | On | Off: the mod does nothing at all. |
| Rotation Mode | Time Based / Enemy Kills | Time Based | What triggers the next weapon. |
| Time Interval (Seconds) | whole number, 1 to 3600 | 30 | **Time Based**: seconds of play between rotations. Type any value in the box; 15, 45, 75, 120 and 180 all work. |
| Enemy Kills Per Rotation | whole number, 1 to 1000 | 3 | **Enemy Kills**: enemies defeated between rotations. 1 rotates after every kill. |
| Avoid Consecutive Repeats | on/off | On | Never give the same weapon twice in a row while you own another. |
| Prefer Weapons With Ammo | on/off | On | Only hand over weapons that can fire. If nothing else has ammo, Ratchet keeps the weapon he has. Off: empty weapons can come up too. |
| Show Notifications | on/off | On | Show the mod's lines in the HUD (below). Off: the mod shows nothing. |

Both numbers stay visible and keep their values when you switch modes; only
the one for the selected mode is used.

**What counts as time.** Only time with Ratchet under your control. The clock
stops in the pause menu, at vendors, in other menus and dialogs, during
cutscenes and the ship's landings, while a level loads, and while you play as
Clank or in anything that is not Ratchet on foot. It measures game ticks (60 a
second on the NTSC-U discs, 50 on PAL), so it keeps true time whatever frame
rate your PC draws at.

**What counts as a kill.** Exactly the kills the game itself records for an
enemy: the ones that go into its bestiary count. Crates, ammo boxes, scripted
deaths that give no reward, deaths during cutscenes, and the same enemy dying
twice do not count.

**Which weapons.** Any of these you own: Bomb Glove, Visibomb Gun,
Sheepinator, Decoy Glove, Tesla Claw, Chopper, Pulse Rifle, Seeker Gun,
Hoverbomb Gun, Blitz Gun, Minirocket Tube, Plasma Coil, Lava Gun, Lancer,
Spiderbot Glove, Bouncer, Miniturret Glove, Gravity Bomb, Zodiac, RYNO II and
Walloper. The pool is rebuilt at every rotation, so a weapon you have just
bought joins straight away. Never chosen:

- gadgets, packs, boots, helmets and armour;
- the **Synthenoid** and the **Shield Charger**: the game never holds these,
  it fires them the moment they are selected, which would spend their ammo;
- the **Clank Zapper**, which is Clank's;
- the **wrench**: in Going Commando it is not an inventory item, so there is
  no way to hand it over through the game's equip request. The brief's
  "Include Wrench" option is therefore not offered (see
  [docs/RESEARCH.md](docs/RESEARCH.md#the-wrench)).

The weapon in Ratchet's hand is never chosen while you own another one. If you
own only one weapon it stays in his hand; if you own none, nothing happens.

**Ammo first.** With Prefer Weapons With Ammo on (the default), the roulette
only picks from weapons with rounds left; the Sheepinator and Walloper use no
ammo and always qualify. If the weapon in hand is the only one that can fire,
it stays. If nothing can fire at all, it picks as normal. Avoid Consecutive
Repeats gives way to this: a repeat is better than an empty gun.

**A rotation waits for a weapon in hand.** If it falls due while Ratchet is
using a gadget (the Glider, the Swingshot and so on), has the wrench out, or
holds anything the game can't name, the mod waits and rotates the moment a
weapon is back in his hand. It never pulls a gadget away mid-use.

### What you see

With notifications on, near the top of the screen:

- `WEAPON ROTATED: PLASMA COIL` for three seconds of play once the new weapon
  is actually in Ratchet's hand (time paused in menus doesn't use it up);
- Enemy Kills mode: `WEAPON ROTATION: 2 / 3 KILLS`, small, while you play;
- Time Based mode: `NEXT WEAPON IN 5` ... `1` in the last five seconds.

The lines are drawn with the game's own font and hide with the rest of its HUD
under menus and in cutscenes.

## Supported discs

| Disc | Version id | Supported |
|---|---|---|
| NTSC-U v1.01 | `ntsc` | Yes |
| PAL | `pal` | Yes (50 ticks a second, handled) |
| NTSC-U v2.00 (Greatest Hits) | `ntsc2` | Yes |

Every hook point and call the mod uses exists on all three discs in the SDK it
is built against. The mod needs OpenRAC with **mod interface 1.21 or later**
(it uses `hook_entry`) and refuses to load on anything else, on another game,
or on a disc it does not know, with a message in the launcher.

The mod is marked `"multiplayer": "solo"`: it has not been tried in an OpenRAC
co-op party, so the launcher keeps it out of one.

## How it works

The mod never patches code and never writes game memory. Through the OpenRAC
SDK it:

- runs once after every game tick (`on_frame`) to keep the clock, rotate and
  update the HUD;
- asks `play_status` what the game is doing: only the `PLAYING` phase counts,
  and a change of its `loads` counter (a new level, or the level rebuilt after
  a death) starts the count again;
- hooks `rac2.moby.record_death` (the game's `SetDeathBits`) to see each death
  before the game's own function runs, unchanged;
- calls the game's `Item_Equip` (`rac2.item.equip`) through the SDK's
  `hook_entry`, from the tick callback, never from inside a hook. It does not
  hook `Item_Equip`, so your own weapon changes go through the game untouched
  and do not reset the count, and other mods watching equips still see the
  roulette's;
- reads the owned-item bytes, the hero's moby class and the dying moby's
  reward fields; reads the item in hand through `hero_held`.

The design decisions and the evidence for each are in
[docs/RESEARCH.md](docs/RESEARCH.md), including why kills are taken from
`record_death` rather than `rac2.combat.count_kill`.

## Building

You need CMake 3.20+, a C++17 compiler (GCC, Clang or MSVC), Ninja (optional)
and Python 3. No copy of OpenRAC or the game: the build uses the released
OpenRAC mod SDK that [sdk.lock](sdk.lock) pins (1.36.0, with its SHA-256).

```bash
python3 tools/fetch_sdk.py
```

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
```

```bash
cmake --build build
```

```bash
ctest --test-dir build --output-on-failure
```

```bash
python3 .sdk/openrac-mod-sdk-1.36.0/tools/package_mod.py build/package/weapon_roulette -o dist
```

The last step writes `dist/weapon-roulette-0.1.0-<platform>.zip`. On Windows,
run these in an "x64 Native Tools Command Prompt" for Visual Studio. To build
against another SDK (a newer release, or `sdk/` in an OpenRAC checkout), pass
`-DOPENRAC_SDK=<its folder>` to the configure step.

## Testing

`ctest` runs six tests:

| Test | Covers |
|---|---|
| `weapons_test` | the weapon table (gadgets, never-held and Clank weapons excluded), the pool from owned bytes, single and multiple weapons, avoiding the weapon in hand and the previous pick, new weapons joining, preferring weapons with ammo (and the ammo-free ones), no change to ammo, experience or versions |
| `rotation_test` | rotation after exactly the interval (60 and 50 ticks a second), none before, repeats, custom intervals, pausing and resuming, level changes; kill thresholds, resets, mode switches and setting limits |
| `kills_test` | the game's kill rule: enemies count; crates, unrewarded and scripted deaths, non-kill events, duplicates and out-of-play deaths do not; level changes forget deaths |
| `feedback_test` | the HUD texts, the font's limits, notice timing, the countdown, notifications off |
| `load_test` | the built library loaded against a fake OpenRAC (no game): load refusals, hooks taken, time and kill rotations on all three discs' addresses, pausing, level changes, manual switches, refused equips, notifications and disable switches, live setting changes, and that no game memory is written |
| `manifest_test` | the manifest passes the SDK's own checks; settings, defaults and limits match the brief and the code; no memory-writing calls in the sources |

These prove the mod's own logic and its use of the SDK's interface. They
cannot prove that the game behaves as the research says; that needs the
in-game checks below.

## Outstanding in-game tests

None of these has been done. Each needs OpenRAC (mod interface 1.21+) and a
disc, ideally all three.

1. The mod loads (launcher row shows it on, the log says `on, every 30 s`).
2. Time Based, 30 s: a rotation lands at 30 s of play; the notice shows; the
   weapon in hand changes and works (fires, its ammo and version unchanged).
3. The clock stops in the pause menu, at a vendor, in a cutscene, while
   loading and in the sections where you play as Clank, and resumes.
4. PAL: 30 s on a stopwatch is 30 s.
5. Enemy Kills, 3: crates do not count; enemies do; the count resets on a new
   planet and after Ratchet dies.
6. Arena and wave enemies: check whether they count (spawned enemies with no
   save id give no reward in the game, so they will not; see
   [Known limitations](#known-limitations)).
7. Rotation during awkward moments: with the wrench out, mid-jump, grinding,
   swimming, swinging, on the hoverbike, while a Visibomb is being steered, while charging the Tesla
   Claw or holding the Lava Gun's stream.
8. Ship and race sections (Feltzin, Hrugis, Gorn, the hoverbike races): no
   rotation, no crash. The guard is "the hero's moby class is Ratchet's (0)";
   confirm that holds on foot and fails in these.
9. HUD placement: the lines do not cover the health, bolt or ammo displays.
10. With the item randomizer and NG++: rotations still work, and NG++ tiers
    are kept.
11. Settings changed in the launcher mid-game take effect.

The mod logs at debug level each rotation (`rotation: item 42 (was 30)`), a
refused or unfulfilled equip, and when the hero is not Ratchet, which should
make these checks quick to read from OpenRAC's log.

## Known limitations

- **Kills come from `record_death`, not `count_kill`.** `rac2.combat.count_kill`
  was added to OpenRAC on 25 November 2026 and is in no released SDK yet.
  The mod uses the exact condition under which the game calls it, so the
  result should be the same; see [docs/RESEARCH.md](docs/RESEARCH.md).
- **Enemies the game gives no reward for do not count.** Enemies spawned with
  no save id (some arena and wave spawns) are skipped by the game's own kill
  record, so they are skipped here too. This follows the game, but in an arena
  it may feel stingy. Untested.
- **"Is Ratchet the hero" is a class check** (hero moby class 0). If it turns
  out wrong on some level the roulette simply pauses there, which is safe but
  noticeable; the debug log names the class.
- **Notifications are English only** and use the base weapon name (Lancer,
  not Heavy Lancer).
- **No progress is saved.** A partially counted rotation is lost on quitting,
  on a new planet and on death, by design.
- **Not tried in multiplayer** (the manifest marks it solo).

## Releasing

Push a tag `v<version>` (matching `version` in `openrac-mod.json` and in
`OPENRAC_MOD_CPP` in `src/mod.cpp`). The workflow in
`.github/workflows/build.yml`, the SDK template's own, builds and tests on
Windows, Linux and macOS, cross-builds Android, packages each platform, joins
them into `weapon-roulette-<version>.zip` with `SHA256SUMS`, and publishes the
release the launcher installs from.

## Licence

MIT ([LICENSE](LICENSE)). No game code, data, text or images are included;
the weapon names in the notices are typed by hand.
