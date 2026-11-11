# StimFlow 4KB intro

一个独立的 4KB intro 子项目，不是 StimFlow 主程序的一部分。

它不读取图片，也不使用 WIC。画面由 Direct3D 11 的全屏像素着色器实时生成：扇形排列的程序化 Cover Flow 卡片，带透视、旋转、阴影和倒影。

## 规格

- Windows x86（32 位 PE，64 位 Windows 通过 WoW64 运行）
- 无 CRT、无音频、无外部文件
- Direct3D 11，Shader Model 4.0
- Crinkler 3.0b 压缩
- 目标：`intro.exe` ≤ 4096 字节（当前约 2.9 KB）
- 键盘 `←` / `→` 切换中心卡片，`Esc` 退出

## 构建

需要：

- Visual Studio 2022/2026 Build Tools，含 C++ 桌面开发
- Windows SDK（含 `fxc.exe`）
- PowerShell 5.1

首次构建前下载 Crinkler：

```powershell
.\get-crinkler.ps1
```

然后：

```powershell
.\build.ps1
```

产物：`build\intro.exe`。脚本会打印实际字节数，超过 4096 会直接失败。

## 运行

双击 `build\intro.exe`，用 `←` / `→` 切换卡片，按 `Esc` 退出。

## 许可

intro 源码沿用仓库的 MIT 许可。Crinkler 是独立的构建工具，采用 Zlib 许可；`get-crinkler.ps1` 只负责下载，不把 Crinkler 提交进仓库。
