# Internal ISO copy and selectable saves — HOLD test candidate

The current candidate adds an explicit choice after selecting an external ISO:
**Copy to internal SSD and use** or **Use directly**. Copies are byte-verified
under `/data/dusklight/<TITLEID>/images/`, outside the replaceable application
folder. The external original is retained. Equal internal copies are reused;
different files are never overwritten. There is no automatic ISO restoration.
App-owned images still use the explicit **Move and use ISO** action.

**Settings → Prelaunch → Save folder** is between Graphics Backend and Save File
Type. Select an existing folder (including USB) and choose **Use this folder**
or **Copy current saves and use**. Copying is only available before a game has
initialized its memory card. Existing different saves block copying without
overwriting them. Settings, textures and ISOs are not moved with saves.
Changing save location requires a full app restart. Default remains protected
internal userdata. Returning to default uses its existing saves; it does not
silently migrate newer USB saves. To copy back, select that default directory
explicitly and choose the copy action (conflicting saves must be resolved first).

A per-title folder marker and a write check prevent a missing USB folder from
silently falling back to fresh internal saves. Reconnect the drive or select a
folder in Settings if unavailable. Keep the save drive connected while playing.
The folder browser selects existing directories; create a named folder with your
file manager first if desired. Copy/verification progress is per current file;
directory enumeration shows an activity indicator and file count.

Required console test before Go: select external PAL ISO, copy internally,
verify progress/cancel and start; restart after choosing a USB save folder,
save/load a test slot, check existing-save conflicts and missing-drive behavior.
Repeat in both titles. Direct HDD play was reported less smooth via Remote Play;
no claim is made that copying guarantees an FPS improvement. Native compilation
and host data-preservation tests are recorded separately from console results.

Both pull requests remain DRAFT / HOLD until explicit tester Go. No merge or release.

---

# HOLD — console test required

This candidate remains a draft. Do not merge or release before the tester
confirms both console tests and explicitly gives the go-ahead.

Select Disc Image opens a controller-operated folder/file browser over the
existing illustrated menu. Locations include the last selected folder, USB0,
USB1, persistent images, /data, the app folder and the filesystem root. Choose
subfolders with Cross, return with Circle/Back, and cancel without selecting.
A fresh installation needs no prior ISO path. No ISO is bundled or downloaded.

Opening the browser or starting the app never copies an ISO or restores a
deleted one. Each directory is read only when entered, with a cancelable activity
indicator and checked-file count. Enumeration has no truthful total in advance,
so it does not show a fabricated scan percentage. Only the selected disc goes
through the engine's normal disc verification before it can be used.

USB images and images outside installer-owned folders are used at their chosen
path, including nested directories. The saved path survives restart when the
drive is present. There is no internal SSD requirement and no automatic internal
copy. A missing/unplugged disc can be selected again through the browser.

An ISO inside the app/download/previous folder requires the explicit
"Move and use ISO" action before play. It moves to
/data/dusklight/<TITLEID>/images/. A same-filesystem rename needs no second full
allocation. A cross-filesystem move temporarily needs space for a copy, displays
byte-copy/verification progress, and removes only the selected original after
successful verification. Cancel/error retains the source. Different same-name
images receive numbered filenames; an identical protected file is reused only
after byte comparison. Unselected sibling files are never moved or deleted.
If the source is read-only, use your file manager to move it outside the app.

Saves/settings live in /data/dusklight/<TITLEID>/user/. Existing current protected
files win over old app/Store-backup user data. ISO files are never bulk-imported.
Move an app-local ISO before a future Store update; a first upgrade can delete
files before the new code has run, so preserve old personal files beforehand.
Files deliberately left in the replaceable app folder cannot be protected there.

Both ports use Gary's original five HUD labels, blue Cross/red Circle shading,
and additional L2/R2/R1 inline prompts. GC L=L2, R=R2, Z=R1. Custom remapping does
not dynamically redraw the artwork; user texture packs retain higher priority.
Settings -> Interface -> Menu language offers English, Français, Deutsch,
Español and Italiano; restart to apply. Game language remains independent.
Matching catalogs cover common menus/browser messages; advanced uncatalogued
help falls back to English.

Host tests with ASan/UBSan pass: fresh nested external selection without an
internal ISO; no automatic recreation; read-only browsing; explicit selected-only
moves; same-name collisions; full app-folder replacement; region filtering;
save precedence; symlinks; real cross-filesystem copy/verify progress and
cancellation. Catalog key sets/placeholders and reproducible public CC0 artwork
are checked separately. These are not console gameplay tests.

Required console tests for this candidate:
- Startup reaches the illustrated menu without first copying an ISO.
- Browser opens immediately; USB subfolders are navigable; cancel/reopen works.
- PAL starts from a chosen USB path and no internal ISO is created.
- Closing/reopening retains that nested USB selection when connected.
- Deleting an unwanted protected copy does not silently recreate it.
- With expendable test data, explicit Move protects only the selected app ISO
  without a permanent duplicate; cancel leaves the source.
- Settings -> Interface exposes all five languages and persists the choice.
- HUD/Items/Direct selection use the intended colours and L2/R2/R1 prompts.
- After a backed-up app-folder replacement, moved ISO and saves still load.

Earlier PAL/USB launches and German-menu checks apply to earlier builds.
This file-browser candidate and a real Store update still need console testing.
No game-disc pixels, ISO, save, key or private diagnostic file is distributed.
