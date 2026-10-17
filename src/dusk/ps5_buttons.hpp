#pragma once

#ifdef __PROSPERO__
#include <dolphin/mtx.h>
#include <cstddef>

class J2DPicture;

namespace dusk::ps5_buttons {
void set_available(std::size_t registrations);
// Handles only the exact button resources in the manifest. All other pictures
// return false and use the original J2D rendering path.
bool draw(J2DPicture& picture, float x, float y, float width, float height,
          Mtx* parent = nullptr);
}
#endif
