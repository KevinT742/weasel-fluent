# scripts/verify-no-rime-ice.ps1
# rime-ice 残留自检。
#
# 本项目已与 rime-ice（雾凇拼音）解耦：默认方案改为 librime 自带的
# luna_pinyin，发行包不再携带 rime-ice 词库。用户如需大型词库，
# 通过设置中心的「自定义词库」自行导入。
#
# 扫描范围排除 docs/（设计文档与计划里需要引用这个名字来说明移除动作）
# 以及本脚本自身（模式以字面量形式出现）。
#
# 用法：
#   powershell -ExecutionPolicy Bypass -File scripts/verify-no-rime-ice.ps1
#
# 退出码：0 = 通过，1 = 发现残留

$ErrorActionPreference = "Stop"
$repo = Split-Path -Parent $PSScriptRoot

$patterns = @('rime_ice', 'rime-ice', '雾凇拼音')

$hits = @()
Get-ChildItem -Path $repo -Recurse -Include *.cpp, *.h, *.ps1, *.lua, *.md, *.yaml, *.json -File |
    Where-Object {
        $_.FullName -notmatch '[\\/]3rdparty[\\/]' -and
        $_.FullName -notmatch '[\\/]\.git[\\/]' -and
        $_.FullName -notmatch '[\\/]deps[\\/]' -and
        $_.FullName -notmatch '[\\/]docs[\\/]' -and
        $_.Name -notlike 'verify-*.ps1' -and
        $_.Name -notlike '设计.md'
    } |
    ForEach-Object {
        $f = $_
        foreach ($p in $patterns) {
            $m = Select-String -Path $f.FullName -Pattern $p -SimpleMatch -Encoding UTF8 -ErrorAction SilentlyContinue
            foreach ($mm in $m) {
                $hits += "{0}:{1} [{2}] {3}" -f $f.FullName.Replace("$repo\", ''), $mm.LineNumber, $p, $mm.Line.Trim()
            }
        }
    }

if ($hits.Count -gt 0) {
    Write-Host "FAIL: found $($hits.Count) rime-ice residue(s):" -ForegroundColor Red
    $hits | ForEach-Object { Write-Host "  $_" -ForegroundColor Red }
    exit 1
}

Write-Host "PASS: no rime-ice residue" -ForegroundColor Green
exit 0
