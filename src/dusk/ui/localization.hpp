#pragma once

#include <RmlUi/Core/Types.h>
#include <string>

namespace Rml { class Element; }

namespace dusk::ui::localization {
// Loaded once at startup. Changing the saved menu language requires a restart.
const char* language_name(const std::string& id);
void initialize() noexcept;
Rml::String translate(const Rml::String& text);
void translate_document(Rml::Element* root);
}
