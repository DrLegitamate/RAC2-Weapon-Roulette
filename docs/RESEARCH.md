# Research notes

What was investigated before writing Weapon Roulette, what was found, and the
decision each finding led to. Sources are OpenRAC (`OpenRAC/OpenRAC`, master
as of 26 November 2026), the released mod SDK 1.36.0, `OpenRAC/openrac-mods`,
and the Going Commando decompilation (`OpenRAC/rac2-decomp`). Function
addresses are NTSC-U v1.01's unless a disc is named.

## The SDK version

The newest SDK whose archive is published is **1.36.0**
(`sdk-v1.36.0`, SHA-256 `815e9730…3638`, matching the release's own `.sha256`).
Tags `sdk-v1.37.0` onwards exist in git, and OpenRAC master's `sdk/VERSION`
reads 1.40.0, but none of those has a downloadable archive, so none can be
pinned the way `tools/fetch_sdk.py` and `sdk.lock` require. The mod pins
1.36.0. openrac-mods itself pins 1.34.0.

The mod declares `"api": "1.21"`, the oldest interface that has everything it
calls: `play_status` (1.8), `hero_held` (1.9), `hud_text_set` (1.12),
`on_settings` (1.15) and `hook_entry` (1.21).

## Enemy kills: `count_kill` versus `record_death`

### What the game does

`SetDeathBits` (`func_002A6C58`, the hook point `rac2.moby.record_death`) is
called when an enemy dies or a crate breaks. In the decompilation
(`src/game/002A0430.c`; PAL `src/pal/game/002A7650-cc-g8.c`; NTSC-U v2.00
`src/us2/game/002A7AA8-g8.c`, all the same) it:

1. returns at once if the moby's save id (`s16` at `+0xB2`) is negative or bit
   `0x80` of its reward flags (`u8` at `+0xB1`) is set: no reward, no kill;
2. increments the moby's own death count (`+0x7F`);
3. **only if** the moby's class record (`+0x24`) is non-null and that record's
   kind (`s16` at `+0x46`) is **5**, calls
   `moby_count_kill_or_event(moby, 0)`;
4. then works out and scatters the bolts (crates reach this too).

`moby_count_kill_or_event` (`func_002A6AE0`, the hook point
`rac2.combat.count_kill`) looks the moby's class id (`+0xAA`) up in the
bestiary table (64 rows of four class ids), returns if it is not there, feeds
the kill-streak multiplier, and adds one to column `type` of the bestiary's
counts (capped at 9999). `type` 0 is the kill column. Type 1 is the table's
second column; the decompilation shows no caller that passes it.

Across the decompilation's C for all three discs, `SetDeathBits` is the only
caller of `moby_count_kill_or_event`, and it always passes `type` 0. Many
functions are still assembly, so another caller cannot be ruled out entirely;
the filter therefore still checks `type`.

So "the game counted an enemy kill" is exactly "`SetDeathBits` ran for a moby
with a save id, without the no-reward bit, whose class kind is 5".

### Why the mod hooks `record_death`

`rac2.combat.count_kill` was added to OpenRAC in commit `e140714e`
("feat(rac2): expose story hooks and scoped gameplay queries", 25 November
2026). It is in OpenRAC master and its `sdk/` folder, but in **no released
SDK** (`sdk-v1.36.0` and `sdk-v1.37.0` predate it), and so presumably in no
released OpenRAC build players have.

Taking it anyway is not workable:

- the SDK's packager (`package_mod.py`) rejects a manifest that lists a hook
  point its tables do not have, so a mod built on 1.36.0 cannot declare it;
- OpenRAC warns when a mod takes a hook its manifest does not list;
- and a hook the port lacks is refused, and **any refused hook fails the whole
  mod's load** (`port/game/common/mod_api/hooks.cpp` sets `hook_refused`, and
  `mods.cpp` unloads the mod), so trying it on an older port would break the
  mod outright.

`rac2.moby.record_death` is in the released SDK, on all three discs. Hooking
it and applying rules 1 and 3 above to the moby before the game's function
runs gives the same answer `count_kill` would, one tick earlier in the same
call. The kill filter (`src/roulette/kills.hpp`) also:

- refuses a second death of the same moby (its address and save id) in one
  level load, since some classes run their death code more than once;
- refuses kills while the player is not in control (a cutscene's scripted
  deaths), remembering them so they are not counted later either;
- forgets everything on a new level load or a death's reload.

When a released SDK carries `count_kill`, switching is small: hook it instead,
build a `DeathRecord` with `event_type = type`, and declare it in the
manifest. The filter already handles `type`.

### Crates

Crates call `SetDeathBits` (they drop bolts through it) but their class kind
is not 5, so they never reach `count_kill`, and the filter's `NotEnemy`
verdict drops them. NG++ (`ng-plus-plus/src/discs.h`, `kClassKind`,
`kKindEnemy`) and OpenRAC's enemy copies (`port/game/rac2/host/enemy_copies.c`)
rely on the same field.

## Equipping: `Item_Equip`

