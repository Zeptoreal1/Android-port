# Voidius – Android port (independent reimplementation)

**Status: source only. NOT compiled, NOT tested on a device.** The authoring environment had no network, so the Geode SDK, bindings and Android NDK could not be fetched.

## Provenance
- Original `prsevend.voidius` v1.6.1 `.geode` contains a Windows x86-64 DLL only: no `.so`, no source. No public source repository was found.
- This is a new C++ implementation from the shipped `mod.json`/`about.md`, plus function names recovered from the DLL: hooks on `GJBaseGameLayer::handleButton`, `GJBaseGameLayer::processCommands`, `PlayLayer::postUpdate/resetLevel/setupHasCompleted`.
- Mod ID, setting IDs, defaults and the Poppins fonts (OFL) are unchanged. Theme names were recovered; the theme colours are approximations. The original's exact takeover/hand-back and go-straight algorithms are NOT recovered.

## Build
Push to GitHub and run the **Build Voidius (Android)** workflow (needs `geode-sdk/build-geode-mod`), or locally:
`geode sdk install && geode build -p android64` (verify flags with `geode build --help`).
Fix any compile errors against your installed bindings: signatures were written from memory of Geode 4/5 and are unverified.

## Mobile controls
Pause menu → **Voidius** (opens the panel) and **Voidius Assist** checkbox (the `enabled` setting). Panel: live / speed / modes / look + tutorial (?).
F5/F7 remain desktop-only.

## Device test checklist
1. Mod loads; no crash on launch. 2. Pause: Voidius button + toggle present once, don't overlap Resume/Restart. 3. Panel opens/closes, taps never jump the player, back key closes.
4. Toggle OFF/ON changes the "enabled" setting. 5. Tap fast: CPS shows; takeover at Trigger CPS; Quick Start = 2 clicks.
6. **Verify first:** `processCommands` is called once per 240 Hz physics step (else rhythm becomes frame-based). Test at 60/120 Hz.
7. Stop tapping/hold: hand-back within ~1.5 intervals, no stuck jump. 8. Die/restart/pause/quit/disable mid-spam: no stuck input.
9. Each of the 8 modes respects its toggle. 10. Go straight in ship/wave/ufo/swing (gains are untuned guesses). 11. Dual and platformer.

Cheat mod: don't submit runs made with it to leaderboards.
