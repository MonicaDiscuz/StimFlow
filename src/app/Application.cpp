#include "app/Application.h"

#include "app/FolderPicker.h"
#include "app/Settings.h"
#include "cache/ThumbnailCache.h"
#include "gfx/Hr.h"

#include <windowsx.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace sf {
namespace {

constexpr wchar_t kWindowClass[] = L"StimFlow.MainWindow";
constexpr wchar_t kWindowTitle[] = L"StimFlow";

constexpr UINT_PTR kAutoTimerId = 1;
constexpr UINT_PTR kAnimationTimerId = 2;
constexpr UINT_PTR kCursorTimerId = 3;
constexpr UINT_PTR kGamepadTimerId = 4;
constexpr UINT_PTR kHintTimerId = 5;

constexpr UINT kAutoIntervalMs = 5000;
constexpr UINT kAnimationIntervalMs = 16;
constexpr UINT kCursorHideMs = 2000;

constexpr float kHoverBand = 0.38f;       // 卡片带半高 / 屏高
constexpr float kDragPages = 0.7f;        // 0.7 × 卡宽 = 1 页

constexpr float kGamepadRotatePixelsPerTick = 4.0f;
constexpr float kGamepadPanPixelsPerTick = 14.0f;
constexpr BYTE kGamepadTriggerThreshold = 30;
constexpr ULONGLONG kGamepadZoomIntervalMs = 80;

// 手柄连续翻页使用和键盘相同的重复节奏（Windows 键盘速度/延迟设置）。
UINT KeyboardRepeatIntervalMs() {
  UINT speed = 31;
  if (!SystemParametersInfoW(SPI_GETKEYBOARDSPEED, 0, &speed, 0)) {
    speed = 31;
  }
  speed = std::min<UINT>(speed, 31);
  const double characters_per_second =
      2.5 + (30.0 - 2.5) * static_cast<double>(speed) / 31.0;
  return static_cast<UINT>(std::max(16.0, std::round(1000.0 / characters_per_second)));
}

UINT KeyboardRepeatDelayMs() {
  UINT delay = 1;
  if (!SystemParametersInfoW(SPI_GETKEYBOARDDELAY, 0, &delay, 0)) {
    delay = 1;
  }
  delay = std::min<UINT>(delay, 3);
  return (delay + 1) * 250;
}

float NormalizeAxis(SHORT value) {
  const float deadzone = static_cast<float>(XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE);
  const float raw = static_cast<float>(value);
  if (std::abs(raw) <= deadzone) {
    return 0.0f;
  }
  const float magnitude = (std::abs(raw) - deadzone) / (32767.0f - deadzone);
  return value < 0 ? -magnitude : magnitude;
}

// 展示器模式阻止显示器休眠；.scr 模式不阻止。
class DisplayWakeGuard {
 public:
  DisplayWakeGuard() { SetThreadExecutionState(ES_CONTINUOUS | ES_DISPLAY_REQUIRED); }
  ~DisplayWakeGuard() { SetThreadExecutionState(ES_CONTINUOUS); }

  DisplayWakeGuard(const DisplayWakeGuard&) = delete;
  DisplayWakeGuard& operator=(const DisplayWakeGuard&) = delete;
};

// 原型阶段只支持主显示器（README「常见问题」）。
RECT PrimaryMonitorBounds() {
  const POINT origin{0, 0};
  const HMONITOR monitor = MonitorFromPoint(origin, MONITOR_DEFAULTTOPRIMARY);

  MONITORINFO info{};
  info.cbSize = sizeof(info);
  if (!GetMonitorInfoW(monitor, &info)) {
    return RECT{0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN)};
  }
  return info.rcMonitor;
}

constexpr int kCacheDialogId = 110;

struct CacheDialogContext {
  Gamepad* gamepad = nullptr;
  bool key_a = false;
  bool key_b = false;
  bool key_c = false;
  bool closed = false;
};

