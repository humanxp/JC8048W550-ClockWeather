# =============================================================================
#  patch_gfx.ps1 — 给 Arduino_GFX 打一行补丁（Windows / PowerShell 版）
#
#  为什么要打：
#      ClockWeatherSetup 用 esp_lcd_rgb_panel_restart() 修 ESP32-S3 RGB GDMA
#      的「永久失步」（跑一阵后随机花屏、只有重启才好）。这个函数要拿到
#      Arduino_ESP32RGBPanel 里的 _panel_handle，但它在 private 区段里，
#      不改库就会报：
#          error: 'esp_lcd_panel_t* Arduino_ESP32RGBPanel::_panel_handle'
#                 is private within this context
#
#  这个脚本做的事：把 _panel_handle 上面最近的那个 private: 改成 public:，
#  只动这一处，库的其它部分不受影响，并且会先备份一份 .bak。
#
#  用法（任选一种）：
#    1) 在本文件上右键 ->「使用 PowerShell 运行」
#    2) 打开 PowerShell，cd 到本文件所在目录，然后：
#         powershell -ExecutionPolicy Bypass -File .\patch_gfx.ps1
#    3) 自动找不到库时，把文件路径当参数传进去：
#         powershell -ExecutionPolicy Bypass -File .\patch_gfx.ps1 "C:\Users\MinG\Documents\Arduino\libraries\GFX_Library_for_Arduino\src\databus\Arduino_ESP32RGBPanel.h"
#
#  不想打补丁的替代方案：把工程里 board_pins.h 的
#      #define LCD_DMA_RESTART_FIX  1
#  改成 0，也能编译通过，代价是花屏只能靠按 RST 恢复。
# =============================================================================

$ErrorActionPreference = 'Stop'

function Resolve-GfxHeader {
    param([string[]]$PathArgs)

    # 1) 命令行里给了路径就用它
    if ($PathArgs -and $PathArgs.Count -ge 1 -and $PathArgs[0] -and (Test-Path $PathArgs[0])) {
        return (Resolve-Path $PathArgs[0]).Path
    }

    # 2) 常见 Arduino 库目录里找 GFX* 文件夹
    $userHome = $env:USERPROFILE
    $roots = @(
        (Join-Path $userHome 'Documents\Arduino\libraries'),
        (Join-Path $userHome 'Arduino\libraries'),
        (Join-Path $userHome 'OneDrive\Documents\Arduino\libraries'),
        'C:\Program Files (x86)\Arduino\libraries'
    )

    foreach ($root in $roots) {
        if (-not (Test-Path $root)) { continue }
        $hit = Get-ChildItem -Path $root -Directory -ErrorAction SilentlyContinue |
               Where-Object { $_.Name -like 'GFX*' } |
               ForEach-Object { Join-Path $_.FullName 'src\databus\Arduino_ESP32RGBPanel.h' } |
               Where-Object { Test-Path $_ } |
               Select-Object -First 1
        if ($hit) { return $hit }
    }
    return $null
}

$target = Resolve-GfxHeader -PathArgs $args

if (-not $target) {
    Write-Host ''
    Write-Host '没有自动找到 Arduino_ESP32RGBPanel.h。' -ForegroundColor Red
    Write-Host '请把它的完整路径作为参数再跑一次，例如：' -ForegroundColor Yellow
    Write-Host '  powershell -ExecutionPolicy Bypass -File .\patch_gfx.ps1 "C:\Users\MinG\Documents\Arduino\libraries\GFX_Library_for_Arduino\src\databus\Arduino_ESP32RGBPanel.h"'
    Write-Host ''
    exit 1
}

Write-Host "目标文件: $target" -ForegroundColor Cyan

# 按原始字节读，避免把 BOM / 换行 / 编码改坏
$bytes = [System.IO.File]::ReadAllBytes($target)
$text  = [System.Text.Encoding]::UTF8.GetString($bytes)

$i = $text.IndexOf('_panel_handle')
if ($i -lt 0) {
    Write-Host '这个文件里没有 _panel_handle，可能库版本不对。' -ForegroundColor Red
    exit 1
}

$before = $text.Substring(0, $i)
$p = $before.LastIndexOf('private:')
if ($p -lt 0) {
    Write-Host '已经是 public，不用改。' -ForegroundColor Green
    exit 0
}

# 算一下在第几行，方便你核对
$lineNo = ($before.Substring(0, $p) -split "`n").Count

Copy-Item $target "$target.bak" -Force

$out = $text.Substring(0, $p) + 'public:' + $text.Substring($p + 'private:'.Length)
[System.IO.File]::WriteAllText($target, $out, (New-Object System.Text.UTF8Encoding($false)))

Write-Host "已打补丁：第 $lineNo 行  private: -> public:" -ForegroundColor Green
Write-Host "备份文件：$target.bak" -ForegroundColor DarkGray
Write-Host ''
Write-Host '回 Arduino IDE 重新点「上传」即可。' -ForegroundColor Green
