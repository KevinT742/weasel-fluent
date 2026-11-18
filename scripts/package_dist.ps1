$ErrorActionPreference = "Stop"

# 仓库根目录 = 本脚本所在目录的父目录（脚本位于 scripts/）
$ProjectDir = Split-Path -Parent $PSScriptRoot
$DistBase = Join-Path $ProjectDir "dist"
$DistDir = Join-Path $DistBase "weasel-fluent"

Write-Host "============================================================" -ForegroundColor Cyan
Write-Host "  Packaging Weasel-Fluent C++ Qt + Rime Standalone Bundle   " -ForegroundColor Cyan
Write-Host "============================================================" -ForegroundColor Cyan

# 1. Prepare dist directory
if (Test-Path $DistDir) {
    Remove-Item -Recurse -Force $DistDir
}
New-Item -ItemType Directory -Path $DistDir -Force | Out-Null

# 2. Copy Frontend Executable
$ExeSrc = "$ProjectDir\build\Release\weasel-fluent-cpp-qt.exe"
if (-not (Test-Path $ExeSrc)) {
    Write-Host "Executable not found at $ExeSrc! Running xmake build..." -ForegroundColor Yellow
    Push-Location $ProjectDir
    xmake build
    Pop-Location
}
Copy-Item $ExeSrc -Destination "$DistDir\weasel-fluent-cpp-qt.exe" -Force
Write-Host "[1/7] Copied weasel-fluent-cpp-qt.exe" -ForegroundColor Green

# 1b. Copy the TSF TIP DLL (the TSF front end that talks to the service)
$TsfDllSrc = "$ProjectDir\build\Release\weasel-fluent-tsf.dll"
if (-not (Test-Path $TsfDllSrc)) {
    $TsfDllSrc = "$ProjectDir\build\windows\x64\release\weasel-fluent-tsf.dll"
}
if (Test-Path $TsfDllSrc) {
    Copy-Item $TsfDllSrc -Destination "$DistDir\weasel-fluent-tsf.dll" -Force
    Write-Host "[1/7] Copied weasel-fluent-tsf.dll (TSF TIP)" -ForegroundColor Green
} else {
    Write-Host "[1/7] WARNING: weasel-fluent-tsf.dll not found! The IME will not load." -ForegroundColor Red
}

# 3. Deploy Qt6 Runtime
$Windeployqt = "C:\Qt\6.8.2\msvc2022_64\bin\windeployqt.exe"
if (Test-Path $Windeployqt) {
    Write-Host "[2/7] Running windeployqt to bundle Qt6 dependencies..." -ForegroundColor Green
    & $Windeployqt "$DistDir\weasel-fluent-cpp-qt.exe" --no-translations --no-compiler-runtime | Out-Null
} else {
    Write-Host "[WARNING] windeployqt not found at $Windeployqt" -ForegroundColor Yellow
}

# 4. Copy Librime Backend (rime.dll)
$RimeDll = Join-Path $ProjectDir "deps\rime\dist\lib\rime.dll"
if (Test-Path $RimeDll) {
    Copy-Item $RimeDll -Destination (Join-Path $DistDir "rime.dll") -Force
    Write-Host "[3/7] Copied Librime 64-bit backend (rime.dll)" -ForegroundColor Green
} else {
    Write-Host "[3/7] WARNING: rime.dll not found at $RimeDll (run scripts/fetch_rime.ps1)" -ForegroundColor Yellow
}

# 5. Copy Rime Shared Data (rime-prelude + rime-luna-pinyin)
$RimeDataSrc = Join-Path $ProjectDir "deps\rime-prelude"
$LunaSrc = Join-Path $ProjectDir "deps\rime-luna-pinyin"
if ((Test-Path $RimeDataSrc) -and (Test-Path $LunaSrc)) {
    $RimeDataDst = Join-Path $DistDir "data"
    New-Item -ItemType Directory -Path $RimeDataDst -Force | Out-Null
    $preludeKeep = @("default.yaml", "key_bindings.yaml", "grammar.yaml", "punctuation.yaml",
                      "symbols.yaml", "sgns.yaml", "emoji.yaml")
    foreach ($name in $preludeKeep) {
        $src = Join-Path $RimeDataSrc $name
        if (Test-Path $src) { Copy-Item $src -Destination $RimeDataDst -Force }
    }
    $lunaKeep = @("luna_pinyin.schema.yaml", "luna_pinyin.dict.yaml", "pinyin.yaml",
                  "luna_pinyin_simp.schema.yaml", "luna_pinyin_tw.schema.yaml",
                  "luna_pinyin_fluency.schema.yaml", "luna_quanpin.schema.yaml")
    foreach ($name in $lunaKeep) {
        $src = Join-Path $LunaSrc $name
        if (Test-Path $src) { Copy-Item $src -Destination $RimeDataDst -Force }
    }
    Write-Host "[4/7] Copied Rime shared data (rime-prelude + rime-luna-pinyin)" -ForegroundColor Green
} else {
    Write-Host "[4/7] WARNING: Rime data not found (run scripts/fetch_rime.ps1)" -ForegroundColor Yellow
}