// 退出时的缓存处理：保留 / 清除本次运行 / 清除全部。
INT_PTR CALLBACK CacheDialogProc(HWND dialog, UINT message, WPARAM wparam, LPARAM lparam) {
  switch (message) {
    case WM_INITDIALOG: {
      auto* context = reinterpret_cast<CacheDialogContext*>(lparam);
      SetWindowLongPtrW(dialog, DWLP_USER, reinterpret_cast<LONG_PTR>(context));
      if (context != nullptr) {
        // 记录初始按键状态，避免打开对话框时已按住的键立即触发。
        context->key_a = (GetAsyncKeyState('A') & 0x8000) != 0;
        context->key_b = (GetAsyncKeyState('B') & 0x8000) != 0;
        context->key_c = (GetAsyncKeyState('C') & 0x8000) != 0;
      }
      SetTimer(dialog, 1, 33, nullptr);
      SetWindowPos(dialog, HWND_TOPMOST, 0, 0, 0, 0,
                   SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
      SetForegroundWindow(dialog);
      SetFocus(GetDlgItem(dialog, IDOK));
      return TRUE;
    }
    case WM_TIMER:
      if (wparam == 1) {
        auto* context =
            reinterpret_cast<CacheDialogContext*>(GetWindowLongPtrW(dialog, DWLP_USER));
        if (context != nullptr && !context->closed) {
          const GamepadState state =
              context->gamepad != nullptr ? context->gamepad->Poll() : GamepadState{};

          const auto pressed = [](int key, bool& was_down) {
            const SHORT state = GetAsyncKeyState(key);
            const bool down = (state & 0x8000) != 0;
            const bool edge = (state & 0x0001) != 0 || (down && !was_down);
            was_down = down;
            return edge;
          };

          const bool a = pressed('A', context->key_a);
          const bool b = pressed('B', context->key_b);
          const bool c = pressed('C', context->key_c);

          const auto close = [&](INT_PTR code) {
            context->closed = true;
            EndDialog(dialog, code);
          };

          if (state.a || a) {
            close(IDOK);  // A：保留并退出
          } else if (state.b || b) {
            close(100);  // B：清除本次缓存
          } else if (state.y || c) {
            close(101);  // C / 手柄 Y：清除全部缓存
          }
        }
      }
      return TRUE;
    case WM_KEYDOWN: {
      auto* context =
          reinterpret_cast<CacheDialogContext*>(GetWindowLongPtrW(dialog, DWLP_USER));
      if (context == nullptr || context->closed) {
        return TRUE;
      }
      const auto close = [&](INT_PTR code) {
        context->closed = true;
        EndDialog(dialog, code);
      };
      switch (wparam) {
        case 'A':
        case 'a':
          close(IDOK);
          return TRUE;
        case 'B':
        case 'b':
          close(100);
          return TRUE;
        case 'C':
        case 'c':
          close(101);
          return TRUE;
        default:
          break;
      }
      break;
    }
    case WM_CLOSE: {
      auto* context =
          reinterpret_cast<CacheDialogContext*>(GetWindowLongPtrW(dialog, DWLP_USER));
      if (context != nullptr) {
        context->closed = true;
      }
      KillTimer(dialog, 1);
      EndDialog(dialog, IDOK);
      return TRUE;
    }
    case WM_DESTROY:
      KillTimer(dialog, 1);
      return TRUE;
    case WM_COMMAND:
      switch (LOWORD(wparam)) {
        case IDOK:
          EndDialog(dialog, IDOK);
          return TRUE;
        case IDCANCEL:
          // Esc / 关闭按钮按「保留」处理，避免误删。
          EndDialog(dialog, IDOK);
          return TRUE;
        case 100:
          EndDialog(dialog, 100);
          return TRUE;
        case 101:
          EndDialog(dialog, 101);
          return TRUE;
        default:
          break;
      }
      break;
    default:
      break;
  }
  return FALSE;
}

}  // namespace
Application::Application(HINSTANCE instance, bool scr_mode)
    : instance_(instance), scr_mode_(scr_mode) {}

Application::~Application() {
  if (window_ != nullptr) {
    KillTimer(window_, kAutoTimerId);
    KillTimer(window_, kAnimationTimerId);
    KillTimer(window_, kCursorTimerId);
    KillTimer(window_, kGamepadTimerId);
  }

  click_player_.reset();
  renderer_.reset();

  if (window_ != nullptr) {
    DestroyWindow(window_);
  }
}

