$ErrorActionPreference = "Stop"

# scripts/verify-langbar-icons.ps1
# 把 **TIP DLL 资源段里的语言栏图标实际渲染出来**并断言其尺寸/内容。
#
# 为什么必须渲染而不是只查存在：
#   语言栏图标是运行时从 DLL 资源段加载的（LanguageBar.cpp 的
#   LoadImageW(g_hInst, MAKEINTRESOURCEW(IDI_ZH), IMAGE_ICON, ...)）。
#   「资源存在」只说明 FindResourceW 找得到 —— ico 的图像数据被 rc.exe
#   编坏（多分辨率 + PNG 压缩条目是重灾区）时，图标照样「存在」，
#   但画出来是糊的或变形的。这种回归编译链接全绿，只能靠渲染发现。
#
# 判定：
#   1. 每个 ID 都能 LoadImage 出一个非空 HICON；
#   2. 渲染出的位图**必须是正方形**（变形的直接证据）且边长 > 0；
#   3. 位图必须**有可见像素**（全透明 = 看不见的图标）。
#
# 用法：powershell -NoProfile -ExecutionPolicy Bypass -File scripts/verify-langbar-icons.ps1
# 退出码：0 = 通过；1 = 有检查失败

Add-Type -AssemblyName System.Drawing

