#include <windows.h>
#include <objbase.h>
#include <shellapi.h>

#include <algorithm>
#include <cwctype>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "app/Application.h"
#include "gfx/Hr.h"

namespace {

std::wstring Lower(std::wstring text) {
  std::transform(text.begin(), text.end(), text.begin(),
                 [](wchar_t character) { return static_cast<wchar_t>(towlower(character)); });
  return text;
}

std::vector<std::wstring> Arguments() {
  int argc = 0;
  LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
  std::vector<std::wstring> result;
  if (argv == nullptr) {
    return result;
  }
  for (int i = 1; i < argc; ++i) {
    result.emplace_back(argv[i]);
  }
  LocalFree(argv);
  return result;
}

bool IsScreenSaverInvocation(const std::vector<std::wstring>& arguments) {
  wchar_t path[MAX_PATH];
  const DWORD length = GetModuleFileNameW(nullptr, path, MAX_PATH);
  bool scr = false;
  if (length > 0) {
    const std::wstring extension =
        Lower(std::filesystem::path(std::wstring(path, length)).extension().wstring());
    scr = (extension == L".scr");
  }

  for (const std::wstring& argument : arguments) {
    if (Lower(argument) == L"/s") {
      scr = true;
    }
  }
  return scr;
}

bool HasConfigOrPreview(const std::vector<std::wstring>& arguments) {
  for (const std::wstring& argument : arguments) {
    const std::wstring lower = Lower(argument);
    if (lower == L"/c" || lower == L"/p") {
      return true;
    }
  }
  return false;
}

class ComScope {
 public:
  ComScope() {
    const HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    initialized_ = SUCCEEDED(hr);
  }

  ~ComScope() {
    if (initialized_) {
      CoUninitialize();
    }
  }

  ComScope(const ComScope&) = delete;
  ComScope& operator=(const ComScope&) = delete;

 private:
  bool initialized_ = false;
};

std::wstring Widen(std::string_view text) {  if (text.empty()) {
    return {};
  }

  const int length = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                                         nullptr, 0);
  std::wstring result(static_cast<std::size_t>(length), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), length);
  return result;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
  SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

  const ComScope com;

  const std::vector<std::wstring> arguments = Arguments();
  if (HasConfigOrPreview(arguments)) {
    return 0;  // 没有配置界面，也不支持预览
  }
  const bool scr_mode = IsScreenSaverInvocation(arguments);

  try {
    sf::Application application{instance, scr_mode};
    return application.Run();
  } catch (const std::exception& error) {
    const std::wstring message = Widen(error.what());
    MessageBoxW(nullptr, message.c_str(), L"StimFlow", MB_OK | MB_ICONERROR);
    return 1;
  }
}
