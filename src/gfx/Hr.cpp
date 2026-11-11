#include "gfx/Hr.h"

#include <cstdio>
#include <string>
#include <string_view>

namespace sf {
namespace {

std::wstring Widen(const char* text) {
  if (text == nullptr || *text == '\0') {
    return {};
  }

  const int length = MultiByteToWideChar(CP_UTF8, 0, text, -1, nullptr, 0);
  if (length <= 1) {
    return {};
  }

  std::wstring result(static_cast<std::size_t>(length), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, text, -1, result.data(), length);
  result.resize(static_cast<std::size_t>(length - 1));
  return result;
}

std::string Narrow(std::wstring_view text) {
  if (text.empty()) {
    return {};
  }

  const int length = WideCharToMultiByte(
      CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
  std::string result(static_cast<std::size_t>(length), '\0');
  WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), length,
                      nullptr, nullptr);
  return result;
}

std::wstring Describe(HRESULT hr, const char* what) {
  std::wstring message;

  wchar_t* buffer = nullptr;
  const DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                      FORMAT_MESSAGE_IGNORE_INSERTS;
  const DWORD length = FormatMessageW(flags, nullptr, static_cast<DWORD>(hr), 0,
                                      reinterpret_cast<wchar_t*>(&buffer), 0, nullptr);
  if (length != 0 && buffer != nullptr) {
    message.assign(buffer, length);
    LocalFree(buffer);
    while (!message.empty() &&
           (message.back() == L'\r' || message.back() == L'\n' || message.back() == L' ')) {
      message.pop_back();
    }
  }

  wchar_t code[16];
  swprintf_s(code, L"0x%08X", static_cast<unsigned int>(hr));

  std::wstring result = Widen(what);
  result += L" 失败（";
  result += code;
  result += L"）";
  if (!message.empty()) {
    result += L"：";
    result += message;
  }
  return result;
}

}  // namespace

HrError::HrError(HRESULT hr, const char* what)
    : std::runtime_error(Narrow(Describe(hr, what))), code_(hr) {}

void CheckHr(HRESULT hr, const char* what) {
  throw HrError(hr, what);
}

}  // namespace sf
