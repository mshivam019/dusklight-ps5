#pragma once

#include <RmlUi/Core/Types.h>

namespace Rml { class Element; }

namespace dusk::ui::localization {
// Loaded once at startup. Changing the saved menu language requires a restart.
void initialize() noexcept;
Rml::String translate(const Rml::String& text);
void translate_document(Rml::Element* root);
}
