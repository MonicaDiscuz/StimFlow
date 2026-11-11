#requires -Version 5.1
<#
  30 分钟 / 12 小时浸泡测试（半自动）。

  使用前提：
    1. 已启动 StimFlow，并且已经选好图片文件夹；
    2. 程序可以停在虚拟桌面，脚本不需要它在前台；
    3. 电脑保持开机，系统睡眠设为“从不”。

  典型用法：
    powershell -ExecutionPolicy Bypass -File .\tools\soak_test.ps1 -Minutes 30
    powershell -ExecutionPolicy Bypass -File .\tools\soak_test.ps1 -Minutes 690

  只监控、不发输入：
    ... -NoInput

  说明：
    - 默认每 10 秒翻一页；
    - 每 2 分钟进入一次查看模式，滚轮缩放、拖动、再用右键安全退出；
    - 每 30 秒记录一次内存/工作集/句柄/GDI/USER/缓存；
    - 不发送 Esc，避免在非查看模式下误退出程序；
    - 测试结束不自动关闭 StimFlow，方便切回桌面检查。
#>
[CmdletBinding()]
param(
    [double]$Minutes = 30,
    [int]$PageIntervalSec = 10,
    [int]$InspectIntervalSec = 120,
    [int]$SampleIntervalSec = 30,
    [string]$CacheFolder = '',
    [string]$LogPath = '',
    [int]$WaitSeconds = 60,
    [switch]$NoInput
)

