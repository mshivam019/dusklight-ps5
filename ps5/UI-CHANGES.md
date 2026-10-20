# Menu languages and PlayStation prompts

Settings > Interface > Menu language offers English, Francais, Deutsch, Espanol
and Italiano. Restart applies the saved UI language independently of game text.
Some uncatalogued advanced help falls back to English. The translation layer is
shared UI code; non-PS5 builds also need regression coverage.

CC0 Kenney artwork and Gary's HUD style provide Cross/Circle/Square/Triangle and
L2/R2/R1 prompts, including inline prompts with shaded bodies. Default GC mapping:
A=Cross, B=Circle, X=Square, Y=Triangle, L=L2, R=R2, Z=R1. Custom mappings do not
dynamically alter artwork. User texture packs retain priority. Exact texture
identification gates the PS5-only J2D draw/color hooks; this is more than a PNG
swap, but does not tune frame rate, resolution, driver or gameplay rendering.

This PR does not change ISO selection or save paths. Earlier combined candidates
were used on PS5 and menu languages were checked; this newly split branch still
needs visual/regression testing. Catalog/artwork tests are separate from console
gameplay tests. No game-disc pixels, ISO, saves or private logs are distributed.
