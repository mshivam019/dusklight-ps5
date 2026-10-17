# HOLD — console test required

This candidate remains a draft. Do not merge or publish it as a release before
the tester confirms both console tests and explicitly gives the go-ahead.

Both PS5 ports now use the same default button symbols and lettering as Gary's
original five HUD glyphs, blue Cross / red Circle shading, and additional L2/R2/R1
inline prompts. GC L maps to L2, R to R2, Z to R1. Custom control remapping does
not dynamically change the artwork; higher-priority user texture packs still win.

Settings -> Interface -> Menu language offers English, Français, Deutsch,
Español and Italiano. Restart the app to apply the saved UI language. Game
language remains independent and is limited by the selected disc's own assets.
277 common labels/messages have matching DE/FR/ES/IT catalogs. Untranslated
advanced help falls back to English.

The menu is displayed before ISO discovery/protection. Select Disc Image opens
a worker-driven progress window over the existing menu background. It reports
directory enumeration, checked files, copy bytes and verified bytes separately;
percentages are displayed only when a real byte total is available. Cancel is
cooperative. No partial copy becomes a selectable protected ISO; originals stay
in place. Unchanged verified imports avoid repeated full-disc reads.

An ISO beside eboot.bin is protected when Select Disc Image completes, before
a future app-folder replacement. ISO images and saves/settings remain under
/data/dusklight/<TITLEID>/images and user. USB images remain selectable in place.
Before upgrading an old unprotected installation, back up its ISO/user files.
A new app cannot protect files that an installer deleted before it first ran.

Host regression tests cover PAL/USA/Japan headers, update survival, name collisions,
save priority, symlinks, deferred ISO work, byte progress, cancellation during copy
and verification, and verified-import cache reuse (ASan/UBSan). Catalog coverage,
placeholders and reproducible public CC0 button artwork are checked separately.

Required console tests for this combined candidate:
- Startup reaches the illustrated menu without first copying an entire ISO.
- Select Disc Image shows progress; cancel/reopen works and PAL starts.
- Settings -> Interface exposes all five languages; saved choice survives restart.
- HUD/file selection use Gary's shaded symbols; Items/Direct selection show L2;
  right trigger and Z prompts show R2 and R1 with correct button assignments.
- After a backed-up app-folder replacement, protected ISO and saves still load.

Earlier PAL launches and German settings-location checks apply to earlier test
builds. They do not establish console success for the new combined candidate.
No game disc pixels, ISO, save, key or private diagnostic file is distributed.
