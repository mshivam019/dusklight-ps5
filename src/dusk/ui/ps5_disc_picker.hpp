#pragma once
#ifdef __PROSPERO__
#include <functional>
#include <string>

namespace dusk::ui {
// Opens a non-blocking search/protection window over the existing menu image.
void open_ps5_disc_picker(std::function<void(std::string)> on_selected);
}
#endif
