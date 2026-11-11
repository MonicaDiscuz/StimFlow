#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace sf {

struct ImageEntry {
  std::filesystem::path path;
  std::wstring title;
};

// 递归扫描一个文件夹，按文件名自然排序，标题取文件名（去扩展名）。
class FolderLibrary {
 public:
  bool Scan(const std::filesystem::path& root, bool recursive);

  [[nodiscard]] std::size_t Count() const noexcept { return entries_.size(); }
  [[nodiscard]] const ImageEntry& At(std::size_t index) const { return entries_[index]; }


 private:
  std::vector<ImageEntry> entries_;
};

}  // namespace sf