int Application::Run() {
  CreateMainWindow();

  Settings settings = Settings::Load();
  std::filesystem::path folder;
  if (scr_mode_) {
    folder = settings.last_folder;
  } else {
    folder = PickFolder(window_, settings.last_folder);
    if (folder.empty()) {
      return 0;
    }
    settings.last_folder = folder;
    settings.Save();
  }

  if (folder.empty() || !library_.Scan(folder, settings.recursive)) {
    if (!scr_mode_) {
      MessageBoxW(window_, L"这个文件夹里没有支持的图片。", L"StimFlow",
                  MB_OK | MB_ICONINFORMATION);
    }
    return 0;
  }

  if (!scr_mode_ && settings.cache_enabled && !settings.cache_folder.empty()) {
    cache_.Open(settings.cache_folder);
  }

  renderer_ = std::make_unique<Renderer>(window_, &library_,
                                        cache_.Enabled() ? &cache_ : nullptr);
  if (!scr_mode_) {
    click_player_ = std::make_unique<ClickPlayer>();
    click_player_->LoadEmbedded();
  }

  renderer_->Render();
  SetCursorVisible(false);
  SetTimer(window_, kAutoTimerId, kAutoIntervalMs, nullptr);
  SetTimer(window_, kGamepadTimerId, 500, nullptr);

  std::unique_ptr<DisplayWakeGuard> display_wake;
  if (!scr_mode_) {
    display_wake = std::make_unique<DisplayWakeGuard>();
  }

  MSG message{};
  while (true) {
    const BOOL result = GetMessageW(&message, nullptr, 0, 0);
    if (result == 0) {
      break;
    }
    if (result == -1) {
      CheckWin32(FALSE, "GetMessageW");
    }
    TranslateMessage(&message);
    DispatchMessageW(&message);
  }

  const int exit_code = static_cast<int>(message.wParam);

  // 先释放所有查看资源（纹理、分块、原图缓冲），再询问缓存去留。
  renderer_.reset();
  click_player_.reset();
  display_wake.reset();

  if (!scr_mode_ && cache_.Enabled()) {
    ShowCacheDecision();
  }
  return exit_code;
}

void Application::ShowCacheDecision() {
  CacheDialogContext context{&gamepad_};
  const INT_PTR choice = DialogBoxParamW(instance_, MAKEINTRESOURCEW(kCacheDialogId), nullptr,
                                         CacheDialogProc,
                                         reinterpret_cast<LPARAM>(&context));
  if (choice == 100) {
    cache_.ClearSession();
  } else if (choice == 101) {
    cache_.ClearAll();
  }
}

void Application::CreateMainWindow() {
  WNDCLASSEXW window_class{};
  window_class.cbSize = sizeof(window_class);
  window_class.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
  window_class.lpfnWndProc = &Application::WindowProc;
  window_class.hInstance = instance_;
  window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  window_class.lpszClassName = kWindowClass;

  CheckWin32(RegisterClassExW(&window_class) != 0, "RegisterClassExW");

  const RECT bounds = PrimaryMonitorBounds();
  window_ = CreateWindowExW(WS_EX_APPWINDOW, kWindowClass, kWindowTitle, WS_POPUP | WS_VISIBLE,
                            bounds.left, bounds.top, bounds.right - bounds.left,
                            bounds.bottom - bounds.top, nullptr, nullptr, instance_, this);
  if (window_ == nullptr) {
    CheckWin32(FALSE, "CreateWindowExW");
  }

  ShowWindow(window_, SW_SHOW);
  UpdateWindow(window_);
}

void Application::FitToPrimaryMonitor() {
  if (window_ == nullptr) {
    return;
  }

  const RECT bounds = PrimaryMonitorBounds();
  SetWindowPos(window_, nullptr, bounds.left, bounds.top, bounds.right - bounds.left,
               bounds.bottom - bounds.top, SWP_NOZORDER | SWP_NOACTIVATE);
}

