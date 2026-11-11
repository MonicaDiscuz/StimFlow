#include "image/FolderLibrary.h"

#include <windows.h>
#include <shlwapi.h>

#include <algorithm>
#include <cwctype>
#include <system_error>

namespace sf {
namespace {

constexpr std::wstring_view kSupportedExtensions[] = {
    L".jpg", L".jpeg", L".jpe", L".jfif", L".png", L".bmp",
    L".dib", L".gif", L".tif", L".tiff", L".ico",
};

bool IsSupported(const std::filesystem::path& path) {
  std::wstring extension = path.extension().wstring();
  std::transform(extension.begin(), extension.end(), extension.begin(),
                 [](wchar_t character) { return static_cast<wchar_t>(towlower(character)); });
  for (std::wstring_view supported : kSupportedExtensions) {
    if (extension == supported) {
      return true;
    }
  }
  return false;
}

bool IsReparsePoint(const std::filesystem::path& path) {
  const DWORD attributes = GetFileAttributesW(path.c_str());
  return attributes != INVALID_FILE_ATTRIBUTES &&
         (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
}

}  // namespace

bool FolderLibrary::Scan(const std::filesystem::path& root, bool recursive) {
  entries_.clear();

  std::error_code error;
  if (!std::filesystem::is_directory(root, error)) {
    return false;
  }

  const auto options = std::filesystem::directory_options::skip_permission_denied;

  try {
    if (recursive) {
      for (auto it = std::filesystem::recursive_directory_iterator(root, options);
           it != std::filesystem::recursive_directory_iterator(); ++it) {
        if (it->is_directory()) {
          if (IsReparsePoint(it->path())) {
            it.disable_recursion_pending();
          }
          continue;
        }
        if (!it->is_regular_file() || !IsSupported(it->path())) {
          continue;
        }
        entries_.push_back(ImageEntry{it->path(), it->path().stem().wstring()});
      }
    } else {
      for (const auto& entry : std::filesystem::directory_iterator(root, options)) {
        if (!entry.is_regular_file() || !IsSupported(entry.path())) {
          continue;
        }
        entries_.push_back(ImageEntry{entry.path(), entry.path().stem().wstring()});
      }
    }
  } catch (const std::filesystem::filesystem_error&) {
    // 权限/坏链接：保留已经扫描到的部分
  }

  std::sort(entries_.begin(), entries_.end(), [](const ImageEntry& a, const ImageEntry& b) {
    const int comparison = StrCmpLogicalW(a.path.filename().c_str(), b.path.filename().c_str());
    if (comparison != 0) {
      return comparison < 0;
    }
    return a.path.wstring() < b.path.wstring();
  });

  return !entries_.empty();
}

}  // namespace sf
