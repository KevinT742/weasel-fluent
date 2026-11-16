# scripts/fetch_rime.ps1
# 获取 librime 运行时与 Rime 基础数据。
#
#   deps/rime/dist/lib/rime.dll   —— librime 核心（动态加载）
#   deps/rime/dist/include/       —— rime_api.h 等头文件
#   deps/rime-prelude/            —— default.yaml / key_bindings.yaml / punctuation.yaml / symbols.yaml
#   deps/rime-luna-pinyin/        —— luna_pinyin.schema.yaml / luna_pinyin.dict.yaml / pinyin.yaml
#
# 为什么要这些：
#   * rime.dll   —— 没有它前端只能跑仿真模式，永远不出真实候选词。
#   * prelude    —— default.yaml 的 schema_list 与 key_binder 都引用 key_bindings，
#                   缺 key_bindings.yaml 会导致配置编译失败（config_compiler 报错）。
#   * luna-pinyin—— 随发行包分发的默认方案（朙月拼音）。发行包不含第三方大词库。
#
# 用法：
#   powershell -ExecutionPolicy Bypass -File scripts/fetch_rime.ps1
#
# 说明：GitHub 的 git 协议在本机会因 TLS 代理中断，因此数据部分一律用
# codeload 的 tarball 下载 + tar 解包，不依赖 git clone。

$ErrorActionPreference = "Stop"

$RepoRoot = Split-Path -Parent $PSScriptRoot
$DepsDir  = Join-Path $RepoRoot "deps"
$RimeDir  = Join-Path $DepsDir "rime"

New-Item -ItemType Directory -Path $DepsDir -Force | Out-Null

function Get-RepoTarball {
    param(
        [Parameter(Mandatory = $true)][string]$Repo,   # e.g. rime/rime-prelude
        [Parameter(Mandatory = $true)][string]$Dest,   # target directory
        [Parameter(Mandatory = $true)][string[]]$Keep   # files to keep
    )
    $name = ($Repo -split '/')[1]
    $tar = Join-Path $DepsDir "$name.tar.gz"
    $url = "https://codeload.github.com/$Repo/tar.gz/refs/heads/master"

    if (-not (Test-Path $tar)) {
        Write-Host "      downloading $Repo ..." -ForegroundColor DarkGray
        & curl.exe -sSL --retry 4 --retry-delay 3 --max-time 600 -o $tar $url
        if ($LASTEXITCODE -ne 0 -or -not (Test-Path $tar)) {
            throw "failed to download $Repo"
        }
    }

    $extract = Join-Path $env:TEMP "rime_tar_$name"
    if (Test-Path $extract) { Remove-Item $extract -Recurse -Force }
    New-Item -ItemType Directory -Path $extract -Force | Out-Null
    & tar.exe -xzf $tar -C $extract
    if ($LASTEXITCODE -ne 0) { throw "failed to extract $Repo" }

    $root = Get-ChildItem -Path $extract -Directory | Select-Object -First 1
    New-Item -ItemType Directory -Path $Dest -Force | Out-Null
    foreach ($f in $Keep) {
        $src = Join-Path $root.FullName $f
        if (Test-Path $src) {
            Copy-Item $src -Destination $Dest -Force
        } else {
            Write-Host "      note: $f not present in $Repo" -ForegroundColor DarkGray
        }
    }
    Remove-Item $extract -Recurse -Force
}

# --- 1. librime 运行时 ---------------------------------------------------
Write-Host "[1/3] Fetching librime runtime..." -ForegroundColor Cyan
$archive = Join-Path $DepsDir "rime-msvc-x64.7z"
if (-not (Test-Path $archive)) {
    $release = Invoke-RestMethod -Uri 'https://api.github.com/repos/rime/librime/releases/latest' `
        -Headers @{ 'User-Agent' = 'weasel-fluent-build' } -TimeoutSec 60
    $asset = $release.assets | Where-Object { $_.name -match 'Windows-msvc-x64\.7z$' } | Select-Object -First 1
    if (-not $asset) { throw "No Windows msvc x64 asset found in librime release $($release.tag_name)" }
    Write-Host "      $($release.tag_name) -> $($asset.name)" -ForegroundColor DarkGray
    & curl.exe -sSL --retry 4 --retry-delay 3 --max-time 600 -o $archive $asset.browser_download_url
    if ($LASTEXITCODE -ne 0) { throw "failed to download librime" }
}
New-Item -ItemType Directory -Path $RimeDir -Force | Out-Null
$extractScript = @'
import sys, py7zr
archive, dest = sys.argv[1], sys.argv[2]
with py7zr.SevenZipFile(archive, 'r') as z:
    z.extractall(path=dest)
'@
$extractScript | Out-File -FilePath (Join-Path $env:TEMP "extract_rime.py") -Encoding utf8
python (Join-Path $env:TEMP "extract_rime.py") $archive $RimeDir
if ($LASTEXITCODE -ne 0) { throw "extraction failed" }

# --- 2. rime-prelude -----------------------------------------------------
Write-Host "[2/3] Fetching rime-prelude data..." -ForegroundColor Cyan
Get-RepoTarball -Repo 'rime/rime-prelude' -Dest (Join-Path $DepsDir 'rime-prelude') -Keep @(
    'default.yaml', 'key_bindings.yaml', 'punctuation.yaml', 'symbols.yaml'
)

# --- 3. rime-luna-pinyin --------------------------------------------------
Write-Host "[3/3] Fetching rime-luna-pinyin data..." -ForegroundColor Cyan
Get-RepoTarball -Repo 'rime/rime-luna-pinyin' -Dest (Join-Path $DepsDir 'rime-luna-pinyin') -Keep @(
    'luna_pinyin.schema.yaml', 'luna_pinyin.dict.yaml', 'pinyin.yaml',
    'luna_pinyin_simp.schema.yaml', 'luna_pinyin_tw.schema.yaml',
    'luna_pinyin_fluency.schema.yaml', 'luna_quanpin.schema.yaml'
)

# --- 验证 ---------------------------------------------------------------
$dll = Get-ChildItem -Path $RimeDir -Recurse -Filter 'rime.dll' -File | Select-Object -First 1
$hdr = Get-ChildItem -Path $RimeDir -Recurse -Filter 'rime_api.h' -File | Select-Object -First 1
if (-not $dll) { throw "rime.dll not found after extraction" }
if (-not $hdr) { throw "rime_api.h not found after extraction" }

Write-Host ""
Write-Host "librime ready." -ForegroundColor Green
Write-Host "  rime.dll   -> $($dll.FullName)"
Write-Host "  rime_api.h -> $($hdr.FullName)"
Write-Host "  data       -> $(Join-Path $DepsDir 'rime-prelude'), $(Join-Path $DepsDir 'rime-luna-pinyin')"
