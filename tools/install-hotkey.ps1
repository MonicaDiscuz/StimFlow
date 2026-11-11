#requires -Version 5.1
<#
  在开始菜单创建 StimFlow 快捷方式，并绑定 Ctrl+Alt+G。
  热键由 Windows 资源管理器监听，不需要任何后台进程。
  删除该快捷方式即可移除热键。
#>
$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$candidates = @(
    (Join-Path $root 'StimFlow.exe'),
    (Join-Path $root 'build\Release\StimFlow.exe')
)
$exe = $candidates | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $exe) {
    throw '找不到 StimFlow.exe。'
}

$startMenu = Join-Path $env:APPDATA 'Microsoft\Windows\Start Menu\Programs'
$lnkPath = Join-Path $startMenu 'StimFlow.lnk'

$shell = New-Object -ComObject WScript.Shell
$shortcut = $shell.CreateShortcut($lnkPath)
$shortcut.TargetPath = $exe
$shortcut.WorkingDirectory = Split-Path -Parent $exe
$shortcut.Hotkey = 'Ctrl+Alt+G'
$shortcut.Description = 'StimFlow'
$shortcut.Save()

Write-Host "已创建：$lnkPath"
Write-Host '热键：Ctrl+Alt+G'
Write-Host '删除该快捷方式即可移除热键。'
