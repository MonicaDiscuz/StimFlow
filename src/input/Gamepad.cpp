#include "input/Gamepad.h"

namespace sf {

GamepadState Gamepad::Poll() {
  XINPUT_STATE state{};
  if (XInputGetState(0, &state) != ERROR_SUCCESS) {
    previous_buttons_ = 0;
    previous_stick_ = 0;
    return {};
  }

  const WORD buttons = state.Gamepad.wButtons;
  const SHORT stick = state.Gamepad.sThumbLX;
  constexpr SHORT deadzone = XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE;
  const bool a = (buttons & XINPUT_GAMEPAD_A) != 0 && (previous_buttons_ & XINPUT_GAMEPAD_A) == 0;
  const bool b = (buttons & XINPUT_GAMEPAD_B) != 0 && (previous_buttons_ & XINPUT_GAMEPAD_B) == 0;
  const bool x = (buttons & XINPUT_GAMEPAD_X) != 0 && (previous_buttons_ & XINPUT_GAMEPAD_X) == 0;
  const bool y = (buttons & XINPUT_GAMEPAD_Y) != 0 && (previous_buttons_ & XINPUT_GAMEPAD_Y) == 0;

  const bool left_held = ((buttons & XINPUT_GAMEPAD_DPAD_LEFT) != 0) || (stick < -deadzone);
  const bool right_held = ((buttons & XINPUT_GAMEPAD_DPAD_RIGHT) != 0) || (stick > deadzone);

  const bool any_button = buttons != 0 && buttons != previous_buttons_;
  const bool any_stick = (stick > deadzone && previous_stick_ <= deadzone) ||
                         (stick < -deadzone && previous_stick_ >= -deadzone);

  previous_buttons_ = buttons;
  previous_stick_ = stick;

  GamepadState result;
  result.connected = true;
  result.a = a;
  result.b = b;
  result.x = x;
  result.y = y;
  result.any = any_button || any_stick;
  result.left_held = left_held;
  result.right_held = right_held;
  result.left_x = stick;
  result.left_y = state.Gamepad.sThumbLY;
  result.left_trigger = state.Gamepad.bLeftTrigger;
  result.right_trigger = state.Gamepad.bRightTrigger;
  return result;
}

}  // namespace sf
