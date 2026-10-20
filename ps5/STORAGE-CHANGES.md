# Protected ISO and save storage

Userdata lives in /data/dusklight/<TITLEID>/user; protected ISOs live in sibling
images. Preserve personal files before the first upgrade: code cannot rescue an
app-local file that the installer removed before the new version first ran.

Old userdata is copied once without overwriting current files or copying ISOs.
A completion marker is written only after all copies succeed. Failed migration
can be retried; completed migration does not resurrect deliberately deleted
saves. Large first-time userdata/texture migration remains synchronous before
the menu and has no progress UI; ISO copying is deferred to explicit selection.

Selecting an external ISO offers Copy to internal SSD and use or Use directly;
the original remains. App-local images require Move and use ISO, with verified
cross-filesystem copy before source removal. Canceled temporary copies are
cleaned up. Different same-name files are not overwritten.

Settings > Prelaunch > Save folder is between Graphics Backend and Save File
Type. Choose an existing folder, use its saves or copy current saves. Different
existing saves block copying. Copy before memory-card initialization and restart
the app to apply the location. Keep the USB drive connected during play. Missing
custom folders block play rather than silently falling back to empty saves.
Settings/textures stay internal. Returning to default uses its existing saves.

This PR retains the original ISO selection frontend. The companion browser PR
provides controller selection on PS5; the two features are separated for review.
No Prospero Store updater code is changed. A real Store update and this newly
split branch need console regression testing. Earlier combined candidates passed
USB save copy, restart and load/save in both ports. Native/host validation is
recorded separately, with no claim of a guaranteed FPS improvement.
