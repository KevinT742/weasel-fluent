# scripts/apply-weasel-patches.ps1
# 把 3rdparty/weasel/patches/ 下的补丁幂等地应用到 vendor 目录。
#
# 为什么用补丁而不是直接改 vendor 源码：上游 rime/weasel 是持续开发的主分支，
# 保持 vendor 形态可复现、易 rebase。补丁本身是可审计的 diff。
#
# 补丁路径相对**仓库根**（git apply 在仓库内按 repo root 解析路径），
# 因此调用 git 时用 -C <repoRoot>。
#
# 用法：
#   powershell -ExecutionPolicy Bypass -File scripts/apply-weasel-patches.ps1
#
# 退出码：0 = 全部补丁处于已应用状态；1 = 有补丁应用失败

$ErrorActionPreference = "Stop"
$RepoRoot   = Split-Path -Parent $PSScriptRoot
$VendorRoot = Join-Path $RepoRoot "3rdparty\weasel"
$PatchDir   = Join-Path $VendorRoot "patches"

# git 会把 "Checking patch ..." 写到 stderr；在 $ErrorActionPreference='Stop' 下
# 2>&1 会把它变成终止性错误，因此这里临时放宽并把输出当纯文本收。
function Invoke-Git {
    param([string[]]$GitArgs)
    $prev = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        $out = & git @GitArgs 2>&1
        $code = $LASTEXITCODE
        return @{ Code = $code; Output = ($out | Out-String) }
    }
    finally {
        $ErrorActionPreference = $prev
    }
}

if (-not (Test-Path $PatchDir)) {
    Write-Host "no patch directory, nothing to do" -ForegroundColor DarkGray
    exit 0
}

$patches = Get-ChildItem -Path $PatchDir -Filter "*.patch" -File | Sort-Object Name
if ($patches.Count -eq 0) {
    Write-Host "no patches found" -ForegroundColor DarkGray
    exit 0
}

foreach ($p in $patches) {
    $patch = $p.FullName

    # 幂等判定：反向检查通过 == 补丁已应用
    $rev = Invoke-Git @("-C", $RepoRoot, "apply", "--reverse", "--check", "-v", $patch)
    if ($rev.Code -eq 0) {
        if ($rev.Output -match "Skipped patch") {
            Write-Host "FAIL: patch paths do not resolve (skipped): $($p.Name)" -ForegroundColor Red
            exit 1
        }
        Write-Host "      already applied: $($p.Name)" -ForegroundColor DarkGray
        continue
    }

    $fwd = Invoke-Git @("-C", $RepoRoot, "apply", "--check", "-v", $patch)
    if ($fwd.Code -ne 0 -or $fwd.Output -match "Skipped patch") {
        Write-Host "FAIL: patch does not apply cleanly: $($p.Name)" -ForegroundColor Red
        Write-Host "      $($fwd.Output.Trim())" -ForegroundColor DarkGray
        exit 1
    }

    $res = Invoke-Git @("-C", $RepoRoot, "apply", $patch)
    if ($res.Code -ne 0) {
        Write-Host "FAIL: failed to apply: $($p.Name)" -ForegroundColor Red
        exit 1
    }
    Write-Host "      applied: $($p.Name)" -ForegroundColor Green
}

exit 0
