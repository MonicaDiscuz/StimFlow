# 构建

> 只想使用程序的话看 [README](../README.md)。

## 环境

- Windows 10/11 x64
- Visual Studio 2022 或 2026 Build Tools，安装“使用 C++ 的桌面开发”
- Windows SDK 10.0.17763 或更新（需要 `fxc.exe`）
- CMake 3.25+

不需要 vcpkg、Python 或任何第三方库。

## 构建

最省事：

```powershell
.\build.ps1
```

脚本会通过 `vswhere` 找 VS 和它自带的 CMake，生成 Visual Studio 工程并编译。产物在 `build\Release\StimFlow.exe`。

也可以手动：

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

或者：

```powershell
cmake -S . -B build
cmake --build build --config Release
```

## 调试

```powershell
.\build.ps1 -Configuration Debug
```

Debug 构建会打开 D3D11 调试层（系统没装图形工具时自动退回），退出时调用 `ID3D11Debug::ReportLiveDeviceObjects` 输出未释放的对象。

## 运行时文件

程序在 exe 同目录读写 `StimFlow.ini`，保存上次文件夹、缓存目录和缓存开关。缩略图缓存默认放在非系统盘，也可以在 ini 里用 `cache_folder` 指定；`cache_enabled=0` 时只用内存。

## 打包

```powershell
.\tools\package.ps1
```

产物是 `dist\StimFlow-0.1.0-win64.zip`，包含 exe、LICENSE、README、BUILD 和热键脚本。`build/`、`dist/`、`artifacts/` 不参与打包。
