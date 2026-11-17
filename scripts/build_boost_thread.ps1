# scripts/build_boost_thread.ps1
# 手工编译 Boost.Thread 并把 Boost 库统一成简单名字。
#
# 为什么手工编：Boost 1.88 的 libs/thread/build/Jamfile.v2 用
# `<conditional>@requirements` 组织依赖，b2 判定不满足时**静默跳过**该 target
# （组件配置里显示 "thread: building"，但没有任何 .lib 产出），
# 于是链接期报一堆 boost::thread::start_thread_noexcept 未解析符号。
#
# Boost.Thread 的 Windows 实现只有 4 个编译单元，直接用 cl.exe 编即可：
#   libs/thread/src/win32/thread.cpp
#   libs/thread/src/win32/tss_dll.cpp
#   libs/thread/src/win32/tss_pe.cpp
#   libs/thread/src/win32/thread_primitives.cpp
#
# 同时把 b2 产出的带标签库名（libboost_serialization-vc143-mt-x64-1_88.lib）
# 复制成简单名字（boost_serialization.lib），方便 add_links("boost_serialization")。
#
# 用法：
#   powershell -ExecutionPolicy Bypass -File scripts/build_boost_thread.ps1

$ErrorActionPreference = "Stop"
$RepoRoot  = Split-Path -Parent $PSScriptRoot
$BoostRoot = Join-Path $RepoRoot "deps\boost_1_88_0"
$StageLib  = Join-Path $BoostRoot "stage\lib"
$ObjDir    = Join-Path $BoostRoot "stage\obj-thread"

if (-not (Test-Path $StageLib)) {
    throw "Boost stage/lib not found. Run scripts/build_boost.ps1 first."
}

# --- 1. 定位 MSVC 工具链 -------------------------------------------------
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vsPath  = (& $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath).Trim()
if (-not $vsPath) { throw "MSVC toolchain not found via vswhere." }
$vcvars = Join-Path $vsPath "VC\Auxiliary\Build\vcvars64.bat"
if (-not (Test-Path $vcvars)) { throw "vcvars64.bat not found at $vcvars" }

# --- 2. 编译 Boost.Thread 的 win32 源文件 --------------------------------
New-Item -ItemType Directory -Path $ObjDir -Force | Out-Null
$srcDir = Join-Path $BoostRoot "libs\thread\src\win32"
$sources = @("thread.cpp", "tss_dll.cpp", "tss_pe.cpp", "thread_primitives.cpp")

$cmdFile = Join-Path $env:TEMP "weasel_boost_thread.cmd"
$lines = @(
    "@echo off",
    "call `"$vcvars`" >nul",
    "cd /d `"$ObjDir`""
)
foreach ($s in $sources) {
    $src = Join-Path $srcDir $s
    $obj = Join-Path $ObjDir ($s -replace '\.cpp$', '.obj')
    $lines += "cl /c /nologo /MD /O2 /std:c++17 /EHsc /GS /W3 /utf-8 /DWIN32 /D_WINDOWS /DBOOST_THREAD_USE_LIB=1 /DBOOST_ALL_NO_LIB /DBOOST_USE_WINAPI_VERSION=0x0603 /I`"$BoostRoot`" /Fo`"$obj`" `"$src`""
    if ($LASTEXITCODE -ne 0) { throw "compile failed: $s" }
}
$objList = ($sources | ForEach-Object { '"' + (Join-Path $ObjDir ($_ -replace '\.cpp$', '.obj')) + '"' }) -join " "
$lines += "lib /nologo /OUT:`"$StageLib\boost_thread.lib`" $objList"
$lines += "exit /b %errorlevel%"
$lines | Set-Content -Path $cmdFile -Encoding ASCII

Write-Host "[1/3] Compiling Boost.Thread (win32)..." -ForegroundColor Cyan
cmd /c $cmdFile
if ($LASTEXITCODE -ne 0) { throw "Boost.Thread build failed with exit code $LASTEXITCODE" }

# --- 3. 把带标签的库名复制成简单名字 --------------------------------------
Write-Host "[2/3] Normalizing library names..." -ForegroundColor Cyan
$map = @{
    "boost_serialization" = "libboost_serialization-vc143-mt-x64-1_88.lib"
    "boost_system"        = "libboost_system-vc143-mt-x64-1_88.lib"
    "boost_atomic"        = "libboost_atomic-vc143-mt-x64-1_88.lib"
    "boost_regex"         = "libboost_regex-vc143-mt-x64-1_88.lib"
}
foreach ($k in $map.Keys) {
    $src = Join-Path $StageLib $map[$k]
    if (Test-Path $src) {
        Copy-Item $src (Join-Path $StageLib "$k.lib") -Force
        Write-Host ("      {0}.lib" -f $k) -ForegroundColor DarkGray
    } else {
        Write-Host ("      missing: {0}" -f $map[$k]) -ForegroundColor Yellow
    }
}

# --- 4. 验证 --------------------------------------------------------------
Write-Host "[3/3] Verifying..." -ForegroundColor Cyan
foreach ($k in @("boost_thread", "boost_serialization", "boost_system", "boost_atomic")) {
    $p = Join-Path $StageLib "$k.lib"
    if (-not (Test-Path $p)) { throw "Expected $k.lib missing in $StageLib" }
    Write-Host ("      OK {0}.lib ({1:N0} KB)" -f $k, ((Get-Item $p).Length / 1KB)) -ForegroundColor Green
}

Write-Host ""
Write-Host "Boost libraries ready in $StageLib" -ForegroundColor Green
