# scripts/verify-tip-dll.ps1
# 校验 Round 2 的 TIP（weasel-fluent-tsf.dll）：
#   1. 存在，且导出 DllGetClassObject / DllCanUnloadNow / DllRegisterServer /
#      DllUnregisterServer 四个 COM 入口；
#   2. 与主程序使用**同一个** IPC 管道名，且该名字与官方 Weasel 隔离
#      （官方是 WeaselNamedPipe，本仓是 WeaselFluentNamedPipe）——
#      两端不一致的话 TIP 连不上服务；与官方同名的话官方 WeaselServer.exe
#      会占住管道，本仓服务起不来；
#   3. 注册逻辑走本仓的 tip/weasel_tsf_register.cpp，上游的 Register.cpp
#      不参与编译（它写 HKLM、要管理员，破坏「每用户免提权」安装）。
#
# 用法：
#   powershell -NoProfile -ExecutionPolicy Bypass -File scripts/verify-tip-dll.ps1
#
# 退出码：0 = 通过；1 = 有检查失败

$ErrorActionPreference = "Continue"
$RepoRoot = Split-Path -Parent $PSScriptRoot

$problems = @()

function Find-Artifact {
    param([string]$Name)
    $candidates = @(
        (Join-Path $RepoRoot "build\windows\x64\release\$Name"),
        (Join-Path $RepoRoot "build\Release\$Name")
    )
    return ($candidates | Where-Object { Test-Path $_ } | Select-Object -First 1)
}

# 读二进制里的 UTF-16LE 字符串（Windows 的宽字符串常量以 UTF-16LE 存放）
function Read-WideStrings {
    param([string]$Path)
    $bytes = [System.IO.File]::ReadAllBytes($Path)
    return [System.Text.Encoding]::Unicode.GetString($bytes)
}

function Read-AsciiStrings {
    param([string]$Path)
    $bytes = [System.IO.File]::ReadAllBytes($Path)
    return [System.Text.Encoding]::ASCII.GetString($bytes)
}

Write-Host "=== TIP DLL verification ===" -ForegroundColor Cyan

$dll = Find-Artifact "weasel-fluent-tsf.dll"
$exe = Find-Artifact "weasel-fluent-cpp-qt.exe"

if (-not $dll) {
    Write-Host "FAIL: weasel-fluent-tsf.dll not found (run 'xmake build' first)" -ForegroundColor Red
    exit 1
}
Write-Host "TIP DLL: $dll"

# ── 1. 导出表 ───────────────────────────────────────────────────────────
$required = @("DllGetClassObject", "DllCanUnloadNow", "DllRegisterServer", "DllUnregisterServer")

