#include <SDL3/SDL.h>
#include "native/platform.h"
#include <algorithm>

namespace {
struct Player { int slot; SDL_JoystickID id = 0; SDL_Joystick* joystick = nullptr; };
Player players[PAD_PLAYERS]{{0},{1},{2},{3}};
bool rumble(void* data, Uint16 low, Uint16 high) {
    pad_player_vibrate(static_cast<Player*>(data)->slot, low / 65535.f, high / 65535.f);
    return true;
}
bool led(void* data, Uint8 r, Uint8 g, Uint8 b) {
    pad_player_light_bar(static_cast<Player*>(data)->slot, r, g, b);
    return true;
}
constexpr unsigned buttons[] = {PAD_CROSS, PAD_CIRCLE, PAD_SQUARE, PAD_TRIANGLE,
    PAD_TOUCH_PAD, 0, PAD_OPTIONS, PAD_L3, PAD_R3, PAD_L1, PAD_R1,
    PAD_UP, PAD_DOWN, PAD_LEFT, PAD_RIGHT};
Sint16 axis(float value) {
    return static_cast<Sint16>(std::clamp(value * 32767.f, -32768.f, 32767.f));
}
void detach(Player& p) {
    pad_player_vibrate(p.slot, 0, 0);
    if (p.joystick) SDL_CloseJoystick(p.joystick);
    if (p.id) SDL_DetachVirtualJoystick(p.id);
    p.id = 0; p.joystick = nullptr;
}
}
extern "C" void ps5_input_initialize() { pad_open(); }
extern "C" void ps5_input_shutdown() { for (auto& p : players) detach(p); }
extern "C" void ps5_input_poll() {
    pad first{}; pad_poll(&first);
    for (auto& p : players) {
        pad state{};
        if (!pad_player(p.slot, &state)) { if (p.id) detach(p); continue; }
        if (!p.id) {
            SDL_VirtualJoystickDesc desc{}; SDL_INIT_INTERFACE(&desc);
            desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
            desc.naxes = SDL_GAMEPAD_AXIS_COUNT; desc.nbuttons = 15;
            desc.axis_mask = (1u << desc.naxes) - 1;
            desc.button_mask = (1u << desc.nbuttons) - 1;
            desc.name = "PS5 Native Controller"; desc.userdata = &p;
            desc.Rumble = rumble; desc.SetLED = led;
            p.id = SDL_AttachVirtualJoystick(&desc);
            if (!p.id) { say("Controller attach failed: %s", SDL_GetError()); continue; }
            p.joystick = SDL_OpenJoystick(p.id);
            if (!p.joystick) { detach(p); continue; }
            SDL_SetJoystickPlayerIndex(p.joystick, p.slot);
        }
        const float axes[] = {state.left_x, state.left_y, state.right_x,
            state.right_y, state.l2 * 2 - 1, state.r2 * 2 - 1};
        for (int i = 0; i < SDL_GAMEPAD_AXIS_COUNT; ++i)
            SDL_SetJoystickVirtualAxis(p.joystick, i, axis(axes[i]));
        for (int i = 0; i < 15; ++i)
            SDL_SetJoystickVirtualButton(p.joystick, i, (state.held & buttons[i]) != 0);
    }
}