if (-not ("LangBarIconProbe" -as [type])) {
    Add-Type -TypeDefinition @"
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
public static class LangBarIconProbe {
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    private static extern IntPtr LoadLibraryExW(string lpFileName, IntPtr hFile, uint dwFlags);
    [DllImport("kernel32.dll")]
    private static extern bool FreeLibrary(IntPtr hModule);
    [DllImport("user32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    private static extern IntPtr LoadImageW(IntPtr hInst, string lpName, uint uType,
                                           int cx, int cy, uint fuFlags);
    [DllImport("user32.dll", SetLastError = true)]
    private static extern bool DestroyIcon(IntPtr hIcon);
    [DllImport("user32.dll")]
    public static extern int GetSystemMetrics(int nIndex);

    private const uint LOAD_LIBRARY_AS_DATAFILE = 0x2;
    private const uint LOAD_LIBRARY_AS_IMAGE_RESOURCE = 0x20;
    private const uint IMAGE_ICON = 1;

    public class IconInfo {
        public bool loaded;
        public int width;
        public int height;
        public long visiblePixels;
        public long totalPixels;
        public string error;
    }

    // 渲染 DLL 资源里的某个图标 ID，返回尺寸与可见像素数。
    public static IconInfo Render(string path, int id, int cx, int cy) {
        var info = new IconInfo();
        IntPtr h = LoadLibraryExW(path, IntPtr.Zero,
                                  LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE);
        if (h == IntPtr.Zero) {
            info.error = "LoadLibraryExW failed: " + Marshal.GetLastWin32Error();
            return info;
        }
        IntPtr hIcon = LoadImageW(h, MAKEINTRESOURCE(id), IMAGE_ICON, cx, cy, 0);
        if (hIcon == IntPtr.Zero) {
            info.error = "LoadImageW failed: " + Marshal.GetLastWin32Error();
            FreeLibrary(h);
            return info;
        }
        try {
            using (var ico = System.Drawing.Icon.FromHandle(hIcon)) {
                using (var bmp = ico.ToBitmap()) {
                    info.loaded = true;
                    info.width = bmp.Width;
                    info.height = bmp.Height;
                    long visible = 0;
                    for (int y = 0; y < bmp.Height; y++) {
                        for (int x = 0; x < bmp.Width; x++) {
                            var px = bmp.GetPixel(x, y);
                            if (px.A > 0) {
                                visible++;
                            }
                        }
                    }
                    info.visiblePixels = visible;
                    info.totalPixels = (long)bmp.Width * bmp.Height;
                }
            }
        } finally {
            DestroyIcon(hIcon);
            FreeLibrary(h);
        }
        return info;
    }

    public static string MAKEINTRESOURCE(int id) {
        return "#" + id;
    }
}
"@ -ReferencedAssemblies System.Drawing
}

$RepoRoot = Split-Path -Parent $PSScriptRoot
$dll = Join-Path $RepoRoot 'build\Release\weasel-fluent-tsf.dll'
if (-not (Test-Path $dll)) {
    $dll = Join-Path $RepoRoot 'dist\weasel-fluent\weasel-fluent-tsf.dll'
}
if (-not (Test-Path $dll)) {
    Write-Host "SKIP: the TIP DLL was not found (build it first)" -ForegroundColor Yellow
    exit 2
}

# 资源 ID 取自 vendor 的 include/resource.h：
#   100 WEASEL / 101 EN / 102 ZH / 103 RELOAD / 106 FULL_SHAPE / 107 HALF_SHAPE
$icons = @(
    @{ Id = 102; Name = 'IDI_ZH (中文模式)' },
    @{ Id = 101; Name = 'IDI_EN (英文模式)' },
    @{ Id = 106; Name = 'IDI_FULL_SHAPE (全角)' },
    @{ Id = 107; Name = 'IDI_HALF_SHAPE (半角)' },
    @{ Id = 103; Name = 'IDI_RELOAD (重新加载)' },
    @{ Id = 100; Name = 'IDI_WEASEL (系统语言指示器)' }
)

$sm = [LangBarIconProbe]::GetSystemMetrics(11)   # SM_CXSMICON
$smY = [LangBarIconProbe]::GetSystemMetrics(12)   # SM_CYSMICON
Write-Host "SM_CXSMICON = $sm x SM_CYSMICON = $smY"
Write-Host "TIP DLL: $dll"

$failures = 0
foreach ($entry in $icons) {
    $info = [LangBarIconProbe]::Render($dll, $entry.Id, $sm, $smY)
    if (-not $info.loaded) {
        Write-Host ("FAIL: {0} (id={1}) did not render: {2}" -f $entry.Name, $entry.Id, $info.error) -ForegroundColor Red
        $failures++
        continue
    }
    $square = ($info.width -eq $info.height)
    $visible = $info.visiblePixels -gt 0
    $ok = $square -and $visible
    $flag = if ($ok) { 'PASS' } else { 'FAIL' }
    $color = if ($ok) { 'Green' } else { 'Red' }
    Write-Host ("{0}: {1} (id={2}) rendered {3}x{4}, visible pixels {5}/{6}" -f `
        $flag, $entry.Name, $entry.Id, $info.width, $info.height, $info.visiblePixels, $info.totalPixels) -ForegroundColor $color
    if (-not $square) {
        Write-Host "      the icon is NOT square => it is being stretched" -ForegroundColor Red
    }
    if (-not $visible) {
        Write-Host "      the icon is fully transparent => nothing would be drawn" -ForegroundColor Red
    }
    if (-not $ok) {
        $failures++
    }
}

# ── 系统语言指示器必须是**纯字形**，应用自己的图标必须是**圆角方块** ──────
#
# 任务栏里那一排系统图标中间，Windows 用 profile 的 IconFile/IconIndex
# 画输入法指示器。带底的方块在那里像个没渲染出来的黑块（微软拼音的「拼」
# 是纯字形）。而应用自己的托盘/标题栏图标仍然要保留方块 —— 那是应用的品牌。
#
# 判据用「不透明像素占比」：方块底几乎铺满（实测 98%），纯字形只有笔画（实测 52%），阈值取 75%。
function Test-IconCoverage {
    param([string]$File, [int]$Id, [string]$What)
    $info = [LangBarIconProbe]::Render($File, $Id, $sm, $smY)
    if (-not $info.loaded -or $info.totalPixels -le 0) {
        Write-Host ("FAIL: {0}: {1} (id={2}) did not render: {3}" -f $What, $File, $Id, $info.error) -ForegroundColor Red
        return $false
    }
    $ratio = $info.visiblePixels / [double]$info.totalPixels
    Write-Host ("  {0} coverage {1:P1} ({2}/{3})" -f $What, $ratio, $info.visiblePixels, $info.totalPixels)
    return $ratio
}

$tipCoverage = Test-IconCoverage -File $dll -Id 100 -What 'TIP 的 IDI_WEASEL（系统语言指示器）'
if ($tipCoverage -ge 0.75) {
    Write-Host "FAIL: the system language indicator must be a plain glyph (transparent background), not a filled tile" -ForegroundColor Red
    $failures++
}

$exe = Join-Path $RepoRoot 'build\Release\weasel-fluent-cpp-qt.exe'
if (-not (Test-Path $exe)) {
    $exe = Join-Path $RepoRoot 'build\windows\x64\release\weasel-fluent-cpp-qt.exe'
}
if (Test-Path $exe) {
    $exeCoverage = Test-IconCoverage -File $exe -Id 100 -What 'exe 的应用图标（托盘/标题栏）'
    if ($exeCoverage -lt 0.75) {
        Write-Host "FAIL: the application icon must stay a filled tile (it is the app's own identity)" -ForegroundColor Red
        $failures++
    }
}

Write-Host ""
if ($failures -eq 0) {
    Write-Host "Language bar icon rendering check passed" -ForegroundColor Green
    exit 0
}
Write-Host "$failures check(s) failed" -ForegroundColor Red
exit 1