# scripts/verify-license.ps1
# 许可证与第三方致谢自检。
#
# 本项目内置了官方 Weasel（GPLv3）源码，因此整体必须按 GPLv3 发布。
#
# 用法：
#   powershell -ExecutionPolicy Bypass -File scripts/verify-license.ps1
#
# 退出码：0 = 通过，1 = 不合规

$ErrorActionPreference = "Stop"
$repo = Split-Path -Parent $PSScriptRoot

$licensePath = Join-Path $repo "LICENSE"
if (-not (Test-Path $licensePath)) {
    Write-Host "FAIL: LICENSE is missing" -ForegroundColor Red
    exit 1
}
$licenseText = Get-Content $licensePath -Raw
if ($licenseText -notmatch "GNU GENERAL PUBLIC LICENSE") {
    Write-Host "FAIL: LICENSE is not the GPLv3 text" -ForegroundColor Red
    exit 1
}
if ($licenseText -notmatch "Version 3") {
    Write-Host "FAIL: LICENSE is not GPL version 3" -ForegroundColor Red
    exit 1
}

$noticesPath = Join-Path $repo "THIRD_PARTY_NOTICES.md"
if (-not (Test-Path $noticesPath)) {
    Write-Host "FAIL: THIRD_PARTY_NOTICES.md is missing" -ForegroundColor Red
    exit 1
}
$notices = Get-Content $noticesPath -Raw
foreach ($needle in @("rime/weasel", "librime", "Fluent-Qt", "Boost", "Qt")) {
    if ($notices -notmatch [regex]::Escape($needle)) {
        Write-Host "FAIL: THIRD_PARTY_NOTICES.md does not mention $needle" -ForegroundColor Red
        exit 1
    }
}

$readmePath = Join-Path $repo "README.md"
if (-not (Test-Path $readmePath)) {
    Write-Host "FAIL: README.md is missing" -ForegroundColor Red
    exit 1
}
$readme = Get-Content $readmePath -Raw
if ($readme -match "MIT License" -or $readme -match "MIT License（MIT）") {
    Write-Host "FAIL: README.md still advertises the MIT license" -ForegroundColor Red
    exit 1
}

Write-Host "PASS: license is GPLv3 and third-party notices are complete" -ForegroundColor Green
exit 0
