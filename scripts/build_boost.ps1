# scripts/build_boost.ps1
# 构建官方 Weasel 内核所需的 Boost 库。
#
# 上游 Weasel 的 install_boost.bat 固定使用 Boost 1.84.0，但本机 MSVC 为 19.51
# （Visual Studio 18），故改用 1.88.0。Weasel 用到的 Boost API
# （interprocess / serialization / thread / filesystem / locale / regex）在 1.88 中
# 保持向后兼容。
#
# 两个环境相关的坑：
#  1) Boost 的 bootstrap.bat 靠解析 VS 产品版本推断工具集名，最新只认到 vc143
#     （VS 2022）。本机是 Visual Studio 18，会算出未知工具集 "vcunk" 而失败。
#     因此显式指定 vc143 —— Boost.Build 的 vc143 配置只调用 PATH 中的 cl.exe，
#     对 MSVC 19.51 同样有效（msvc.jam 对更高版本回退到 14.3）。
#  2) 本脚本含中文注释，必须存为 UTF-8 **带 BOM**：Windows PowerShell 5.1 读取
#     无 BOM 的 UTF-8 .ps1 时按系统 ANSI(GBK) 解码，乱码字节会破坏脚本解析
#     （表现为变量被悄悄置空）。
#
# 链接方式：link=static + runtime-link=shared（/MD），与本仓默认运行库及
# Qt 6.8.2 官方二进制保持一致。若编成 /MT 会与 Qt 的 /MD 导入库产生
# LNK2038 运行库不匹配。上游 Weasel 用 /MT 是因为它不链接 Qt。
#
# 用法：
#   powershell -ExecutionPolicy Bypass -File scripts/build_boost.ps1
#
# 产物：deps/boost_1_88_0/stage/lib/*.lib（BOOST_ROOT 指向 deps/boost_1_88_0）

$ErrorActionPreference = "Stop"

$RepoRoot = Split-Path -Parent $PSScriptRoot
$DepsDir  = Join-Path $RepoRoot "deps"
$BoostVer = "1_88_0"
$BoostUrl = "https://archives.boost.io/release/1.88.0/source/boost_$BoostVer.zip"
$BoostSrc = Join-Path $DepsDir "boost_$BoostVer"

New-Item -ItemType Directory -Path $DepsDir -Force | Out-Null

# --- 1. 下载源码包 -------------------------------------------------------
$zip = Join-Path $DepsDir "boost_$BoostVer.zip"
if (-not (Test-Path $zip)) {
    Write-Host "[1/4] Downloading Boost 1.88.0 source..." -ForegroundColor Cyan
    Invoke-WebRequest -Uri $BoostUrl -OutFile $zip -TimeoutSec 1800
}
Write-Host ("      archive: {0:N1} MB" -f ((Get-Item $zip).Length / 1MB)) -ForegroundColor DarkGray

# --- 2. 解压 -------------------------------------------------------------
if (-not (Test-Path (Join-Path $BoostSrc "boost"))) {
    Write-Host "[2/4] Extracting..." -ForegroundColor Cyan
    Expand-Archive -Path $zip -DestinationPath $DepsDir -Force
}

# --- 3. 定位 MSVC 开发者环境 --------------------------------------------
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vswhere)) { throw "vswhere not found; cannot locate MSVC." }
$vsPath = (& $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath).Trim()
if (-not $vsPath) { throw "No Visual Studio with C++ tools found." }
$vcvars = Join-Path $vsPath "VC\Auxiliary\Build\vcvars64.bat"
if (-not (Test-Path $vcvars)) { throw "vcvars64.bat not found at $vcvars" }
Write-Host "[3/4] MSVC: $vsPath" -ForegroundColor Cyan

# --- 4. bootstrap + b2 ---------------------------------------------------
# Boost.Build 的 setup 步骤会自行寻找 vcvarsall.bat：它从编译器目录反推
# VC\Auxiliary\Build。对 VS 2022 成立，但 Visual Studio 18 的目录布局不同，
# 反推结果落到 ...\bin\Hostx64\vcvarsall.bat（不存在），于是 setup 步骤失败、
# 后续所有编译被整体跳过（报 "skipped ... for lack of msvc-setup.nup"）。
#
# 修复：把 msvc.jam 里那一行默认值改成绝对路径。该行用的是 `?=`，只有当
# setup 为空时才生效，而前面的 locate-default-setup 正是返回空，所以改它有效。
$vcvarsAll = Join-Path $vsPath "VC\Auxiliary\Build\vcvarsall.bat"
if (-not (Test-Path $vcvarsAll)) { throw "vcvarsall.bat not found at $vcvarsAll" }
$jamFile = Join-Path $BoostSrc "tools\build\src\tools\msvc.jam"
$jamText = [System.IO.File]::ReadAllText($jamFile)
$jamPat = 'setup \?= \[ path\.join  \$\(parent\) "vcvarsall\.bat" \] ;'
if ($jamText -notmatch $jamPat) {
    Write-Host "      msvc.jam already patched or pattern changed - skipping patch" -ForegroundColor DarkGray
} else {
    $jamPath = $vcvarsAll -replace '\\', '/'
    $jamText = [regex]::Replace($jamText, $jamPat, ('setup ?= "' + $jamPath + '" ;'))
    [System.IO.File]::WriteAllText($jamFile, $jamText, (New-Object System.Text.UTF8Encoding($false)))
    Write-Host "      patched msvc.jam setup path -> $jamPath" -ForegroundColor DarkGray
}

# 通过临时 .cmd 文件调用：cmd.exe 处理多行 /c 参数不可靠。
$cmdFile = Join-Path $env:TEMP "weasel_boost_build.cmd"
@"
@echo off
call "$vcvars" >nul
cd /d "$BoostSrc"
if errorlevel 1 exit /b 1
if not exist b2.exe call bootstrap.bat vc143
if errorlevel 1 exit /b 1
b2.exe -j8 ^
  --with-filesystem ^
  --with-json ^
  --with-locale ^
  --with-regex ^
  --with-serialization ^
  --with-system ^
  --with-thread ^
  define=BOOST_USE_WINAPI_VERSION=0x0603 ^
  toolset=msvc-14.3 ^
  link=static ^
  runtime-link=shared ^
  --build-type=complete ^
  architecture=x86 ^
  address-model=64 ^
  stage
exit /b %errorlevel%
"@ | Set-Content -Path $cmdFile -Encoding ASCII

Write-Host "[4/4] Building Boost libraries (static /MD)..." -ForegroundColor Cyan
cmd /c $cmdFile
if ($LASTEXITCODE -ne 0) { throw "Boost build failed with exit code $LASTEXITCODE" }

# --- 5. 验证产物 ---------------------------------------------------------
$expected = @("boost_thread", "boost_serialization", "boost_system")
$libDir = Join-Path $BoostSrc "stage\lib"
foreach ($lib in $expected) {
    $hit = Get-ChildItem -Path $libDir -Filter "$lib*.lib" -ErrorAction SilentlyContinue | Select-Object -First 1
    if (-not $hit) { throw "Expected library $lib not found in $libDir" }
    Write-Host ("      OK {0} -> {1}" -f $lib, $hit.Name) -ForegroundColor Green
}

Write-Host ""
Write-Host "Boost ready." -ForegroundColor Green
Write-Host "  BOOST_ROOT = $BoostSrc"
Write-Host "  link dir   = $libDir"
