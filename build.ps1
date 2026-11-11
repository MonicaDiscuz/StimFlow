#requires -Version 5.1
<#
  一键构建 StimFlow。
  用法：
    .\build.ps1                 # Release
    .\build.ps1 -Configuration Debug
    .\build.ps1 -Clean          # 先删除 build\
#>
[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release',

    [switch]$Clean
)

$ErrorActionPreference = 'Stop'

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path $vswhere)) {
    throw '未找到 Visual Studio Installer。请先安装 Visual Studio Build Tools（含「使用 C++ 的桌面开发」工作负载）。'
}

$vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsPath) {
    throw '未找到 C++ 工具链。请在 Visual Studio Installer 里勾选「使用 C++ 的桌面开发」。'
}

$cmake = Join-Path $vsPath 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
if (-not (Test-Path $cmake)) {
    throw "未找到 CMake：$cmake。请在安装器里勾选「适用于 Windows 的 C++ CMake 工具」。"
}

$root = $PSScriptRoot
$build = Join-Path $root 'build'

if ($Clean -and (Test-Path $build)) {
    Remove-Item $build -Recurse -Force
}

& $cmake -S $root -B $build -A x64
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

& $cmake --build $build --config $Configuration --parallel
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host ''
Write-Host "构建完成：$build\$Configuration\StimFlow.exe"
