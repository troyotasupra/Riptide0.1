# Riptide: working agreement

Riptide is a co-op survival game on a hostile modern sea, built in **Unreal Engine 5 (C++)**. It was first prototyped in Godot (`troyotasupra/Riptide`). The design plan is `Docs/PLAN.md`, and `Docs/GODOT_CARRYOVER.md` covers what came over from the Godot build.

## Who
- **Troy** (`troyotasupra`, they/them) is the designer and playtester, and doesn't want to handle technical detail. Claude builds the game.
- **Josh** (`JoshuaGessner`) is a developer on macOS. Everything has to build on both Windows and macOS, so never add Windows-only code or tools without a Mac equivalent. Assume Josh may be editing the same files.
- Troy's PC: Windows 11, Ryzen 5 2600X, 32 GB RAM, RTX 3060. Target 1080p at 60 fps.

## Troy's rules
- **Quality bar:** "half-assed" is the complaint to avoid. Finish a feature fully (wired up, compiling, tested) before calling it done, and check it up close in first person.
- **Main must build.** Troy plays straight from source, so a broken `main` breaks the playtest.
- **Assets:** CC0 only, chosen from well-liked ones (by downloads and ratings). Ask before downloading anything, listing each file, its source and its size. Credit every asset in `Docs/CREDITS.md`.
- **Binary files** (`.uasset`, `.umap`, models, textures, audio) go through Git LFS (see `.gitattributes`).
- Never take over Troy's mouse, keyboard or audio.

## Landing work
- Use short-lived branches off `main`, merged by pull request.
- Never rewrite published history: no rebase, amend or force-push on anything that has reached `main`.
- Don't commit `Binaries/`, `Intermediate/`, `Saved/` or `DerivedDataCache/`.
- Commit subjects say what the player or the code now does, in plain present tense.
