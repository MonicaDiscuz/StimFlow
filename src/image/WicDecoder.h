#pragma once

#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <cstdint>
#include <filesystem>
#include <vector>

namespace sf {

// WIC 解码器：JPEG / PNG / BMP / GIF（第一帧）/ TIFF（第一页）/ ICO。
// 支持 EXIF 方向；按 max_width / max_height 等比缩小。
class WicDecoder {
 public:
  WicDecoder();
  ~WicDecoder();

  WicDecoder(const WicDecoder&) = delete;
  WicDecoder& operator=(const WicDecoder&) = delete;

  bool Decode(const std::filesystem::path& path, std::uint32_t max_width,
              std::uint32_t max_height, std::vector<std::uint8_t>& pixels,
              std::uint32_t& width, std::uint32_t& height);

 private:
  Microsoft::WRL::ComPtr<IWICImagingFactory> factory_;
};

}  // namespace sf
