#requires -Version 5.1
<#
  打包 4KB intro 发布 zip。

  产物：intro\dist\StimFlow-4KB-intro-win32.zip
  解压后只有 intro.exe、README.txt、LICENSE。
#>
[CmdletBinding()]
param(
    [switch]$SkipBuild
)

$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$repo = Split-Path -Parent $root
$build = Join-Path $root 'build'
$dist = Join-Path $root 'dist'
$stage = Join-Path $dist 'StimFlow-4KB-intro'
$zip = Join-Path $dist 'StimFlow-4KB-intro-win32.zip'

if (-not $SkipBuild) {
    & (Join-Path $root 'build.ps1')
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
}

$exe = Join-Path $build 'intro.exe'
if (-not (Test-Path -LiteralPath $exe)) {
    throw "找不到 $exe"
}

if (Test-Path -LiteralPath $stage) {
    Remove-Item -LiteralPath $stage -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $stage | Out-Null
Copy-Item -LiteralPath $exe (Join-Path $stage 'intro.exe')
Copy-Item -LiteralPath (Join-Path $repo 'LICENSE') (Join-Path $stage 'LICENSE')

$readme = @'
StimFlow 4KB intro

一个单文件 Windows 小程序，不需要安装、运行库或网络。

运行：
  双击 intro.exe。
  ← / → 切换卡片。
  Esc 退出。

系统：
  Windows 10 1809 或更新（64 位系统通过 WoW64 运行 32 位 exe）。
  需要 Direct3D 11；显卡驱动不支持时会退回 WARP 软件渲染。

说明：
  程序不写注册表、不联网、不生成额外文件。
  exe 未签名，首次运行可能被 SmartScreen 提示；选择“更多信息 → 仍要运行”。
  4KB 压缩程序也可能被杀毒软件误报，请自行确认来源。
'@

[System.IO.File]::WriteAllText(
    (Join-Path $stage 'README.txt'),
    $readme.TrimEnd() + "`n",
    (New-Object System.Text.UTF8Encoding($false))
)

if (Test-Path -LiteralPath $zip) {
    Remove-Item -LiteralPath $zip -Force
}
Compress-Archive -Path (Join-Path $stage '*') -DestinationPath $zip

$size = (Get-Item -LiteralPath $zip).Length
Write-Host ''
Write-Host "发布包：$zip"
Write-Host ("大小：{0} 字节" -f $size)
Write-Host '内容：intro.exe、README.txt、LICENSE'
