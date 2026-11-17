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

Write-Host ""
if ($problems.Count -eq 0) {
    Write-Host "TIP DLL verification passed" -ForegroundColor Green
    exit 0
}
foreach ($p in $problems) {
    Write-Host "  - $p" -ForegroundColor Red
}
exit 1
