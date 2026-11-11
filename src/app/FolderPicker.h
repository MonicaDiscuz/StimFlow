#pragma once

#include <windows.h>

#include <filesystem>

namespace sf {

// 弹出文件夹选择框；取消时返回空路径。
std::filesystem::path PickFolder(HWND owner, const std::filesystem::path& initial);

}  // namespace sf
