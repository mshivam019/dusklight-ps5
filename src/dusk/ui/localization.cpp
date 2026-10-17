#include "localization.hpp"

#include "dusk/config.hpp"
#include "dusk/logging.h"
#include "dusk/settings.h"

#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementText.h>
#include <nlohmann/json.hpp>

#include <fstream>
#include <unordered_map>

namespace dusk::ui::localization {
namespace {
bool sGerman = false;
std::unordered_map<std::string, std::string> sGermanText;
}

void initialize() noexcept {
    sGermanText.clear();
    auto& language = getSettings().backend.menuLanguage;
    if (language.getValue() != "de" && language.getValue() != "en") {
        language.setValue("en");
#ifdef __PROSPERO__
        // Existing console installs used to skip the only screen with language settings.
        // Show it once when this new preference is introduced; later respect the user's choice.
        getSettings().backend.skipPreLaunchUI.setValue(false);
#endif
        config::save();
    }
    sGerman = language.getValue() == "de";
    if (!sGerman) return;
    try {
#ifdef __PROSPERO__
        std::ifstream file("/app0/res/locales/de.json");
#else
        std::ifstream file("res/locales/de.json");
#endif
        const auto catalog = nlohmann::json::parse(file);
        for (const auto& [key, value] : catalog.items()) {
            if (value.is_string()) sGermanText.emplace(key, value.get<std::string>());
        }
        DuskLog.info("German menu translations loaded: {}", sGermanText.size());
    } catch (const std::exception& error) {
        // A missing optional catalog must not prevent access to disc selection or saves.
        DuskLog.warn("Cannot load German menu translations: {}", error.what());
    }
}

Rml::String translate(const Rml::String& text) {
    if (!sGerman || text.empty()) return text;
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == Rml::String::npos) return text;
    const auto last = text.find_last_not_of(" \t\r\n");
    const auto found = sGermanText.find(text.substr(first, last - first + 1));
    if (found == sGermanText.end()) return text;
    return text.substr(0, first) + found->second + text.substr(last + 1);
}

void translate_document(Rml::Element* root) {
    if (!sGerman || root == nullptr) return;
    if (auto* text = dynamic_cast<Rml::ElementText*>(root)) {
        text->SetText(translate(text->GetText()));
        return;
    }
    // Translate text nodes only. RML tags, resource paths and event attributes are unchanged.
    for (int i = 0; i < root->GetNumChildren(); ++i) translate_document(root->GetChild(i));
}
}
