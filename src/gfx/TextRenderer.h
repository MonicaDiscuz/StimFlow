#pragma once

#include <windows.h>

#include <d3d11.h>

#include <cstdint>
#include <string>

#include "gfx/Texture.h"

namespace sf {

// 用 GDI 把一行文字画到 32-bit DIB，再上传为 RGBA8 纹理。
// 返回的纹理 RGB 全白，alpha = 字形亮度。
Texture CreateTextTexture(ID3D11Device* device, ID3D11DeviceContext* context,
                          const std::wstring& text, std::uint32_t font_px,
                          std::uint32_t max_width);

}  // namespace sf
