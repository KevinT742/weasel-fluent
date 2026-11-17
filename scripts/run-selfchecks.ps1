# scripts/run-selfchecks.ps1
# 一次性跑完全部自检：路径红线、rime-ice 残留、许可证，以及可执行文件的
# 单元/集成测试套件。
#
# 用法：
#   powershell -ExecutionPolicy Bypass -File scripts/run-selfchecks.ps1
#
# 退出码：0 = 全部通过，1 = 有检查失败

$ErrorActionPreference = "Continue"
$RepoRoot = Split-Path -Parent $PSScriptRoot

$failed = @()

function Invoke-Check {
    param([string]$Name, [scriptblock]$Action)
    Write-Host ""
    Write-Host "=== $Name ===" -ForegroundColor Cyan
    & $Action
    if ($LASTEXITCODE -ne 0) {
        $script:failed += $Name
        Write-Host "--- $Name FAILED ---" -ForegroundColor Red
    }
}

Invoke-Check 'no-hardcoded-paths' {
    powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $RepoRoot 'scripts\verify-no-hardcoded-paths.ps1')
}

Invoke-Check 'no-rime-ice' {
    powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $RepoRoot 'scripts\verify-no-rime-ice.ps1')
}

Invoke-Check 'license' {
    powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $RepoRoot 'scripts\verify-license.ps1')
}

Invoke-Check 'tip-dll' {
    powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $RepoRoot 'scripts\verify-tip-dll.ps1')
}

# The test suite needs the built executable and the Qt runtime on PATH.
$exeCandidates = @(
    (Join-Path $RepoRoot 'build\Release\weasel-fluent-cpp-qt.exe'),
    (Join-Path $RepoRoot 'build\windows\x64\release\weasel-fluent-cpp-qt.exe')
)
$exe = $exeCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $exe) {
    Write-Host ""
    Write-Host "=== unit-tests ===" -ForegroundColor Cyan
    Write-Host "SKIP: no built executable found (run 'xmake build' first)" -ForegroundColor Yellow
} else {
    $qtBin = 'C:\Qt\6.8.2\msvc2022_64\bin'
    if (Test-Path $qtBin) { $env:PATH = "$qtBin;$env:PATH" }
    Invoke-Check 'unit-tests' {
        & $exe --verify
    }
}

Write-Host ""
Write-Host "============================================" -ForegroundColor Cyan
if ($failed.Count -eq 0) {
    Write-Host "ALL SELF-CHECKS PASSED" -ForegroundColor Green
    exit 0
}
Write-Host ("FAILED CHECKS: " + ($failed -join ', ')) -ForegroundColor Red
exit 1
