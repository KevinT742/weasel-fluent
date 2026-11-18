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
    $code = $LASTEXITCODE
    # 退出码 2 = 跳过（例如语言栏 IPC 测试需要服务在跑）。
    # 跳过不算失败，但也绝不能当成通过。
    if ($code -eq 2) {
        Write-Host "--- $Name SKIPPED ---" -ForegroundColor Yellow
        return
    }
    if ($code -ne 0) {
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

# 语言栏 ↔ 服务端的端到端连通性（需要服务在跑，没跑就跳过）。
Invoke-Check 'langbar-ipc' {
    powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $RepoRoot 'scripts\test_langbar_ipc.ps1')
}

# The test suite needs the built executable and the Qt runtime on PATH.
#
# ⚠️ **故意不**把 Qt 的 bin 目录加进 PATH：
# exe 是用 Qt 6.8.2 编译的，而 DLL 搜索默认先看 PATH —— 本机 PATH 里排着
# STM32CubeProgrammer 的 bin（里面有一套 Qt 6.6.1），一旦从那里捡到同名 DLL，
# 进程会直接报「无法定位程序输入点 ?setGeometry@QStackedLayout...」。
# 构建目录自带 Qt 6.8.2 的 DLL（xmake 的 after_build 拷），所以自检就按
# **用户双击 exe 的真实方式**来跑 —— 这样才能守住这条回归。
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
