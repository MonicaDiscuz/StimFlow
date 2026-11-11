#include "app/Settings.h"

#include <windows.h>
#include <shlobj.h>

#include <cwctype>
#include <string>

namespace sf {
namespace {

std::filesystem::path ExecutableDirectory() {
  std::wstring buffer(MAX_PATH, L'\0');
  for (;;) {
    const DWORD length =
        GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0) {
      return {};
    }
    if (length < buffer.size()) {
      buffer.resize(length);
      break;
    }
    buffer.resize(buffer.size() * 2);
  }
  return std::filesystem::path(buffer).parent_path();
}

std::filesystem::path IniPath() { return ExecutableDirectory() / "StimFlow.ini"; }

std::filesystem::path DefaultPicturesFolder() {
  PWSTR value = nullptr;
  if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Pictures, 0, nullptr, &value))) {
    std::filesystem::path result(value);
    CoTaskMemFree(value);
    return result;
  }
  return {};
}

// 选一个非系统固定盘，剩余空间最大的。
std::filesystem::path DefaultCacheFolder() {
  wchar_t windows[MAX_PATH];
  if (GetWindowsDirectoryW(windows, MAX_PATH) == 0) {
    return {};
  }
  const wchar_t system_drive = static_cast<wchar_t>(towupper(windows[0]));

  const DWORD mask = GetLogicalDrives();
  wchar_t best_drive = 0;
  ULARGE_INTEGER best_free{};

  for (wchar_t drive = L'A'; drive <= L'Z'; ++drive) {
    if ((mask & (1u << (drive - L'A'))) == 0) {
      continue;
    }
    if (towupper(drive) == system_drive) {
      continue;
    }

    wchar_t root[4] = {drive, L':', L'\\', L'\0'};
    if (GetDriveTypeW(root) != DRIVE_FIXED) {
      continue;
    }

    ULARGE_INTEGER free_bytes{};
    if (!GetDiskFreeSpaceExW(root, &free_bytes, nullptr, nullptr)) {
      continue;
    }
    if (best_drive == 0 || free_bytes.QuadPart > best_free.QuadPart) {
      best_drive = drive;
      best_free = free_bytes;
    }
  }

  if (best_drive == 0) {
    return {};
  }
  wchar_t path[32];
  swprintf_s(path, L"%c:\\StimFlowCache", best_drive);
  return path;
}

std::wstring ReadString(const wchar_t* section, const wchar_t* key, const wchar_t* fallback,
                        const std::filesystem::path& ini) {
  wchar_t buffer[1024];
  GetPrivateProfileStringW(section, key, fallback, buffer, 1024, ini.c_str());
  return buffer;
}

}  // namespace

Settings Settings::Load() {
  Settings settings;
  const std::filesystem::path ini = IniPath();

  settings.last_folder = ReadString(L"paths", L"last_folder", L"", ini);
  if (settings.last_folder.empty()) {
    settings.last_folder = DefaultPicturesFolder();
  }

  settings.cache_folder = ReadString(L"paths", L"cache_folder", L"", ini);
  if (settings.cache_folder.empty()) {
    settings.cache_folder = DefaultCacheFolder();
  }

  settings.cache_enabled = GetPrivateProfileIntW(L"options", L"cache_enabled", 1, ini.c_str()) != 0;
  if (settings.cache_folder.empty()) {
    settings.cache_enabled = false;
  }
  settings.recursive = GetPrivateProfileIntW(L"options", L"recursive", 1, ini.c_str()) != 0;
  return settings;
}

void Settings::Save() const {
  const std::filesystem::path ini = IniPath();
  WritePrivateProfileStringW(L"paths", L"last_folder", last_folder.c_str(), ini.c_str());
  WritePrivateProfileStringW(L"paths", L"cache_folder", cache_folder.c_str(), ini.c_str());
  // cache_folder 为空表示没有可用的非系统盘，此时不要把手动/自动的 cache_enabled
  // 永久写成 0；下次有盘时仍可重新启用磁盘缓存。
  if (!cache_folder.empty()) {
    WritePrivateProfileStringW(L"options", L"cache_enabled", cache_enabled ? L"1" : L"0",
                               ini.c_str());
  }
  WritePrivateProfileStringW(L"options", L"recursive", recursive ? L"1" : L"0", ini.c_str());
}

}  // namespace sf
