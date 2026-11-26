#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""The manifest and the sources, checked against each other and the pinned SDK.

    python3 tests/manifest_test.py <openrac-mod.json> <src/roulette/rotation.hpp> <SDK folder>

- the SDK's own manifest check (package_mod.check_manifest) passes;
- every setting the brief asks for is there, with its type, default and limits, and the limits
  are the ones the code keeps (rotation.hpp);
- every setting the mod reads is declared, and the version and id agree with OPENRAC_MOD_CPP;
- the mod's sources write no game memory (they only read it).
"""
from __future__ import annotations

import importlib.util
import json
import re
import sys
from pathlib import Path

FAILURES: list[str] = []


def check(ok: bool, what: str) -> None:
    if not ok:
        FAILURES.append(what)


def main() -> int:
    manifest_path, rotation_path, sdk = (Path(a) for a in sys.argv[1:4])
    root = manifest_path.parent
    m = json.loads(manifest_path.read_text())

    # The SDK's packager's rules (the hooks against its rac2 table, the settings' shapes).
    spec = importlib.util.spec_from_file_location("package_mod", sdk / "tools" / "package_mod.py")
    package_mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(package_mod)
    errors = package_mod.check_manifest(m)
    check(not errors, f"package_mod.check_manifest: {errors}")

    # The settings.
    settings = {s["key"]: s for s in m["settings"]}
    want = {
        "enabled": ("bool", True),
        "rotation_mode": ("choice", "time"),
        "interval_seconds": ("int", 30),
        "kills_per_rotation": ("int", 3),
        "avoid_repeats": ("bool", True),
        "show_notifications": ("bool", True),
    }
    for key, (kind, default) in want.items():
        s = settings.get(key)
        check(s is not None, f"setting {key} missing")
        if s:
            check(s["type"] == kind, f"setting {key}: type {s['type']}, want {kind}")
            check(s["default"] == default, f"setting {key}: default {s['default']}, want {default}")
    check(set(settings) == set(want), f"unexpected settings: {set(settings) - set(want)}")

    choices = {c["value"]: c["name"] for c in settings["rotation_mode"]["choices"]}
    check(choices == {"time": "Time Based", "kills": "Enemy Kills"}, f"rotation_mode choices {choices}")

    # The limits are the code's.
    rotation = rotation_path.read_text()

    def constant(name: str) -> int:
        found = re.search(rf"\b{name}\s*=\s*(\d+)\s*;", rotation)
        check(found is not None, f"{name} not found in rotation.hpp")
        return int(found.group(1)) if found else -1

    check(settings["interval_seconds"]["min"] == constant("kMinSeconds") == 1, "interval min")
    check(settings["interval_seconds"]["max"] == constant("kMaxSeconds") == 3600, "interval max")
    check(settings["kills_per_rotation"]["min"] == constant("kMinKills") == 1, "kills min")
    check(settings["kills_per_rotation"]["max"] == constant("kMaxKills") == 1000, "kills max")

    # What the mod reads is declared, and its id and version are the manifest's.
    mod = (root / "src" / "mod.cpp").read_text()
    read = set(re.findall(r'setting<[^>]+>\("([a-z0-9_]+)"\)', mod))
    check(read == set(want), f"settings read by mod.cpp {read} differ from the manifest's")
    check(f'OPENRAC_MOD_CPP("{m["id"]}", "{m["version"]}"' in mod, "OPENRAC_MOD_CPP id/version")
    check('== "kills"' in mod, "mod.cpp reads the kills choice by its value")

    # The games and the hooks.
    check(m["games"] == {"rac2": ["ntsc", "pal", "ntsc2"]}, f"games {m['games']}")
    check(m["hooks"] == ["rac2.moby.record_death"], f"hooks {m['hooks']}")

    # No writes to game memory anywhere in the mod's sources.
    for path in sorted((root / "src").rglob("*.[ch]pp")):
        text = path.read_text()
        for pattern in (r"\.write\s*[<(]", r"\.ref\s*<", r"\.pointer\s*\(", r"\.base\s*\(\)"):
            check(re.search(pattern, text) is None, f"{path.name}: game memory access {pattern}")

    for f in FAILURES:
        print("FAIL:", f)
    print("ok manifest_test" if not FAILURES else f"{len(FAILURES)} failure(s)")
    return 1 if FAILURES else 0


if __name__ == "__main__":
    sys.exit(main())
