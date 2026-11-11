#pragma once

#include <windows.h>

#include <memory>

#include "audio/ClickPlayer.h"
#include "cache/ThumbnailCache.h"
#include "gfx/Renderer.h"
#include "image/FolderLibrary.h"
#include "input/Gamepad.h"

namespace sf {

// 通用图片 Cover Flow：选择文件夹、递归扫描、按需解码缩略图。
class Application {
 public:
  Application(HINSTANCE instance, bool scr_mode);
  ~Application();

  Application(const Application&) = delete;
  Application& operator=(const Application&) = delete;

  int Run();

 private:
  static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam);
  LRESULT HandleMessage(UINT message, WPARAM wparam, LPARAM lparam);

  void CreateMainWindow();
  void FitToPrimaryMonitor();

  void Advance(int direction, bool play_sound);
  void StartAnimationTimer();
  void ResetAutoTimer();

  void SetCursorVisible(bool visible);
  void ShowCursorAndRestartHideTimer();
  void UpdateHoverPause(int mouse_y);
  void FinishDrag();
  void ShowCacheDecision();

  HINSTANCE instance_ = nullptr;
  bool scr_mode_ = false;
  HWND window_ = nullptr;
  FolderLibrary library_;
  ThumbnailCache cache_;
  std::unique_ptr<Renderer> renderer_;
  std::unique_ptr<ClickPlayer> click_player_;
  Gamepad gamepad_;

  POINT scr_origin_{};
  bool scr_origin_set_ = false;

  bool manual_paused_ = false;
  bool hover_paused_ = false;
  bool cursor_visible_ = false;

  bool dragging_ = false;
  int drag_start_x_ = 0;
  int drag_start_y_ = 0;
  bool mouse_moved_ = false;
  float drag_start_position_ = 0.0f;
  int last_mouse_x_ = 0;
  ULONGLONG last_mouse_ms_ = 0;
  float mouse_velocity_ = 0.0f;

  bool inspect_dragging_ = false;
  int inspect_last_x_ = 0;
  int inspect_last_y_ = 0;

  int gamepad_direction_ = 0;
  ULONGLONG gamepad_repeat_at_ = 0;
  UINT gamepad_repeat_interval_ms_ = 33;
  int gamepad_zoom_direction_ = 0;
  ULONGLONG gamepad_zoom_repeat_at_ = 0;
};

}  // namespace sf
