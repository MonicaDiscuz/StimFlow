#include "gfx/TextRenderer.h"

#include "gfx/Hr.h"

#include <algorithm>
#include <stdexcept>
#include <vector>

namespace sf {
namespace {

struct GdiDc {
  HDC dc = nullptr;
  ~GdiDc() {
    if (dc != nullptr) {
      DeleteDC(dc);
    }
  }
};

struct GdiObject {
  HGDIOBJ object = nullptr;
  ~GdiObject() {
    if (object != nullptr) {
      DeleteObject(object);
    }
  }
};

struct GdiSelection {
  HDC dc = nullptr;
  HGDIOBJ previous = nullptr;
  ~GdiSelection() {
    if (dc != nullptr && previous != nullptr) {
      SelectObject(dc, previous);
    }
  }
};

}  // namespace

Texture CreateTextTexture(ID3D11Device* device, ID3D11DeviceContext* context,
                          const std::wstring& text, std::uint32_t font_px,
                          std::uint32_t max_width) {
  if (text.empty() || font_px == 0 || max_width == 0) {
    return {};
  }

  GdiDc dc{CreateCompatibleDC(nullptr)};
  if (dc.dc == nullptr) {
    throw std::runtime_error("CreateCompatibleDC 失败");
  }

  GdiObject font{CreateFontW(-static_cast<int>(font_px), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE,
                             FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                             ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
                             L"Microsoft YaHei UI")};
  if (font.object == nullptr) {
    throw std::runtime_error("CreateFontW 失败");
  }

  GdiSelection font_selection{dc.dc, SelectObject(dc.dc, font.object)};

  SIZE measured{};
  GetTextExtentPoint32W(dc.dc, text.c_str(), static_cast<int>(text.size()), &measured);
  const std::uint32_t padding = std::max(8u, font_px / 3);
  const std::uint32_t width =
      std::min(max_width, static_cast<std::uint32_t>(std::max<LONG>(1, measured.cx)) + padding * 2);
  const std::uint32_t height = static_cast<std::uint32_t>(font_px * 1.5f);

  BITMAPINFO info{};
  info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  info.bmiHeader.biWidth = static_cast<LONG>(width);
  info.bmiHeader.biHeight = -static_cast<LONG>(height);  // top-down
  info.bmiHeader.biPlanes = 1;
  info.bmiHeader.biBitCount = 32;
  info.bmiHeader.biCompression = BI_RGB;

  void* bits = nullptr;
  GdiObject bitmap{CreateDIBSection(dc.dc, &info, DIB_RGB_COLORS, &bits, nullptr, 0)};
  if (bitmap.object == nullptr || bits == nullptr) {
    throw std::runtime_error("CreateDIBSection 失败");
  }

  GdiSelection bitmap_selection{dc.dc, SelectObject(dc.dc, bitmap.object)};
  PatBlt(dc.dc, 0, 0, static_cast<int>(width), static_cast<int>(height), BLACKNESS);
  SetBkMode(dc.dc, TRANSPARENT);
  SetTextColor(dc.dc, RGB(255, 255, 255));

  RECT rect{0, 0, static_cast<LONG>(width), static_cast<LONG>(height)};
  DrawTextW(dc.dc, text.c_str(), -1, &rect,
            DT_CENTER | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
  GdiFlush();

  const auto* bgra = static_cast<const std::uint8_t*>(bits);
  std::vector<std::uint8_t> rgba(static_cast<std::size_t>(width) * height * 4);
  for (std::size_t i = 0; i < static_cast<std::size_t>(width) * height; ++i) {
    const std::uint8_t blue = bgra[i * 4 + 0];
    const std::uint8_t green = bgra[i * 4 + 1];
    const std::uint8_t red = bgra[i * 4 + 2];
    const std::uint8_t alpha = std::max(red, std::max(green, blue));
    rgba[i * 4 + 0] = 255;
    rgba[i * 4 + 1] = 255;
    rgba[i * 4 + 2] = 255;
    rgba[i * 4 + 3] = alpha;
  }

  return CreateTextureFromRgba(device, context, rgba, width, height, false);
}

}  // namespace sf
