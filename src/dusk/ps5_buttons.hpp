#pragma once

#ifdef __PROSPERO__
#include <dolphin/mtx.h>
#include <cstddef>

class J2DPicture;
class J2DPane;

namespace dusk::ps5_buttons {
void set_available(std::size_t registrations);
// Preserve the original shaded HUD bodies; use blue for the Cross button.
void prepare_screen(J2DPane& pane);
// Draw only single-image inline prompts without the original Nintendo tint.
// All separate HUD labels and bodies use the original J2D rendering path.
bool draw(J2DPicture& picture, float x, float y, float width, float height,
          Mtx* parent = nullptr);
}
#endif
