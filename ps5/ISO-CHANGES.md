# Controller ISO selection and PAL discs

This change replaces the PS5 desktop file dialog with a read-only controller
browser. Open Select Disc Image, select a location (USB0/USB1, app folder or
/data), enter folders with Cross and return with Circle. Reading runs on a worker
over the menu background, with activity feedback and cancellation. Selected
images use the engine's normal verification; PAL/USA/Japan revision 0 support is
retained. Gary keeps ISO/GCM/CISO, the other port keeps ISO/GCM.

This PR does not move/copy an ISO or change the save/config location. Its separate
storage companion adds update-safe placement and optional copies. Files left in
an app folder remain vulnerable to an installer replacing that folder.

PS5 filesystem access may require the existing SDK's owned-root service. The
app never installs or starts a daemon. A native source split still needs a fresh
console regression test; the preceding combined candidates passed PAL selection
and gameplay, but that is not proof of this independently split branch.
