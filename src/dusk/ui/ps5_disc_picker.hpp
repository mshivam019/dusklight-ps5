#pragma once
#ifdef __PROSPERO__
#include <functional>
#include <string>

namespace dusk::ui {
// Opens a controller folder browser over the existing menu image. External
// images stay in place; app-owned images require an explicit Move and use action.
void open_ps5_disc_picker(std::function<void(std::string)> on_selected);
}
#endif