# 6. Copy user dictionary templates (bundled dictionary excluded, no user state)
$UserRimeSrc = Join-Path $env:APPDATA "Rime"
$UserDictsDst = Join-Path $DistDir "user_dicts"
New-Item -ItemType Directory -Path $UserDictsDst -Force | Out-Null
if (Test-Path $UserRimeSrc) {
    # Only user-authored custom dictionaries; never the bundled dictionary payload.
    Get-ChildItem -Path $UserRimeSrc -Filter "*.dict.yaml" -File -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -notmatch '^(luna_pinyin|essay|ext)' } |
        ForEach-Object { Copy-Item $_.FullName -Destination $UserDictsDst -Force }
    Write-Host "[5/7] Copied user custom dictionaries" -ForegroundColor Green
}

# 7. Copy Assets and Config
$AssetsSrc = "$ProjectDir\assets"
if (Test-Path $AssetsSrc) {
    Copy-Item -Recurse $AssetsSrc -Destination "$DistDir\assets" -Force
}
$DefaultConfig = @{
    orientation = "horizontal"
    page_size = 5
    font_size = 12
    font_family = "Microsoft YaHei UI"
    theme_mode = "system"
    accent_color = "auto"
    sound_enabled = $false
    show_preedit = $false
    show_label = $true
    show_comment = $true
} | ConvertTo-Json -Depth 4
$DefaultConfig | Out-File -FilePath "$DistDir\config.json" -Encoding utf8
Write-Host "[6/7] Copied assets and generated default config.json" -ForegroundColor Green

# 8. Create Scripts
$InstallScript = @"
@echo off
chcp 65001 >nul
title Weasel-Fluent - 安装并接管系统输入
echo ===================================================
echo   正在安装 Weasel-Fluent（每用户，免管理员）...
echo ===================================================

powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0install.ps1" -PayloadDir "%~dp0"

echo.
echo 安装完成！可在任意应用中输入拼音进行打字。
echo 按 Shift 切换中英文，Tab 展开候选词大矩阵，托盘图标右键可进入设置中心。
pause
"@
$InstallScript | Out-File -FilePath "$DistDir\install_and_run.bat" -Encoding utf8

$StopScript = @"
@echo off
chcp 65001 >nul
title 停止 Rime Fluent IME 服务
taskkill /F /IM weasel-fluent-cpp-qt.exe 2>nul
echo Rime Fluent 服务已停止。
"@
$StopScript | Out-File -FilePath "$DistDir\stop.bat" -Encoding utf8

$SettingsScript = @"
@echo off
start "" "%~dp0weasel-fluent-cpp-qt.exe" --settings
"@
$SettingsScript | Out-File -FilePath "$DistDir\settings.bat" -Encoding utf8

# start_service.bat：**上游 TIP 硬依赖这个文件名**。
#
# 上游 Weasel 的 WeaselTSF 在两处按 "<WeaselRoot>\start_service.bat" 拉服务：
#   * WeaselTSF.cpp：连不上服务端时自动拉起（这是「开机后输入法能用」的关键路径）；
#   * LanguageBar.cpp：语言栏菜单里的「重启(Restart)」。
# 而 WeaselRoot 由本仓的安装器写在 HKCU\Software\Rime\Weasel\WeaselRoot
# （vendor 补丁 0002 已把查找顺序改成 HKCU 优先）。
#
# 所以本仓**必须**提供同名同义的脚本，否则 TIP 的自动拉起会静默失败
# （ShellExecuteW 找不到文件不报错，只是服务始终起不来）。
# 上游这个脚本启动的是 WeaselServer.exe；我们的宿主进程本身就同时是
# GUI 与服务端，所以直接启动 weasel-fluent-cpp-qt.exe 即可。
# 单实例由 WeaselFluentSingleInstanceMutex 兜底，重复拉起不会有副作用。
$StartServiceScript = @"
@echo off
chcp 65001 >nul
title Weasel-Fluent - 启动输入法服务
start "" "%~dp0weasel-fluent-cpp-qt.exe"
"@
$StartServiceScript | Out-File -FilePath "$DistDir\start_service.bat" -Encoding utf8

# 安装器 / 卸载器：随包分发，每用户免提权安装。
foreach ($tool in @("install.ps1", "uninstall.ps1")) {
    $toolSrc = Join-Path $ProjectDir "scripts\$tool"
    if (Test-Path $toolSrc) {
        Copy-Item $toolSrc -Destination (Join-Path $DistDir $tool) -Force
    } else {
        Write-Host "[7/7] WARNING: $tool not found at $toolSrc" -ForegroundColor Yellow
    }
}

Write-Host "[7/7] Generated control scripts (install_and_run.bat, stop.bat, settings.bat, start_service.bat, install.ps1, uninstall.ps1)" -ForegroundColor Green

# 9. Compress to zip
$ZipDst = "$DistBase\weasel-fluent-v1.0.0-windows-x64.zip"
if (Test-Path $ZipDst) {
    Remove-Item -Force $ZipDst
}
Write-Host "Compressing bundle into $ZipDst..." -ForegroundColor Cyan
Compress-Archive -Path "$DistDir\*" -DestinationPath $ZipDst -CompressionLevel Optimal

$ZipSizeMB = [math]::Round(((Get-Item $ZipDst).Length / 1MB), 2)
Write-Host "============================================================" -ForegroundColor Cyan
Write-Host "  Packaging complete! Artifact: $ZipDst ($ZipSizeMB MB)     " -ForegroundColor Green
Write-Host "============================================================" -ForegroundColor Cyan