void Application::Advance(int direction, bool play_sound) {
  if (!renderer_ || renderer_->CoverCount() < 2) {
    return;
  }

  renderer_->Step(direction);
  StartAnimationTimer();
  ResetAutoTimer();

  if (play_sound && click_player_) {
    click_player_->Play(1, 0.0f);
  }

  renderer_->Render();
}

void Application::StartAnimationTimer() {
  SetTimer(window_, kAnimationTimerId, kAnimationIntervalMs, nullptr);
}

void Application::ResetAutoTimer() {
  SetTimer(window_, kAutoTimerId, kAutoIntervalMs, nullptr);
}

void Application::SetCursorVisible(bool visible) {
  cursor_visible_ = visible;
  SetCursor(visible ? LoadCursorW(nullptr, IDC_ARROW) : nullptr);
}

void Application::ShowCursorAndRestartHideTimer() {
  if (scr_mode_) {
    return;
  }
  SetCursorVisible(true);
  SetTimer(window_, kCursorTimerId, kCursorHideMs, nullptr);
}

void Application::UpdateHoverPause(int mouse_y) {
  RECT client{};
  if (!GetClientRect(window_, &client)) {
    return;
  }

  const float height = static_cast<float>(client.bottom - client.top);
  const bool hover =
      std::abs(static_cast<float>(mouse_y) - height * 0.5f) < height * kHoverBand;
  if (hover != hover_paused_) {
    hover_paused_ = hover;
    ResetAutoTimer();
  }
}

void Application::FinishDrag() {
  if (!renderer_) {
    return;
  }

  if (GetTickCount64() - last_mouse_ms_ > 100) {
    mouse_velocity_ = 0.0f;
  }

  const float position = renderer_->Position();
  const float sensitivity = std::max(1.0f, kDragPages * renderer_->CardWidthPx());
  const float pages_velocity = -mouse_velocity_ / sensitivity;
  const float extra = std::clamp(pages_velocity * 0.35f, -3.0f, 3.0f);
  const int target = static_cast<int>(std::lround(position + extra));

  const int start_center = static_cast<int>(std::lround(drag_start_position_));
  const bool changed = target != start_center;

  renderer_->AnimateTo(target);
  StartAnimationTimer();
  ResetAutoTimer();

  if (changed && click_player_) {
    click_player_->Play(1, std::abs(mouse_velocity_));
  }

  renderer_->Render();
}

LRESULT CALLBACK Application::WindowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
  Application* self =
      reinterpret_cast<Application*>(GetWindowLongPtrW(window, GWLP_USERDATA));

  if (message == WM_NCCREATE) {
    const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lparam);
    self = static_cast<Application*>(create->lpCreateParams);
    SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    if (self != nullptr) {
      self->window_ = window;
    }
  }

  if (self != nullptr) {
    return self->HandleMessage(message, wparam, lparam);
  }
  return DefWindowProcW(window, message, wparam, lparam);
}