$ErrorActionPreference = 'Stop'

Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class SoakNative {
    public struct RECT { public int Left; public int Top; public int Right; public int Bottom; }

    [DllImport("user32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
    public static extern IntPtr FindWindowW(string className, string windowName);

    [DllImport("user32.dll")]
    public static extern uint GetWindowThreadProcessId(IntPtr hWnd, out uint processId);

    [DllImport("user32.dll")]
    public static extern bool GetClientRect(IntPtr hWnd, out RECT rect);

    [DllImport("user32.dll")]
    public static extern int GetSystemMetrics(int index);

    [DllImport("user32.dll")]
    public static extern bool PostMessage(IntPtr hWnd, uint msg, IntPtr wParam, IntPtr lParam);

    [DllImport("user32.dll")]
    public static extern uint GetGuiResources(IntPtr process, uint flags);
}
"@

$root = Split-Path -Parent $PSScriptRoot
if ([string]::IsNullOrWhiteSpace($LogPath)) {
    $stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
    $LogPath = Join-Path $root "artifacts\soak-$stamp.csv"
}
$logDir = Split-Path -Parent $LogPath
if ($logDir -and -not (Test-Path -LiteralPath $logDir)) {
    New-Item -ItemType Directory -Force -Path $logDir | Out-Null
}
$eventsLog = [System.IO.Path]::ChangeExtension($LogPath, '.events.log')

function Write-Event([string]$message) {
    $line = '{0} {1}' -f (Get-Date -Format 'yyyy-MM-dd HH:mm:ss'), $message
    Write-Host $line
    Add-Content -LiteralPath $eventsLog -Value $line -Encoding UTF8
}

function Make-LParam([int]$x, [int]$y) {
    return [IntPtr]((($y -band 0xFFFF) -shl 16) -bor ($x -band 0xFFFF))
}

function Read-CacheFolderFromIni([string]$exePath) {
    if ([string]::IsNullOrWhiteSpace($exePath)) { return '' }
    $ini = Join-Path (Split-Path -Parent $exePath) 'StimFlow.ini'
    if (-not (Test-Path -LiteralPath $ini)) { return '' }
    $section = ''
    foreach ($line in Get-Content -LiteralPath $ini -ErrorAction SilentlyContinue) {
        $text = $line.Trim()
        if ($text -match '^\[(.+)\]$') {
            $section = $matches[1]
            continue
        }
        if ($section -eq 'paths' -and $text -match '^cache_folder\s*=\s*(.*)$') {
            return $matches[1].Trim()
        }
    }
    return ''
}

function Get-CacheStats {
    if ([string]::IsNullOrWhiteSpace($CacheFolder) -or
        -not (Test-Path -LiteralPath $CacheFolder)) {
        return @{ Files = 0; MB = 0.0 }
    }
    $files = @(Get-ChildItem -LiteralPath $CacheFolder -File -ErrorAction SilentlyContinue)
    $bytes = ($files | Measure-Object -Property Length -Sum).Sum
    if ($null -eq $bytes) { $bytes = 0 }
    return @{
        Files = $files.Count
        MB = [math]::Round([double]$bytes / 1MB, 2)
    }
}

function Send-Key([int]$vk) {
    [void][SoakNative]::PostMessage($hwnd, 0x0100, [IntPtr]$vk, [IntPtr]1)
    [void][SoakNative]::PostMessage($hwnd, 0x0101, [IntPtr]$vk, [IntPtr]1)
}

function Send-MouseMove([int]$x, [int]$y) {
    [void][SoakNative]::PostMessage($hwnd, 0x0200, [IntPtr]0, (Make-LParam $x $y))
}

function Send-MouseDown([int]$x, [int]$y) {
    [void][SoakNative]::PostMessage($hwnd, 0x0201, [IntPtr]1, (Make-LParam $x $y))
}

function Send-MouseUp([int]$x, [int]$y) {
    [void][SoakNative]::PostMessage($hwnd, 0x0202, [IntPtr]0, (Make-LParam $x $y))
}

function Send-RightClick([int]$x, [int]$y) {
    [void][SoakNative]::PostMessage($hwnd, 0x0204, [IntPtr]2, (Make-LParam $x $y))
}

function Send-Wheel([int]$delta) {
    $wParam = [IntPtr](($delta -band 0xFFFF) -shl 16)
    [void][SoakNative]::PostMessage($hwnd, 0x020A, $wParam, (Make-LParam ([int]($width / 2)) ([int]($height / 2))))
}

function Set-MouseOutsideCards {
    # 顶端空白区，解除悬停暂停
    Send-MouseMove ([int]($width / 2)) 40
}

function Invoke-InspectCycle {
    $cx = [int]($width / 2)
    $cy = [int]($height * 0.60)

    # 单击中心卡。若没点中，后续滚轮只会在 Cover Flow 翻页，右键在 Cover Flow 无副作用。
    Send-MouseMove $cx $cy
    Send-MouseDown $cx $cy
    Start-Sleep -Milliseconds 60
    Send-MouseUp $cx $cy
    Start-Sleep -Milliseconds 700

    # 滚轮放大
    for ($i = 0; $i -lt 4; $i++) {
        Send-Wheel 120
        Start-Sleep -Milliseconds 150
    }
    Start-Sleep -Milliseconds 400

    # 拖动：查看模式里是旋转/平移，Cover Flow 里是拖卡片墙
    Send-MouseDown $cx $cy
    for ($i = 1; $i -le 10; $i++) {
        Send-MouseMove ($cx - $i * 12) ($cy + $i * 4)
        Start-Sleep -Milliseconds 20
    }
    Send-MouseUp ($cx - 120) ($cy + 40)
    Start-Sleep -Milliseconds 500

    # 右键安全退出：放大中第一次回 1×，第二次回 Cover Flow；
    # 如果本来就在 Cover Flow，右键没有副作用。
    Send-RightClick $cx $cy
    Start-Sleep -Milliseconds 1200
    Send-RightClick $cx $cy
    Start-Sleep -Milliseconds 700

    Set-MouseOutsideCards
    Write-Event 'input: inspect cycle'
}

# 等待 StimFlow 主窗口。虚拟桌面上的窗口仍然可以被 FindWindowW 找到。
$hwnd = [IntPtr]::Zero
$deadline = (Get-Date).AddSeconds($WaitSeconds)
while ((Get-Date) -lt $deadline) {
    $hwnd = [SoakNative]::FindWindowW('StimFlow.MainWindow', 'StimFlow')
    if ($hwnd -ne [IntPtr]::Zero) { break }
    Start-Sleep -Seconds 1
}
if ($hwnd -eq [IntPtr]::Zero) {
    throw "没有找到 StimFlow 主窗口。请先在虚拟桌面启动程序并选好文件夹，再运行本脚本。"
}

$processId = [uint32]0
[void][SoakNative]::GetWindowThreadProcessId($hwnd, [ref]$processId)
$process = Get-Process -Id $processId -ErrorAction Stop

$rect = New-Object SoakNative+RECT
[void][SoakNative]::GetClientRect($hwnd, [ref]$rect)
$width = $rect.Right - $rect.Left
$height = $rect.Bottom - $rect.Top
if ($width -le 0 -or $height -le 0) {
    $width = [SoakNative]::GetSystemMetrics(0)
    $height = [SoakNative]::GetSystemMetrics(1)
}

if ([string]::IsNullOrWhiteSpace($CacheFolder)) {
    $CacheFolder = Read-CacheFolderFromIni $process.Path
}
if ([string]::IsNullOrWhiteSpace($CacheFolder)) {
    $CacheFolder = Join-Path ([Environment]::GetFolderPath('LocalApplicationData')) 'StimFlowCache'
}

Write-Event "start pid=$processId window=${width}x${height} minutes=$Minutes cache=$CacheFolder"
Write-Event "log=$LogPath events=$eventsLog"

Set-MouseOutsideCards
Start-Sleep -Milliseconds 300

$start = Get-Date
$end = $start.AddMinutes($Minutes)
$nextSample = $start
$nextPage = $start.AddSeconds(5)
$nextInspect = $start.AddSeconds([math]::Max(20, $InspectIntervalSec))
$pageDirection = 1

while ((Get-Date) -lt $end) {
    $now = Get-Date
    $process.Refresh()
    if ($process.HasExited) {
        Write-Event "process exited at $($process.ExitCode)"
        break
    }

    if ($now -ge $nextSample) {
        $cache = Get-CacheStats
        $gdi = 0
        $user = 0
        try { $gdi = [SoakNative]::GetGuiResources($process.Handle, 0) } catch {}
        try { $user = [SoakNative]::GetGuiResources($process.Handle, 1) } catch {}

        $row = [pscustomobject]@{
            timestamp = $now.ToString('yyyy-MM-dd HH:mm:ss')
            elapsed_s = [math]::Round(($now - $start).TotalSeconds, 1)
            pid = $processId
            alive = (-not $process.HasExited)
            private_mb = [math]::Round($process.PrivateMemorySize64 / 1MB, 2)
            working_mb = [math]::Round($process.WorkingSet64 / 1MB, 2)
            handles = $process.HandleCount
            threads = $process.Threads.Count
            gdi = $gdi
            user = $user
            cpu_s = [math]::Round($process.TotalProcessorTime.TotalSeconds, 2)
            cache_files = $cache.Files
            cache_mb = $cache.MB
        }
        $row | Export-Csv -Path $LogPath -Append -NoTypeInformation -Encoding UTF8
        $nextSample = $nextSample.AddSeconds($SampleIntervalSec)
    }

    if (-not $NoInput) {
        if ($now -ge $nextInspect) {
            Invoke-InspectCycle
            $nextInspect = $now.AddSeconds($InspectIntervalSec)
            $nextPage = $now.AddSeconds($PageIntervalSec)
        } elseif ($now -ge $nextPage) {
            Send-Key (0x27)
            Set-MouseOutsideCards
            $pageDirection = -$pageDirection
            $nextPage = $nextPage.AddSeconds($PageIntervalSec)
        }
    }

    Start-Sleep -Milliseconds 500
}

Write-Event 'finished'
if (Test-Path -LiteralPath $LogPath) {
    $rows = @(Import-Csv -Path $LogPath)
    if ($rows.Count -gt 0) {
        $first = $rows[0]
        $last = $rows[$rows.Count - 1]
        Write-Host ''
        Write-Host "样本数：$($rows.Count)"
        Write-Host "私有内存：$($first.private_mb) MB -> $($last.private_mb) MB"
        Write-Host "工作集：  $($first.working_mb) MB -> $($last.working_mb) MB"
        Write-Host "句柄：    $($first.handles) -> $($last.handles)"
        Write-Host "GDI：     $($first.gdi) -> $($last.gdi)"
        Write-Host "USER：    $($first.user) -> $($last.user)"
        Write-Host "缓存文件：$($first.cache_files) -> $($last.cache_files)"
        Write-Host "缓存大小：$($first.cache_mb) MB -> $($last.cache_mb) MB"
        Write-Host "日志：$LogPath"
        Write-Host "事件：$eventsLog"
    }
}

Write-Host ''
Write-Host 'StimFlow 保持运行，方便切回虚拟桌面检查。'
