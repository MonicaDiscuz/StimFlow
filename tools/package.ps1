#requires -Version 5.1
<#
  打包发布 zip。
  用法：
    .\tools\package.ps1
    .\tools\package.ps1 -SkipBuild
  产物：dist\StimFlow-0.1.0-win64.zip
  不包含：build\、dist\、artifacts\、assets\audio（已嵌入 exe）
#>
[CmdletBinding()]
param(
    [switch]$SkipBuild
)

$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$version = '0.1.0'

if (-not $SkipBuild) {
    & (Join-Path $root 'build.ps1')
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}

$exe = Join-Path $root 'build\Release\StimFlow.exe'
if (-not (Test-Path $exe)) { throw "找不到 $exe" }

$dist = Join-Path $root 'dist'
$stage = Join-Path $dist "StimFlow-$version-win64"
if (Test-Path $stage) { Remove-Item $stage -Recurse -Force }
New-Item -ItemType Directory -Force -Path $stage | Out-Null

Copy-Item $exe (Join-Path $stage 'StimFlow.exe')
Copy-Item (Join-Path $root 'LICENSE') $stage
Copy-Item (Join-Path $root 'README.md') $stage

New-Item -ItemType Directory -Force -Path (Join-Path $stage 'docs') | Out-Null
Copy-Item (Join-Path $root 'docs\BUILD.md') (Join-Path $stage 'docs')

New-Item -ItemType Directory -Force -Path (Join-Path $stage 'tools') | Out-Null
Copy-Item (Join-Path $root 'tools\install-hotkey.ps1') (Join-Path $stage 'tools')

$zip = Join-Path $dist "StimFlow-$version-win64.zip"
if (Test-Path $zip) { Remove-Item $zip -Force }
Compress-Archive -Path (Join-Path $stage '*') -DestinationPath $zip

Write-Host ''
Write-Host "发布包：$zip"
$sizeMB = [math]::Round((Get-Item $zip).Length / 1MB, 1)
Write-Host "大小：$sizeMB MB"
Write-Host '内容：StimFlow.exe、LICENSE、README.md、docs\BUILD.md、tools\install-hotkey.ps1'
Write-Host '不包含：build\、dist\、artifacts\、assets\audio（已嵌入）'
