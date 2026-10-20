#include "localization.hpp"

#include "dusk/config.hpp"
#include "dusk/data.hpp"
#include "dusk/logging.h"
#include "dusk/settings.h"

#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementText.h>
#include <nlohmann/json.hpp>

#include <fstream>
#include <unordered_map>

namespace dusk::ui::localization {
namespace {
bool sLocalized = false;
std::unordered_map<std::string, std::string> sText;
}

const char* language_name(const std::string& id) {
    if (id == "fr") return "Français";
    if (id == "de") return "Deutsch";
    if (id == "es") return "Español";
    if (id == "it") return "Italiano";
    return "English";
}

void initialize() noexcept {
    sText.clear();
    auto& language = getSettings().backend.menuLanguage;
    if (language.getValue() != "de" && language.getValue() != "en" &&
        language.getValue() != "fr" && language.getValue() != "es" && language.getValue() != "it") {
        language.setValue("en");
#ifdef __PROSPERO__
        // Existing console installs used to skip the only screen with language settings.
        // Show it once when this new preference is introduced; later respect the user's choice.
        getSettings().backend.skipPreLaunchUI.setValue(false);
#endif
        config::save();
    }
    sLocalized = language.getValue() != "en";
    if (!sLocalized) return;
    try {
#ifdef __PROSPERO__
        std::ifstream file(data::base_path_relative("res/locales/" + language.getValue() + ".json"));
#else
        std::ifstream file("res/locales/" + language.getValue() + ".json");
#endif
        const auto catalog = nlohmann::json::parse(file);
        for (const auto& [key, value] : catalog.items()) {
            if (value.is_string()) sText.emplace(key, value.get<std::string>());
        }
        DuskLog.info("Menu translations loaded: {} ({})", sText.size(), language.getValue());
    } catch (const std::exception& error) {
        // A missing optional catalog must not prevent access to disc selection or saves.
        DuskLog.warn("Cannot load menu translations: {}", error.what());
    }
}

Rml::String translate(const Rml::String& text) {
    if (!sLocalized || text.empty()) return text;
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == Rml::String::npos) return text;
    const auto last = text.find_last_not_of(" \t\r\n");
    const auto found = sText.find(text.substr(first, last - first + 1));
    if (found == sText.end()) return text;
    return text.substr(0, first) + found->second + text.substr(last + 1);
}

void translate_document(Rml::Element* root) {
    if (!sLocalized || root == nullptr) return;
    if (auto* text = dynamic_cast<Rml::ElementText*>(root)) {
        text->SetText(translate(text->GetText()));
        return;
    }
    // Translate text nodes only. RML tags, resource paths and event attributes are unchanged.
    for (int i = 0; i < root->GetNumChildren(); ++i) translate_document(root->GetChild(i));
}
}
