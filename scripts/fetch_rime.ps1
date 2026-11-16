# scripts/fetch_rime.ps1
# 获取 librime 运行时（rime.dll + rime_api.h + 共享数据）。
#
# Weasel 的 RimeWithWeasel 依赖 librime 的 <rime_api.h>；本仓的 RimeEngine
# 需要 rime.dll 才能进入真实引擎模式（否则退化为仿真模式）。
#
# 上游 Weasel 用 get-rime.ps1 做同样的事，这里改为直接调用 GitHub release API，
# 并用 Python 的 py7zr 解包（.7z 格式，Windows 自带解压器不支持）。
#
# 用法：
#   powershell -ExecutionPolicy Bypass -File scripts/fetch_rime.ps1
#
# 产物：deps/rime/  （rime.dll、rime_api.h、data/ 等）

$ErrorActionPreference = "Stop"

$RepoRoot = Split-Path -Parent $PSScriptRoot
$DepsDir  = Join-Path $RepoRoot "deps"
$RimeDir  = Join-Path $DepsDir "rime"
$Archive  = Join-Path $DepsDir "rime-msvc-x64.7z"

New-Item -ItemType Directory -Path $DepsDir -Force | Out-Null

# --- 1. 解析 GitHub release ---------------------------------------------
Write-Host "[1/3] Resolving latest librime release..." -ForegroundColor Cyan
$release = Invoke-RestMethod -Uri 'https://api.github.com/repos/rime/librime/releases/latest' `
    -Headers @{ 'User-Agent' = 'weasel-fluent-build' } -TimeoutSec 60
$tag = $release.tag_name
$asset = $release.assets | Where-Object { $_.name -match 'Windows-msvc-x64\.7z$' } | Select-Object -First 1
if (-not $asset) { throw "No Windows msvc x64 asset found in librime release $tag" }
Write-Host "      $tag -> $($asset.name)" -ForegroundColor DarkGray

# --- 2. 下载 -------------------------------------------------------------
if (-not (Test-Path $Archive)) {
    Write-Host "[2/3] Downloading..." -ForegroundColor Cyan
    Invoke-WebRequest -Uri $asset.browser_download_url -OutFile $Archive -TimeoutSec 900
}
Write-Host ("      archive: {0:N1} MB" -f ((Get-Item $Archive).Length / 1MB)) -ForegroundColor DarkGray

# --- 3. 解包 -------------------------------------------------------------
Write-Host "[3/3] Extracting to deps/rime ..." -ForegroundColor Cyan
New-Item -ItemType Directory -Path $RimeDir -Force | Out-Null
$extractScript = @'
import sys, os, py7zr
archive, dest = sys.argv[1], sys.argv[2]
with py7zr.SevenZipFile(archive, 'r') as z:
    z.extractall(path=dest)
print("extracted to", dest)
'@
$extractScript | Out-File -FilePath (Join-Path $env:TEMP "extract_rime.py") -Encoding utf8
python (Join-Path $env:TEMP "extract_rime.py") $Archive $RimeDir
if ($LASTEXITCODE -ne 0) { throw "extraction failed with exit code $LASTEXITCODE" }

# --- 4. 定位 rime.dll / rime_api.h --------------------------------------
$dll = Get-ChildItem -Path $RimeDir -Recurse -Filter 'rime.dll' -File | Select-Object -First 1
$hdr = Get-ChildItem -Path $RimeDir -Recurse -Filter 'rime_api.h' -File | Select-Object -First 1
if (-not $dll) { throw "rime.dll not found after extraction" }
if (-not $hdr) { throw "rime_api.h not found after extraction" }

Write-Host ""
Write-Host "librime $tag ready." -ForegroundColor Green
Write-Host "  rime.dll   -> $($dll.FullName)"
Write-Host "  rime_api.h -> $($hdr.FullName)"
Write-Host "  RIME_ROOT  -> $($RimeDir)"
