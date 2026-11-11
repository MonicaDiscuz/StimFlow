#requires -Version 5.1
<#
  下载 Crinkler 3.0b 到 intro\tools\crinkler。
  Crinkler 由 Rune L. H. Stubbe 和 Aske Simon Christensen 开发，Zlib 许可。
#>
[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$tools = Join-Path $root 'tools'
$dest = Join-Path $tools 'crinkler'
$zip = Join-Path $tools 'crinkler30b.zip'
$url = 'https://github.com/runestubbe/Crinkler/releases/download/v3.0b/crinkler30b.zip'

New-Item -ItemType Directory -Force -Path $tools | Out-Null
if (-not (Test-Path -LiteralPath $zip)) {
    Write-Host "下载 $url"
    Invoke-WebRequest -Uri $url -OutFile $zip
}
if (Test-Path -LiteralPath $dest) {
    Remove-Item -LiteralPath $dest -Recurse -Force
}
Expand-Archive -LiteralPath $zip -DestinationPath $dest
Write-Host "Crinkler: $(Join-Path $dest 'crinkler30b\Win32\Crinkler.exe')"
