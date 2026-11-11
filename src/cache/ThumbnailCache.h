#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

namespace sf {

// 缩略图磁盘缓存。
// - 文件名 = 规范化路径（小写）的 SHA-256，缓存目录里不出现明文路径。
// - 内容用 Windows DPAPI（当前用户）加密，离开本机或换用户都无法直接读取。
// - 只缓存 512 级别的缩略图；原图和查看模式的分块纹理不会落盘。
class ThumbnailCache {
 public:
  ThumbnailCache() = default;
  ~ThumbnailCache() = default;

  ThumbnailCache(const ThumbnailCache&) = delete;
  ThumbnailCache& operator=(const ThumbnailCache&) = delete;

  void Open(const std::filesystem::path& folder);
  void Disable() noexcept;

  [[nodiscard]] bool Enabled() const noexcept { return enabled_; }
  [[nodiscard]] bool HasSessionWrites() const noexcept { return !session_files_.empty(); }

  bool Load(const std::filesystem::path& source, std::vector<std::uint8_t>& pixels,
            std::uint32_t& width, std::uint32_t& height) const;
  void Store(const std::filesystem::path& source, const std::vector<std::uint8_t>& pixels,
             std::uint32_t width, std::uint32_t height);

  void ClearSession();
  void ClearAll();

 private:
  [[nodiscard]] std::filesystem::path FileFor(const std::filesystem::path& source) const;

  std::filesystem::path folder_;
  bool enabled_ = false;
  std::vector<std::filesystem::path> session_files_;
};

}  // namespace sf
