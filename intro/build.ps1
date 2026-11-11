#requires -Version 5.1
<#
  构建 StimFlow 4KB intro。

  产物：intro\build\intro.exe
  目标：≤ 4096 字节（32 位 PE，在 64 位 Windows 上通过 WoW64 运行）。
#>
[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$build = Join-Path $root 'build'
$src = Join-Path $root 'src'
$crinkler = Join-Path $root 'tools\crinkler\crinkler30b\Win32\Crinkler.exe'

if (-not (Test-Path -LiteralPath $crinkler)) {
    throw "找不到 Crinkler：$crinkler`n先运行 .\get-crinkler.ps1 下载。"
}

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path -LiteralPath $vswhere)) {
    throw '找不到 Visual Studio Installer。请安装 VS Build Tools（C++ 桌面开发 + Windows SDK）。'
}
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) {
    throw '找不到 C++ 工具链。'
}
$devcmd = Join-Path $vs 'Common7\Tools\VsDevCmd.bat'
if (-not (Test-Path -LiteralPath $devcmd)) {
    throw "找不到 VsDevCmd.bat：$devcmd"
}

New-Item -ItemType Directory -Force -Path $build | Out-Null

$cmd = @"
@echo off
call "$devcmd" -arch=x86 -host_arch=x64
if errorlevel 1 exit /b 1
fxc /nologo /T vs_4_0 /E VSMain /Fh "$build\shader_vs.h" /Vn g_vsCode "$src\shader.hlsl"
if errorlevel 1 exit /b 1
fxc /nologo /T ps_4_0 /E PSMain /Fh "$build\shader_ps.h" /Vn g_psCode "$src\shader.hlsl"
if errorlevel 1 exit /b 1
cl /nologo /c /O1 /Os /GS- /GR- /EHs- /fp:fast /Oi /Gy /Gw /Zl /std:c++17 /DWIN32_LEAN_AND_MEAN /DNOMINMAX /I"$build" /I"$src" "$src\main.cpp" /Fo"$build\main.obj"
if errorlevel 1 exit /b 1
"$crinkler" /OUT:"$build\intro.exe" /SUBSYSTEM:WINDOWS /ENTRY:Entry /NODEFAULTLIB /UNSAFEIMPORT /TRANSFORM:CALLS /UNALIGNCODE /NOINITIALIZERS /COMPMODE:SLOW /ORDERTRIES:1000 /PRINT:IMPORTS kernel32.lib user32.lib d3d11.lib "$build\main.obj"
if errorlevel 1 exit /b 1
"@

$cmdPath = Join-Path $build 'build.cmd'
[System.IO.File]::WriteAllText($cmdPath, $cmd, (New-Object System.Text.UTF8Encoding($false)))

& cmd.exe /c $cmdPath
if ($LASTEXITCODE -ne 0) {
    throw "构建失败（exit $LASTEXITCODE）"
}

$exe = Join-Path $build 'intro.exe'
$size = (Get-Item -LiteralPath $exe).Length
Write-Host ''
Write-Host ("intro.exe: {0} 字节" -f $size)
if ($size -gt 4096) {
    throw ("超过 4KB：{0} 字节" -f $size)
}
Write-Host '4KB OK'