`rac2.item.equip` (`func_002AD860`; PAL `func_002AE3C0`, v2.00
`func_002AE6C8`) takes an item id and makes a **request** that the hero's own
update carries out (`ApplyHeroEquipmentRequest`, `func_L00_002AFBC0`). It
returns 1 when a request was made. The game's givers call it after
`Item_Give`, and the vendor after a purchase.

How the mod uses it:

- **Called, not hooked.** The mod gets the hook point's entry with
  `hook_entry("rac2.item.equip", "i32(i32)")`. Calling the entry is calling it
  as the game does: other mods' replacements run first (the item randomizer
  hooks it), then the game's function. Because Weapon Roulette does not take
  the hook point itself, there is no recursion, and the player's own weapon
  changes pass through untouched and never reset the count.
- **From the tick callback.** Kills arrive inside the game's update (the
  `record_death` hook); the mod only marks the rotation due there and calls
  `Item_Equip` from `on_frame`, after the tick, when the hero's update is not
  running.
- **Only owned, holdable weapons.** The game "never puts an item not owned in
  his hand" (item randomizer's `rac2.c`), and the mod only offers owned ones.
- **Refusals.** If the call returns 0 the mod tries again each tick for a
  second, then gives that rotation up. If an accepted request has not reached
  the hand within half a second the mod logs it, for in-game testing.

### Weapons that must not be equipped

The launcher's save editor (`launcher/core/src/save_edit_rac2.rs`,
`NEVER_HELD`) and its save format notes
(`launcher/docs/RAC2_SAVE_FORMAT.md`) record that choosing the **Clank Zapper
(9)**, **Synthenoid (31)** or **Shield Charger (45)** "uses it at once":
`ApplyHeroEquipmentRequest` fires those requests and clears them, so they are
never in hand. Equipping one would fire it and spend ammo, which the brief
forbids, so they are excluded.

### The wrench

Going Commando's item table has no wrench item. The wrench is shown in the
hand while a flag is set (progress block `+0x10`, `0x1A7A08`, "1 while the
wrench is out", per `RAC2_SAVE_FORMAT.md`). There is no item id for
`Item_Equip`, and setting the flag would be a raw memory write with no SDK
facility behind it and no documented effect on the hero's state. The brief
makes the wrench option conditional on support, so it is left out.

### Weapon ids

From the save editor's `ITEMS` (read from the retail record table), cross
checked with the item randomizer's names: weapons are 9, 12, 14, 16, 17, 18,
22 to 32, 37, 41 to 45 and 53. Removing 9, 31 and 45 leaves the 21 in
`src/roulette/weapons.hpp`. Gadgets (2 to 8, 13, 19 to 21, 36, 38, 39, 46, 49
to 51, 54, 55), the head item 47 and the boots 52 are excluded.

## Ownership and the item in hand

- **Owned bytes**: `u8[56]` by item id at `0x1A7AF8` (NTSC-U v1.01),
  `0x1A7B80` (PAL), `0x1A7B00` (v2.00). NG++ and ammo-max agree on all three;
  the save editor names the same table. The mod reads them afresh at every
  rotation.
- **Item in hand**: from the SDK's `hero_held`, which on Going Commando reports
  the hand's item (`port/game/rac2/host/play.c`, hero record `+0x1248`).

## Play state and time

- `play_status` is implemented for Going Commando
  (`port/game/rac2/host/play.c`, installed early by a constructor in
  `host/entry.c` so it works at mod load). Its phase comes from the level
  loop's game state: 0 is `PLAYING` (when the hero moby exists), 1 and 2 and
  the landing are `SCENE`, the pause menu (3), dialogs (4) and vendors (5) are
  `MENU`, and level exits and loads are `LOADING`. The mod counts time only in
  `PLAYING`.
- `loads` changes with every level load and when "the same level [is] made
  again in place, as a hero's death does". The mod restarts its count on any
  change.
- `on_frame` runs "after each game tick (50 or 60 a second)" (`MODS.md`).
  The port's own tables give the rate: `port/game/rac2-ntsc/hostgen.json`
  `frame_rate` 60, `rac2-pal` 50, `rac2-ntsc2` 60. The difficulty mod and NG++
  use the same rates. Counting ticks is independent of the PC's frame rate.
- **Is Ratchet the hero?** `PLAYING` is also reported in ship levels and Clank
  sections. The mod reads the hero record's moby (`+0x2290`, as NG++ and the
  difficulty mod do) and requires its class (`+0xAA`) to be 0, the class
  OpenRAC's notes give Ratchet (the vendor's preview spawns class 0 as
  Ratchet); Clank's is 87 (`port/game/rac2/profile.cpp`). This is the least
  certain piece of the mod and the first thing to check in game.

## The HUD

`hud_text_set` (1.12) draws up to 63 characters a line in the game's font,
hidden with the game's HUD under menus and in scenes, and "goes with the
level", so the mod sends its lines again after every load. The font has no
glyphs for `& * = [ ] { } ~`, and draws `# % @` as button icons; the mod's
texts avoid them. `play_notice` (1.9) was considered and rejected: it is a
persistent banner at the top of the picture meant for things like a party
waiting for players.
