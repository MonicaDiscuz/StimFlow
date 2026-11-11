#pragma once

#include <windows.h>
#include <xinput.h>

namespace sf {

struct GamepadState {
  bool connected = false;
  bool a = false;
  bool b = false;
  bool x = false;
  bool y = false;
  bool any = false;
  bool left_held = false;
  bool right_held = false;
  SHORT left_x = 0;
  SHORT left_y = 0;
  BYTE left_trigger = 0;
  BYTE right_trigger = 0;
};

// 只在有手柄时轮询；按键边沿触发，方向键/左摇杆额外提供保持状态。
class Gamepad {
 public:
  GamepadState Poll();

 private:
  WORD previous_buttons_ = 0;
  SHORT previous_stick_ = 0;
};

}  // namespace sf
