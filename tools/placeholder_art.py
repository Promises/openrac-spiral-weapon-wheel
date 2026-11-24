#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Going Commando's quick select pictures as a local placeholder texture pack for Ratchet & Clank.

    python3 tools/placeholder_art.py --gc-dump <folder> --rac1-dump <folder> --map <file> [--data <folder>] [--dry-run]

For the machine that has both games (docs/placeholder-art.md): it reads two texture dumps made with
OPENRAC_DUMP_TEXTURES (Going Commando with its quick select open, Ratchet & Clank with its own open)
and a mapping file you fill in, one line per picture:

    <Ratchet & Clank hash> <Going Commando hash>   # what it is

(16 hex digits each: the dumps' file names without ".png"; "#" starts a comment), and copies each
Going Commando picture under the Ratchet & Clank hash it replaces into

    <data>/texture_packs/rac1-ntsc/gc-weapon-wheel-placeholder/

where <data> is the game's data folder (by default the launcher's on that machine). It is a
private placeholder: the pictures are Going Commando's, from your own disc, and stay on that
machine. The script refuses to write inside this repository or outside <data>, and never deletes.
"""
from __future__ import annotations

import argparse
import re
import shutil
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
# The launcher's data folder for Ratchet & Clank on the machine this is meant for.
DEFAULT_DATA = Path.home() / "OpenRAC" / "active" / "rac1" / "data"
PACK = Path("texture_packs") / "rac1-ntsc" / "gc-weapon-wheel-placeholder"
HASH = re.compile(r"[0-9a-fA-F]{16}")


def read_map(path: Path) -> list[tuple[str, str, str]]:
    """The mapping file's (rac1 hash, gc hash, note) lines; exits on a malformed one."""
    pairs = []
    for number, raw in enumerate(path.read_text().splitlines(), 1):
        line, _, note = raw.partition("#")
        words = line.split()
        if not words:
            continue
        if len(words) != 2 or not all(HASH.fullmatch(w) for w in words):
            sys.exit(f"placeholder_art: {path}:{number}: want '<rac1 hash> <gc hash>', got {raw!r}")
        pairs.append((words[0].lower(), words[1].lower(), note.strip()))
    return pairs


def index(dump: Path) -> dict[str, Path]:
    """Every <hash>.png under a texture dump, by its hash."""
    found = {}
    for png in dump.rglob("*.png"):
        if HASH.fullmatch(png.stem):
            found.setdefault(png.stem.lower(), png)
    return found


def inside(path: Path, folder: Path) -> bool:
    try:
        path.resolve().relative_to(folder.resolve())
        return True
    except ValueError:
        return False


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--gc-dump", type=Path, required=True, help="Going Commando's texture dump (rac2-ntsc2)")
    ap.add_argument("--rac1-dump", type=Path, required=True, help="Ratchet & Clank's texture dump (rac1-ntsc)")
    ap.add_argument("--map", type=Path, required=True, help="the mapping file (rac1 hash, gc hash per line)")
    ap.add_argument("--data", type=Path, default=DEFAULT_DATA, help=f"the game's data folder (default {DEFAULT_DATA})")
    ap.add_argument("--dry-run", action="store_true", help="say what would be copied, copy nothing")
    args = ap.parse_args(argv)

    for folder in (args.gc_dump, args.rac1_dump, args.data):
        if not folder.is_dir():
            sys.exit(f"placeholder_art: {folder} is not a folder")
    dest = args.data / PACK
    if inside(dest, REPO) or inside(args.data, REPO):
        sys.exit("placeholder_art: refusing to write inside the repository (the pictures are Going Commando's)")
    if not inside(dest, args.data):
        sys.exit(f"placeholder_art: refusing to write outside {args.data}")

    gc = index(args.gc_dump)
    rac1 = index(args.rac1_dump)
    pairs = read_map(args.map)
    if not pairs:
        sys.exit(f"placeholder_art: {args.map} maps nothing")
    copied = 0
    for rac1_hash, gc_hash, note in pairs:
        what = f" ({note})" if note else ""
        if rac1_hash not in rac1:
            print(f"skip {rac1_hash}{what}: not in Ratchet & Clank's dump", file=sys.stderr)
            continue
        source = gc.get(gc_hash)
        if source is None:
            print(f"skip {rac1_hash}{what}: {gc_hash} not in Going Commando's dump", file=sys.stderr)
            continue
        target = dest / f"{rac1_hash}.png"
        print(f"{'would copy' if args.dry_run else 'copy'} {source} -> {target}{what}")
        if not args.dry_run:
            dest.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(source, target)
        copied += 1
    print(f"{copied} of {len(pairs)} pictures {'to copy' if args.dry_run else 'copied'} into {dest}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
