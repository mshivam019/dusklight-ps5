#include "dusk/texture_replacements.hpp"

#include <aurora/texture.hpp>

#include "dusk/logging.h"
#include "dusk/main.h"
#include "dusk/settings.h"
#include "dusk/ps5_buttons.hpp"

namespace dusk::texture_replacements {
namespace {
aurora::texture::ReplacementGroup s_directoryGroup;
#ifdef __PROSPERO__
aurora::texture::ReplacementGroup s_ps5GlyphGroup;
#endif
}

void reload() {
    aurora::texture::unregister_replacements(s_directoryGroup);
    s_directoryGroup.registrations.clear();
#ifdef __PROSPERO__
    ps5_buttons::set_available(0);
    aurora::texture::unregister_replacements(s_ps5GlyphGroup);
    s_ps5GlyphGroup.registrations.clear();
#endif

    if (!getSettings().game.enableTextureReplacements) {
        return;
    }

#ifdef __PROSPERO__
    // Extends Gary Riches' PS5_Dusklight CC0 Kenney glyphs. User texture packs
    // keep their normal higher priority. These are artwork, not extracted game data.
    s_ps5GlyphGroup = aurora::texture::load_replacement_directory(
        "/app0/res/ps5-glyphs", {.priority = kUserTextureReplacementPriority - 2});
    DuskLog.info("PS5 button glyphs: {} registration(s)", s_ps5GlyphGroup.registrations.size());
    ps5_buttons::set_available(s_ps5GlyphGroup.registrations.size());
#endif

    const auto root = ConfigPath / "texture_replacements";
    s_directoryGroup = aurora::texture::load_replacement_directory(
        root, {.priority = kUserTextureReplacementPriority});
    DuskLog.info("Texture replacement directory loaded: {} registration(s)",
                 s_directoryGroup.registrations.size());
}

void shutdown() {
#ifdef __PROSPERO__
    ps5_buttons::set_available(0);
#endif
    aurora::texture::unregister_replacements(s_directoryGroup);
    s_directoryGroup.registrations.clear();
#ifdef __PROSPERO__
    aurora::texture::unregister_replacements(s_ps5GlyphGroup);
    s_ps5GlyphGroup.registrations.clear();
#endif
}

}
