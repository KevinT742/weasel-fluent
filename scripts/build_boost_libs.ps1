# scripts/build_boost_libs.ps1
# 直接从源码编译 Weasel 需要的 Boost 静态库。
#
# 为什么不用 b2：Boost 1.88 的 libs/{thread,chrono,filesystem,json}/build/Jamfile.v2
# 用 `<conditional>@requirements` 组织依赖，b2 判定不满足时**静默跳过**该 target
# （组件配置里显示 "thread: building"，但没有任何 .lib 产出）。结果就是链接期
# 报一堆 boost::thread::start_thread_noexcept / boost::archive::... 未解析符号，
# 而 b2 本身退出码为 0，看不出问题。
#
# 我们需要的库与其源文件：
#   boost_serialization : libs/serialization/src/*.cpp        （含 impl/*.ipp，由 .cpp 引入）
#   boost_thread         : libs/thread/src/win32/*.cpp + src/tss_null.cpp
#   boost_system         : libs/system/src/error_code.cpp
#   boost_atomic         : libs/atomic/src/lock_pool.cpp
#
# 全部按 /MD 编译，与 Qt 6.8.2 官方二进制（本仓 GUI 的 CRT）一致。
#
# 用法：
#   powershell -ExecutionPolicy Bypass -File scripts/build_boost_libs.ps1

$ErrorActionPreference = "Stop"
$RepoRoot   = Split-Path -Parent $PSScriptRoot
$BoostRoot  = Join-Path $RepoRoot "deps\boost_1_88_0"
$StageLib   = Join-Path $BoostRoot "stage\lib"
$ObjRoot    = Join-Path $BoostRoot "stage\obj-manual"

if (-not (Test-Path $BoostRoot)) {
    throw "Boost source tree not found at $BoostRoot (run scripts/build_boost.ps1 first)."
}
New-Item -ItemType Directory -Path $StageLib -Force | Out-Null

# --- 定位 MSVC 工具链 ------------------------------------------------------
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vsPath  = (& $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath).Trim()
if (-not $vsPath) { throw "MSVC toolchain not found via vswhere." }
$vcvars = Join-Path $vsPath "VC\Auxiliary\Build\vcvars64.bat"
if (-not (Test-Path $vcvars)) { throw "vcvars64.bat not found at $vcvars" }

function Get-SourceFiles {
    param([string]$RelativeDir, [string]$Pattern = "*.cpp")
    $dir = Join-Path $BoostRoot $RelativeDir
    if (-not (Test-Path $dir)) { return @() }
    return @(Get-ChildItem -Path $dir -Filter $Pattern -File -Recurse | ForEach-Object { $_.FullName })
}

# 每个库：名字 -> 源文件
$libs = @{
    "boost_serialization" = @(Get-SourceFiles "libs\serialization\src")
    "boost_thread"        = @(Get-SourceFiles "libs\thread\src\win32") +
                            @(Get-SourceFiles "libs\thread\src" "tss_null.cpp")
    "boost_system"        = @(Get-SourceFiles "libs\system\src")
    "boost_atomic"        = @(Get-SourceFiles "libs\atomic\src" "lock_pool.cpp")
}

foreach ($name in $libs.Keys) {
    $sources = $libs[$name]
    if ($sources.Count -eq 0) { throw "No sources found for $name" }

    $objDir = Join-Path $ObjRoot $name
    New-Item -ItemType Directory -Path $objDir -Force | Out-Null

    $cmdFile = Join-Path $env:TEMP ("weasel_boost_" + $name + ".cmd")
    $lines = @("@echo off", "call `"$vcvars`" >nul", "cd /d `"$objDir`"")
    $objList = @()
    $i = 0
    foreach ($src in $sources) {
        $obj = Join-Path $objDir ("o{0}.obj" -f $i)
        $i++
        $objList += "`"$obj`""
        # BOOST_THREAD_BUILD_LIB 让 boost 编译 win32/tss_pe.cpp —— 它才提供
        # boost::tss_cleanup_implemented()；缺了这个定义，链接期会报该符号未解析。
        # （tss_null.cpp 在 MSVC 下被 `!defined(_MSC_VER)` 排除，救不了。）
        $lines += "cl /c /nologo /MD /O2 /std:c++17 /EHsc /GS /W3 /utf-8 /DWIN32 /D_WINDOWS /DBOOST_ALL_NO_LIB /DBOOST_USE_WINAPI_VERSION=0x0603 /DBOOST_THREAD_USE_LIB=1 /DBOOST_THREAD_BUILD_LIB=1 /I`"$BoostRoot`" /Fo`"$obj`" `"$src`""
    }
    $lines += "lib /nologo /OUT:`"$StageLib\$name.lib`" $($objList -join ' ')"
    $lines += "exit /b %errorlevel%"
    $lines | Set-Content -Path $cmdFile -Encoding ASCII

    Write-Host ("[{0}] compiling {1} source file(s)..." -f $name, $sources.Count) -ForegroundColor Cyan
    cmd /c $cmdFile
    if ($LASTEXITCODE -ne 0) { throw "build failed for $name (exit $LASTEXITCODE)" }
}

# --- 验证 ------------------------------------------------------------------
Write-Host ""
foreach ($name in $libs.Keys) {
    $p = Join-Path $StageLib "$name.lib"
    if (-not (Test-Path $p)) { throw "Expected $name.lib missing" }
    Write-Host ("      OK {0}.lib ({1:N0} KB)" -f $name, ((Get-Item $p).Length / 1KB)) -ForegroundColor Green
}
Write-Host ""
Write-Host "Boost libraries ready in $StageLib" -ForegroundColor Green
