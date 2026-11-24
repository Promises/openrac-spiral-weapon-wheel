# Placeholder art: Going Commando's quick select pictures, on your own machine

The mod draws Ratchet & Clank's own ring, marker and weapon icons (Going Commando's ring is its
resident HUD's art, which Ratchet & Clank does not have). To see Going Commando's pictures in their
place while trying the mod, you can make a **private texture pack** from your own two discs with
`tools/placeholder_art.py`. Nothing of either game is in this repository, and the pack it makes must
stay on your machine: never commit it, share it or copy it elsewhere.

You need both games set up in OpenRAC on the same machine: Going Commando (USA v2.00, `rac2-ntsc2`)
and Ratchet & Clank (NTSC-U, `rac1-ntsc`).

1. **Dump Going Commando's textures with its quick select open.** Start Going Commando with
   `OPENRAC_DUMP_TEXTURES=<gc dump folder>` (docs/port/TEXTURE_PACKS.md in OpenRAC), get to a level
   with a few weapons, and hold Triangle for a moment: the ring, the selection marker and the
   weapon icons are written once each, as `<hash>.png`, under `Interface/`.
2. **Dump Ratchet & Clank's with its quick select open**, the same way, with this mod switched
   off: `OPENRAC_DUMP_TEXTURES=<rac1 dump folder>`, hold Triangle. Its panel (sprite 0xE934), its
   marker (0xE99C) and its item icons go to `Interface/`.
3. **Write the mapping file**, one line per picture you want replaced: Ratchet & Clank's hash, then
   Going Commando's, from the two folders' file names (look at the pictures to pair them):

   ```
   # rac1 hash       gc hash
   0123456789abcdef  fedcba9876543210   # the ring's panel quarter (0xE934)
   1111222233334444  5555666677778888   # the selection marker (0xE99C)
   aaaabbbbccccdddd  9999888877776666   # the Bomb Glove's icon
   ```

   Keep it outside this repository too (the repository's `.gitignore` ignores `art/` for it).
4. **Make the pack** (`--dry-run` first to see what it would copy):

   ```sh
   python3 tools/placeholder_art.py --gc-dump <gc dump folder> --rac1-dump <rac1 dump folder> \
       --map <mapping file> [--data <the game's data folder>] [--dry-run]
   ```

   It copies each Going Commando picture under the Ratchet & Clank hash it replaces into
   `<data>/texture_packs/rac1-ntsc/gc-weapon-wheel-placeholder/` (the data folder defaults to
   `~/OpenRAC/active/rac1/data`, the launcher's). It refuses to write inside this repository or
   outside the data folder, and deletes nothing: remove that folder to undo it.

Ratchet & Clank's panel is drawn as four mirrored quarters of one picture, so a whole Going
Commando ring will not fit it exactly: this is a placeholder for trying the mod, not finished art.
