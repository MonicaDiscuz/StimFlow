#pragma once

#include <windows.h>

#include <stdexcept>

namespace sf {

// HRESULT 失败时抛出，附带可读的 Win32 系统消息。
class HrError : public std::runtime_error {
 public:
  HrError(HRESULT hr, const char* what);

  [[nodiscard]] HRESULT Code() const noexcept { return code_; }

 private:
  HRESULT code_;
};

[[noreturn]] void CheckHr(HRESULT hr, const char* what);

inline void Check(HRESULT hr, const char* what) {
  if (FAILED(hr)) {
    CheckHr(hr, what);
  }
}

// 立即读取 GetLastError()。只用于紧跟在 Win32 调用之后的判断。
inline void CheckWin32(BOOL success, const char* what) {
  if (success == FALSE) {
    CheckHr(HRESULT_FROM_WIN32(GetLastError()), what);
  }
}

}  // namespace sf