$dumpbin = Get-ChildItem -Path "C:\Program Files\Microsoft Visual Studio" `
    -Filter "dumpbin.exe" -Recurse -ErrorAction SilentlyContinue |
    Where-Object { $_.FullName -match "Tools\\MSVC\\.*\\bin\\HostX64\\x64" } |
    Select-Object -First 1 -ExpandProperty FullName

$exports = @()
if ($dumpbin) {
    $raw = & $dumpbin /exports $dll 2>&1 | Out-String
    foreach ($name in $required) {
        if ($raw -match ("\b" + [regex]::Escape($name) + "\b")) {
            $exports += $name
        }
    }
} else {
    # 没有 dumpbin 时退化为「导出名出现在二进制里」的粗检。
    Write-Host "note: dumpbin not found, falling back to a binary string scan" -ForegroundColor Yellow
    $ascii = Read-AsciiStrings $dll
    foreach ($name in $required) {
        if ($ascii.Contains($name)) {
            $exports += $name
        }
    }
}

foreach ($name in $required) {
    if ($exports -notcontains $name) {
        $problems += "missing export: $name"
        Write-Host "FAIL: missing export $name" -ForegroundColor Red
    }
}
if ($problems.Count -eq 0) {
    Write-Host "PASS: all 4 COM entry points are exported" -ForegroundColor Green
}

# ── 2. IPC 管道名（两端一致 + 与官方隔离） ───────────────────────────────
$dllWide = Read-WideStrings $dll
if (-not $dllWide.Contains("WeaselFluentNamedPipe")) {
    $problems += "TIP does not use the isolated pipe name"
    Write-Host "FAIL: TIP DLL does not contain WeaselFluentNamedPipe" -ForegroundColor Red
}
if ($dllWide.Contains("WeaselNamedPipe")) {
    $problems += "TIP still uses the upstream pipe name"
    Write-Host "FAIL: TIP DLL still contains the upstream pipe name WeaselNamedPipe" -ForegroundColor Red
}

if (-not $exe) {
    Write-Host "WARN: main executable not found, skipping the two-end consistency check" -ForegroundColor Yellow
} else {
    $exeWide = Read-WideStrings $exe
    if (-not $exeWide.Contains("WeaselFluentNamedPipe")) {
        $problems += "server does not use the isolated pipe name"
        Write-Host "FAIL: main executable does not contain WeaselFluentNamedPipe" -ForegroundColor Red
    } else {
        Write-Host "PASS: TIP and server agree on WeaselFluentNamedPipe" -ForegroundColor Green
    }
}

# ── 3. 注册实现来源 ─────────────────────────────────────────────────────
$ourRegister = Join-Path $RepoRoot "tip\weasel_tsf_register.cpp"
if (-not (Test-Path $ourRegister)) {
    $problems += "tip/weasel_tsf_register.cpp is missing"
    Write-Host "FAIL: tip/weasel_tsf_register.cpp is missing" -ForegroundColor Red
} else {
    $xmake = Get-Content (Join-Path $RepoRoot "xmake.lua") -Raw
    if ($xmake -notmatch 'remove_files\("3rdparty/weasel/WeaselTSF/Register\.cpp"\)') {
        $problems += "upstream Register.cpp is still compiled in"
        Write-Host "FAIL: upstream WeaselTSF/Register.cpp is not excluded from the build" -ForegroundColor Red
    } else {
        Write-Host "PASS: upstream Register.cpp is excluded, per-user registration is used" -ForegroundColor Green
    }
}

# ── 4. 语言栏（输入法工具栏）资源 ─────────────────────────────────────────
#
# 为什么必须查：语言栏的图标和右键菜单是**运行时从 DLL 的资源段里加载**的
# （LanguageBar.cpp：LoadMenuW(g_hInst, MAKEINTRESOURCE(IDR_MENU_POPUP*)) /
# LoadImageW(g_hInst, MAKEINTRESOURCEW(IDI_ZH))）。
# 资源没编进去 ⇒ 编译链接全都正常，但语言栏右键菜单是空的、图标不显示 ——
# 属于「构建通过、功能却是坏的」这类最难发现的回归。
#
# ⚠️ 图标必须查 **RT_GROUP_ICON(14)** 而不是 RT_ICON(3)：
#    上游的 .ico 是多分辨率的（每张 6 幅：16/24/32/48 等），rc.exe 遇到多幅
#    .ico 会生成「RT_GROUP_ICON(14) + 若干张 RT_ICON(3) 图片资源」，
#    而 **IDI_ZH 之类的 ID 落在 GROUP_ICON 上**，图片资源的 ID 是自动编的 1..N。
#    查 RT_ICON(3) 永远查不到 ⇒ 会误报「图标缺失」。
#
# 资源 ID 取自 vendor 的 include/resource.h：
#   RT_MENU(4)：105 IDR_MENU_POPUP / 108 HANT / 109 HANS
#   RT_GROUP_ICON(14)：100 WEASEL / 101 EN / 102 ZH / 103 RELOAD / 106 FULL_SHAPE / 107 HALF_SHAPE
$menuIds = @(105, 108, 109)
$iconIds = @(100, 101, 102, 103, 106, 107)

if (-not ("WeaselTipResourceProbe" -as [type])) {
    Add-Type -TypeDefinition @"
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;

public static class WeaselTipResourceProbe {
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    private static extern IntPtr LoadLibraryExW(string lpFileName, IntPtr hFile, uint dwFlags);

    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern IntPtr FindResourceW(IntPtr hModule, IntPtr lpName, IntPtr lpType);

    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern bool FreeLibrary(IntPtr hModule);

    // LOAD_LIBRARY_AS_DATAFILE：只把资源段当数据读，**不执行 DllMain**。
    private const uint LOAD_LIBRARY_AS_DATAFILE = 0x2;
    private const int RT_ICON = 3;
    private const int RT_MENU = 4;
    private const int RT_GROUP_ICON = 14;

    // 返回的 key 形如 "RT_MENU:105"，value 表示该资源是否存在。
    public static Dictionary<string, bool> Probe(string path, int[] menuIds, int[] iconIds) {
        var result = new Dictionary<string, bool>();
        IntPtr h = LoadLibraryExW(path, IntPtr.Zero, LOAD_LIBRARY_AS_DATAFILE);
        if (h == IntPtr.Zero) {
            result["__loaded__"] = false;
            return result;
        }
        result["__loaded__"] = true;
        foreach (int id in menuIds) {
            result["RT_MENU:" + id] = FindResourceW(h, new IntPtr(id), new IntPtr(RT_MENU)) != IntPtr.Zero;
        }
        foreach (int id in iconIds) {
            result["RT_GROUP_ICON:" + id] = FindResourceW(h, new IntPtr(id), new IntPtr(RT_GROUP_ICON)) != IntPtr.Zero;
        }
        // 至少要有一张实际的 RT_ICON 图片数据（group icon 指向的那些）。
        result["RT_ICON:*"] = FindResourceW(h, new IntPtr(1), new IntPtr(RT_ICON)) != IntPtr.Zero;
        FreeLibrary(h);
        return result;
    }
}
"@
}

$probe = [WeaselTipResourceProbe]::Probe($dll, $menuIds, $iconIds)
if (-not $probe.ContainsKey("__loaded__") -or -not $probe["__loaded__"]) {
    $problems += "TIP DLL could not be opened as a data file (resource probe failed)"
    Write-Host "FAIL: cannot open the TIP DLL for resource enumeration" -ForegroundColor Red
} else {
    $missing = @()
    foreach ($key in ($probe.Keys | Sort-Object)) {
        if ($key -eq "__loaded__") { continue }
        if ($probe[$key]) {
            Write-Host ("    OK  resource {0}" -f $key) -ForegroundColor DarkGray
        } else {
            $missing += $key
        }
    }
    if ($missing.Count -gt 0) {
        $problems += ("missing language bar resources: " + ($missing -join ", "))
        Write-Host ("FAIL: missing language bar resources: " + ($missing -join ", ")) -ForegroundColor Red
    } else {
        Write-Host "PASS: language bar menus and icons are present in the TIP DLL" -ForegroundColor Green
    }
}

# 图标源文件也必须在仓库里（.rc 里引的是相对路径 ..\assets\langbar\*.ico），
# 且每张都要带齐语言栏/资源管理器会用到的分辨率。
$resourceDir = Join-Path $RepoRoot "assets\langbar"
$iconFiles = @("langbar-app.ico", "langbar-en.ico", "langbar-zh.ico",
                "langbar-reload.ico", "langbar-full.ico", "langbar-half.ico")
$expectedSizes = @(16, 20, 24, 32, 40, 48, 64, 256)

Add-Type -AssemblyName System.Drawing
foreach ($icon in $iconFiles) {
    $path = Join-Path $resourceDir $icon
    if (-not (Test-Path $path)) {
        $problems += "missing icon source: assets/langbar/$icon"
        Write-Host "FAIL: missing icon source $icon" -ForegroundColor Red
        continue
    }
    # 读 .ico 里的每一帧尺寸
    $raw = [System.IO.File]::ReadAllBytes($path)
    $count = [BitConverter]::ToUInt16($raw, 4)
    $sizes = @()
    for ($i = 0; $i -lt $count; $i++) {
        $off = 6 + 16 * $i
        $w = $raw[$off]; if ($w -eq 0) { $w = 256 }
        $h = $raw[$off + 1]; if ($h -eq 0) { $h = 256 }
        $sizes += $w
    }
    $missingSizes = @($expectedSizes | Where-Object { $sizes -notcontains $_ })
    if ($missingSizes.Count -gt 0) {
        $problems += ("icon $icon is missing resolutions: " + ($missingSizes -join ", "))
        Write-Host ("FAIL: $icon is missing resolutions " + ($missingSizes -join ", ")) -ForegroundColor Red
    } else {
        Write-Host ("    OK  icon $icon has all 8 resolutions") -ForegroundColor DarkGray
    }
}

# .rc 里引用的图标路径必须都指向真实存在的文件（rc.exe 找不到时**不报错**，
# 只是静默丢掉那个资源 ⇒ 编译链接全绿但图标缺失）。
$rcPath = Join-Path $RepoRoot "tip\weasel-tsf.rc"
if (-not (Test-Path $rcPath)) {
    $problems += "tip/weasel-tsf.rc is missing"
    Write-Host "FAIL: tip/weasel-tsf.rc is missing" -ForegroundColor Red
} else {
    # .rc 是 UTF-16LE（BOM FF FE）
    $rcText = [System.IO.File]::ReadAllText($rcPath, [System.Text.Encoding]::Unicode)
    $refs = [regex]::Matches($rcText, 'ICON\s+"([^"]+)"') | ForEach-Object { $_.Groups[1].Value }
    if ($refs.Count -eq 0) {
        $problems += "tip/weasel-tsf.rc declares no ICON resources"
        Write-Host "FAIL: tip/weasel-tsf.rc declares no ICON resources" -ForegroundColor Red
    }
    foreach ($ref in $refs) {
        # .rc 里的路径是相对 .rc 自身所在目录的
        $resolved = [System.IO.Path]::GetFullPath(
            (Join-Path (Split-Path -Parent $rcPath) ($ref -replace '\\\\', '\')))
        if (-not (Test-Path $resolved)) {
            $problems += "the .rc references a missing icon file: $ref"
            Write-Host "FAIL: the .rc references a missing icon file: $ref" -ForegroundColor Red
        } else {
            Write-Host ("    OK  .rc -> {0}" -f (Split-Path -Leaf $resolved)) -ForegroundColor DarkGray
        }
    }
}

# ── 5. 简体中文菜单必须真的是简体 ─────────────────────────────────────────
#
# 上游的 IDR_MENU_POPUP_HANS 写的是**繁体**（「設定 / 詞典管理 / 重新啟動服務」），
# 而简体中文系统的语言栏加载的恰恰就是 HANS 那份菜单 ⇒ 用户看到的是繁体界面。
# 这里直接在 DLL 的字节里找 UTF-16LE 的简体菜单文字，断言它确实编进去了。
$dllBytes = [System.IO.File]::ReadAllBytes($dll)
$utf16 = [System.Text.Encoding]::Unicode.GetBytes("输入法设置")
$found = $false
for ($i = 0; $i -le $dllBytes.Length - $utf16.Length; $i++) {
    $hit = $true
    for ($j = 0; $j -lt $utf16.Length; $j++) {
        if ($dllBytes[$i + $j] -ne $utf16[$j]) { $hit = $false; break }
    }
    if ($hit) { $found = $true; break }
}
if ($found) {
    Write-Host "PASS: the Simplified Chinese language bar menu is embedded in the TIP DLL" -ForegroundColor Green
} else {
    $problems += "the Simplified Chinese menu text was not found in the TIP DLL"
    Write-Host "FAIL: the Simplified Chinese menu text was not found in the TIP DLL" -ForegroundColor Red
}

Write-Host ""
if ($problems.Count -eq 0) {
    Write-Host "TIP DLL verification passed" -ForegroundColor Green
    exit 0
}
foreach ($p in $problems) {
    Write-Host "  - $p" -ForegroundColor Red
}
exit 1
