# scripts/verify-no-hardcoded-paths.ps1
# 路径红线自检：源码中不得出现指向特定用户或特定机器的绝对路径。
#
# 所有运行期路径必须经 src/paths.cpp 的 WeaselPaths::* 解析。
# 该脚本本身含有这些模式的字面量，因此扫描时排除 scripts/verify-*.ps1。
#
# 用法：
#   powershell -ExecutionPolicy Bypass -File scripts/verify-no-hardcoded-paths.ps1
#
# 退出码：0 = 通过，1 = 发现违规

$ErrorActionPreference = "Stop"
$repo = Split-Path -Parent $PSScriptRoot

$patterns = @(
    @{ re = 'C:[/\\]Users[/\\][A-Za-z0-9_.-]+'; msg = 'hardcoded user directory' },
    @{ re = 'C:[/\\]Program Files[/\\]Rime';  msg = 'hardcoded legacy Rime install path' },
    @{ re = '\.gemini[/\\]antigravity';          msg = 'leftover AI scratch path' }
)

$hits = @()
Get-ChildItem -Path $repo -Recurse -Include *.cpp, *.h, *.ps1, *.lua, *.md -File |
    Where-Object {
        $_.FullName -notmatch '[\\/]3rdparty[\\/]' -and
        $_.FullName -notmatch '[\\/]\.git[\\/]' -and
        $_.FullName -notmatch '[\\/]deps[\\/]' -and
        $_.FullName -notmatch '[\\/]docs[\\/]' -and
        $_.Name -notlike 'verify-*.ps1'
    } |
    ForEach-Object {
        $f = $_
        foreach ($p in $patterns) {
            $m = Select-String -Path $f.FullName -Pattern $p.re -AllMatches -ErrorAction SilentlyContinue
            foreach ($mm in $m) {
                $hits += "{0}:{1} [{2}] {3}" -f $f.FullName.Replace("$repo\", ''), $mm.LineNumber, $p.msg, $mm.Line.Trim()
            }
        }
    }

if ($hits.Count -gt 0) {
    Write-Host "FAIL: found $($hits.Count) hardcoded path(s):" -ForegroundColor Red
    $hits | ForEach-Object { Write-Host "  $_" -ForegroundColor Red }
    exit 1
}

Write-Host "PASS: no hardcoded paths" -ForegroundColor Green
exit 0
