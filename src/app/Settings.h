#pragma once

#include <filesystem>

namespace sf {

// 运行时设置：上次文件夹、缓存位置、是否递归。
// 存在 exe 同目录的 StimFlow.ini。
struct Settings {
  std::filesystem::path last_folder;
  std::filesystem::path cache_folder;
  bool cache_enabled = true;
  bool recursive = true;

  static Settings Load();
  void Save() const;
};

}  // namespace sf