LRESULT Application::HandleMessage(UINT message, WPARAM wparam, LPARAM lparam) {
  switch (message) {
    case WM_PAINT: {
      PAINTSTRUCT paint{};
      BeginPaint(window_, &paint);
      if (renderer_) {
        renderer_->Render();
      }
      EndPaint(window_, &paint);
      return 0;
    }

    case WM_ERASEBKGND:
      return 1;

    case WM_SIZE:
      if (renderer_ && wparam != SIZE_MINIMIZED) {
        renderer_->Resize(LOWORD(lparam), HIWORD(lparam));
        renderer_->Render();
      }
      return 0;

    case WM_KEYDOWN:
      if (scr_mode_) {
        DestroyWindow(window_);
        return 0;
      }
      if (renderer_ && renderer_->InspectActive()) {
        if (wparam == VK_ESCAPE) {
          if (renderer_->InspectZoomed()) {
            renderer_->ResetInspectZoom();
            StartAnimationTimer();
          } else {
            renderer_->EndInspect();
          }
          renderer_->Render();
        }
        return 0;
      }
      switch (wparam) {
        case VK_ESCAPE:
          DestroyWindow(window_);
          return 0;
        case VK_LEFT:
          Advance(-1, true);
          return 0;
        case VK_RIGHT:
          Advance(1, true);
          return 0;
        case VK_SPACE:
          manual_paused_ = !manual_paused_;
          ResetAutoTimer();
          return 0;
        case 'M':
          if (click_player_) {
            click_player_->ToggleMute();
          }
          return 0;
        default:
          break;
      }
      break;

    case WM_MOUSEWHEEL: {
      if (scr_mode_) {
        DestroyWindow(window_);
        return 0;
      }
      const int delta = GET_WHEEL_DELTA_WPARAM(wparam);
      if (renderer_ && renderer_->InspectActive()) {
        renderer_->ZoomInspect(delta);
        StartAnimationTimer();
        renderer_->Render();
        ShowCursorAndRestartHideTimer();
        return 0;
      }
      if (delta > 0) {
        Advance(-1, true);
      } else if (delta < 0) {
        Advance(1, true);
      }
      ShowCursorAndRestartHideTimer();
      return 0;
    }

    case WM_LBUTTONDOWN:
      if (scr_mode_) {
        DestroyWindow(window_);
        return 0;
      }
      if (renderer_) {
        const int x = GET_X_LPARAM(lparam);
        const int y = GET_Y_LPARAM(lparam);

        if (renderer_->InspectActive()) {
          inspect_dragging_ = true;
          SetCapture(window_);
          inspect_last_x_ = x;
          inspect_last_y_ = y;
          ShowCursorAndRestartHideTimer();
          return 0;
        }

        dragging_ = true;
        SetCapture(window_);
        drag_start_x_ = x;
        drag_start_y_ = y;
        mouse_moved_ = false;
        drag_start_position_ = renderer_->Position();
        last_mouse_x_ = drag_start_x_;
        last_mouse_ms_ = GetTickCount64();
        mouse_velocity_ = 0.0f;
        KillTimer(window_, kAnimationTimerId);
        renderer_->SetPosition(drag_start_position_);
        ShowCursorAndRestartHideTimer();
      }
      return 0;

    case WM_LBUTTONDBLCLK:
      if (scr_mode_) {
        DestroyWindow(window_);
        return 0;
      }
      if (renderer_) {
        const int x = GET_X_LPARAM(lparam);
        const int y = GET_Y_LPARAM(lparam);

        if (!renderer_->InspectActive() && renderer_->CenterCardContains(x, y)) {
          if (renderer_->BeginInspect()) {
            renderer_->Render();
            SetTimer(window_, kHintTimerId, 16, nullptr);
          }
          return 0;
        }

        if (renderer_->InspectActive()) {
          if (!renderer_->InspectZoomed()) {
            renderer_->ResetInspectRotation();
            renderer_->Render();
          }
          inspect_dragging_ = true;
          SetCapture(window_);
          inspect_last_x_ = x;
          inspect_last_y_ = y;
        }
      }
      return 0;

    case WM_MOUSEMOVE: {
      const int x = GET_X_LPARAM(lparam);
      const int y = GET_Y_LPARAM(lparam);

      if (scr_mode_) {
        if (!scr_origin_set_) {
          scr_origin_ = POINT{x, y};
          scr_origin_set_ = true;
        } else {
          const int dx = x - scr_origin_.x;
          const int dy = y - scr_origin_.y;
          if (dx * dx + dy * dy > 20 * 20) {
            DestroyWindow(window_);
          }
        }
        return 0;
      }

      ShowCursorAndRestartHideTimer();

      if (renderer_ && renderer_->InspectActive()) {
        if (inspect_dragging_) {
          const int dx = x - inspect_last_x_;
          const int dy = y - inspect_last_y_;
          inspect_last_x_ = x;
          inspect_last_y_ = y;
          if (renderer_->InspectZoomed()) {
            renderer_->PanInspect(static_cast<float>(dx), static_cast<float>(dy));
          } else {
            renderer_->RotateInspect(static_cast<float>(dx), static_cast<float>(dy));
          }
          renderer_->Render();
        }
        return 0;
      }

      UpdateHoverPause(y);

      if (dragging_ && renderer_) {
        if (std::abs(x - drag_start_x_) > 4 || std::abs(y - drag_start_y_) > 4) {
          mouse_moved_ = true;
        }
        if (mouse_moved_) {
          const float sensitivity = std::max(1.0f, kDragPages * renderer_->CardWidthPx());
          float target = drag_start_position_ - static_cast<float>(x - drag_start_x_) / sensitivity;
          target = std::clamp(target, drag_start_position_ - 1.0f, drag_start_position_ + 1.0f);
          renderer_->SetPosition(target);
        }

        const ULONGLONG now = GetTickCount64();
        const ULONGLONG dt = now - last_mouse_ms_;
        if (dt > 0) {
          mouse_velocity_ =
              static_cast<float>(x - last_mouse_x_) * 1000.0f / static_cast<float>(dt);
        }
        last_mouse_x_ = x;
        last_mouse_ms_ = now;

        if (mouse_moved_) {
          renderer_->Render();
        }
      }
      return 0;
    }

    case WM_LBUTTONUP:
      if (renderer_ && renderer_->InspectActive()) {
        if (inspect_dragging_) {
          inspect_dragging_ = false;
          ReleaseCapture();
        }
        return 0;
      }
      if (dragging_) {
        dragging_ = false;
        ReleaseCapture();
        const int x = GET_X_LPARAM(lparam);
        const int y = GET_Y_LPARAM(lparam);
        if (!mouse_moved_) {
          if (renderer_ && renderer_->CenterCardContains(x, y) && renderer_->BeginInspect()) {
            renderer_->Render();
            SetTimer(window_, kHintTimerId, 16, nullptr);
          }
        } else {
          FinishDrag();
        }
      }
      return 0;

    case WM_RBUTTONDOWN:
      if (scr_mode_) {
        DestroyWindow(window_);
        return 0;
      }
      if (renderer_ && renderer_->InspectActive()) {
        if (renderer_->InspectZoomed()) {
          renderer_->ResetInspectZoom();
          StartAnimationTimer();
        } else {
          renderer_->EndInspect();
        }
        renderer_->Render();
        return 0;
      }
      break;

    case WM_MBUTTONDOWN:
      if (scr_mode_) {
        DestroyWindow(window_);
        return 0;
      }
      break;

    case WM_SETCURSOR:
      if (LOWORD(lparam) == HTCLIENT) {
        SetCursor(scr_mode_ ? nullptr
                            : (cursor_visible_ ? LoadCursorW(nullptr, IDC_ARROW) : nullptr));
        return TRUE;
      }
      break;

    case WM_TIMER:
      switch (wparam) {
        case kAutoTimerId:
          if (renderer_ && !manual_paused_ && !hover_paused_ && !dragging_ &&
              !renderer_->IsAnimating() && !renderer_->InspectActive()) {
            Advance(1, false);
          }
          return 0;
        case kAnimationTimerId:
          if (renderer_) {
            if (!renderer_->Tick()) {
              KillTimer(window_, kAnimationTimerId);
            }
            renderer_->Render();
          }
          return 0;
        case kCursorTimerId:
          KillTimer(window_, kCursorTimerId);
          SetCursorVisible(false);
          return 0;
        case kHintTimerId:
          if (renderer_) {
            renderer_->Render();
            if (!renderer_->InspectHintActive()) {
              KillTimer(window_, kHintTimerId);
            }
          } else {
            KillTimer(window_, kHintTimerId);
          }
          return 0;
        case kGamepadTimerId: {
          const GamepadState state = gamepad_.Poll();
          SetTimer(window_, kGamepadTimerId, state.connected ? 16 : 500, nullptr);
          if (!state.connected) {
            gamepad_direction_ = 0;
            return 0;
          }
          if (scr_mode_) {
            if (state.any) {
              DestroyWindow(window_);
            }
            return 0;
          }

          const ULONGLONG now = GetTickCount64();

          if (renderer_ && renderer_->InspectActive()) {
            gamepad_direction_ = 0;

            if (state.b) {
              gamepad_zoom_direction_ = 0;
              if (renderer_->InspectZoomed()) {
                renderer_->ResetInspectZoom();
                StartAnimationTimer();
                  } else {
                renderer_->EndInspect();
                  }
              renderer_->Render();
              return 0;
            }

            bool changed = false;
            const float nx = NormalizeAxis(state.left_x);
            const float ny = NormalizeAxis(state.left_y);
            if (renderer_->InspectZoomed()) {
              if (nx != 0.0f || ny != 0.0f) {
                renderer_->PanInspect(nx * kGamepadPanPixelsPerTick,
                                      -ny * kGamepadPanPixelsPerTick);
                changed = true;
              }
            } else {
              if (nx != 0.0f || ny != 0.0f) {
                renderer_->RotateInspect(nx * kGamepadRotatePixelsPerTick,
                                         ny * kGamepadRotatePixelsPerTick);
                changed = true;
              }
              if (state.x) {
                renderer_->ResetInspectRotation();
                changed = true;
              }
            }

            const int zoom_direction =
                state.right_trigger > kGamepadTriggerThreshold
                    ? 1
                    : (state.left_trigger > kGamepadTriggerThreshold ? -1 : 0);
            if (zoom_direction == 0) {
              gamepad_zoom_direction_ = 0;
            } else if (zoom_direction != gamepad_zoom_direction_) {
              gamepad_zoom_direction_ = zoom_direction;
              renderer_->ZoomInspect(zoom_direction * 120);
              StartAnimationTimer();
                gamepad_zoom_repeat_at_ = now + kGamepadZoomIntervalMs;
              changed = true;
            } else if (now >= gamepad_zoom_repeat_at_) {
              renderer_->ZoomInspect(zoom_direction * 120);
              StartAnimationTimer();
                gamepad_zoom_repeat_at_ = now + kGamepadZoomIntervalMs;
              changed = true;
            }

            if (changed) {
              renderer_->Render();
            }
            return 0;
          }

          if (state.x && renderer_ && renderer_->BeginInspect()) {
            renderer_->Render();
            SetTimer(window_, kHintTimerId, 16, nullptr);
            gamepad_direction_ = 0;
            gamepad_zoom_direction_ = 0;
            return 0;
          }

          const int direction = state.left_held ? -1 : (state.right_held ? 1 : 0);
          if (direction == 0) {
            gamepad_direction_ = 0;
          } else if (direction != gamepad_direction_) {
            gamepad_direction_ = direction;
            gamepad_repeat_interval_ms_ = KeyboardRepeatIntervalMs();
            Advance(direction, true);
            gamepad_repeat_at_ = now + KeyboardRepeatDelayMs();
          } else if (now >= gamepad_repeat_at_) {
            Advance(direction, true);
            gamepad_repeat_at_ += gamepad_repeat_interval_ms_;
            if (gamepad_repeat_at_ < now) {
              gamepad_repeat_at_ = now + gamepad_repeat_interval_ms_;
            }
          }
          if (state.a) {
            manual_paused_ = !manual_paused_;
            ResetAutoTimer();
          }
          if (state.b) {
            DestroyWindow(window_);
            return 0;
          }
          if (state.y && click_player_) {
            click_player_->ToggleMute();
          }
          return 0;
        }
        default:
          break;
      }
      break;

    case WM_DISPLAYCHANGE:
      FitToPrimaryMonitor();
      return 0;

    case WM_DESTROY:
      KillTimer(window_, kAutoTimerId);
      KillTimer(window_, kAnimationTimerId);
      KillTimer(window_, kCursorTimerId);
      KillTimer(window_, kGamepadTimerId);
      KillTimer(window_, kHintTimerId);
      if (click_player_) {
        click_player_->Stop();
      }
      renderer_.reset();
      PostQuitMessage(0);
      return 0;

    case WM_NCDESTROY: {
      const LRESULT result = DefWindowProcW(window_, message, wparam, lparam);
      SetWindowLongPtrW(window_, GWLP_USERDATA, 0);
      window_ = nullptr;
      return result;
    }

    default:
      break;
  }

  return DefWindowProcW(window_, message, wparam, lparam);
}

}  // namespace sf

