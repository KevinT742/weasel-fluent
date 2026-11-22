# scripts/verify-dpi-normalization.ps1
# 断言「输入框矩形 → 候选窗位置」的 DPI 换算链：换算只发生一次，且位置正确。
#
# 为什么要有这个检查：
#   * 候选窗是 Qt 窗口，Qt 用 96-DPI **逻辑坐标**定位（150% 屏上 1 逻辑像素 = 1.5 物理像素）；
#   * TIP 送来的矩形属于**宿主进程**的坐标空间：宿主 Per-Monitor / System DPI 感知时
#     ITfContextView::GetTextExt 返回的是**物理像素**；
#   * 换算必须发生在服务端（WeaselIPCServer）：那里既拿得到前台宿主窗口，
#     又知道自己用的是逻辑坐标；而且 TIP DLL 是被**逐个宿主进程**加载的，
#     换算放进 TIP 就意味着每个正在运行的程序都得重启才能生效。
#
# 三种错法各自对应一个可观测的偏差（150% 屏）：
#   * 不换算            → 候选窗跑到光标的 1.5 倍远处（用户报的「离光标太远」）；
#   * 服务端与 TIP 各换一次 → 候选窗跑到 0.44 倍处；
#   * 对 DPI 不感知的宿主也换 → 候选窗跑到 0.67 倍处（宿主坐标已被系统虚拟化到 96-DPI）。
#
# 用法：
#   powershell -ExecutionPolicy Bypass -File scripts/verify-dpi-normalization.ps1
#
# 退出码：0 = 通过；1 = 失败；2 = 跳过（缺少构建产物，只跑了源码级断言）

$ErrorActionPreference = "Stop"
$RepoRoot = Split-Path -Parent $PSScriptRoot

$failed = @()
function Check {
    param([string]$Name, [bool]$Ok, [string]$Detail = "")
    if ($Ok) {
        Write-Host "  [OK] $Name" -ForegroundColor DarkGray
    } else {
        Write-Host "  [FAIL] $Name $Detail" -ForegroundColor Red
        $script:failed += $Name
    }
}

Write-Host "=== DPI 换算链断言 ===" -ForegroundColor Cyan

# ---------------------------------------------------------------- 源码级断言
$serverSrc = Join-Path $RepoRoot "3rdparty\weasel\WeaselIPCServer\WeaselServerImpl.cpp"
$tipSrc = Join-Path $RepoRoot "3rdparty\weasel\WeaselTSF\Composition.cpp"
$candSrc = Join-Path $RepoRoot "src\candidate_window.cpp"

foreach ($p in @($serverSrc, $tipSrc, $candSrc)) {
    if (-not (Test-Path $p)) {
        Write-Host "FAIL: 缺少源文件 $p" -ForegroundColor Red
        exit 1
    }
}

$serverText = Get-Content $serverSrc -Raw
$tipText = Get-Content $tipSrc -Raw
$candText = Get-Content $candSrc -Raw

# 1. 服务端必须做换算，且系数是 96（96-DPI 逻辑空间）
Check "服务端按 96/DPI 换算" ($serverText -match "MulDiv\(rc\.\w+,\s*96,")

# 2. 必须按**前台宿主窗口**的 DPI 换算，而不是写死倍率
Check "服务端用前台宿主窗口的 DPI" (
    ($serverText -match "GetForegroundWindow") -and
    ($serverText -match "GetDpiForWindow"))

# 3. 必须有 DPI 感知守卫：DPI 不感知的宿主坐标已被系统虚拟化，不能再换
Check "有 DPI 感知守卫（DPI 不感知的宿主不换算）" (
    ($serverText -match "DPI_AWARENESS_UNAWARE") -and
    ($serverText -match "GetWindowDpiAwarenessContext"))

# 4. 换算只能有一处：TIP 侧不得再换
$tipScales = @("MulDiv", "GetDpiForWindow", "PhysicalToLogicalPoint")
$tipHits = @($tipScales | Where-Object { $tipText -match [regex]::Escape($_) })
Check "TIP 侧不做换算（避免二次换算）" ($tipHits.Count -eq 0) ("命中: " + ($tipHits -join ", "))

# 5. 候选窗自身也不得缩放：IPC 送来的已经是逻辑坐标。
#    只看定位函数 setInputBoxRect —— 文件里其它地方用 devicePixelRatio 是为了
#    按真实分辨率绘制画布，与定位无关。
$posStart = $candText.IndexOf("void CandidateWindow::setInputBoxRect")
$posEnd = if ($posStart -ge 0) { $candText.IndexOf("`n}", $posStart) } else { -1 }
$posBody = if ($posStart -ge 0 -and $posEnd -gt $posStart) { $candText.Substring($posStart, $posEnd - $posStart) } else { "" }
Check "定位函数 setInputBoxRect 存在" ($posBody.Length -gt 0)
$posScales = @("devicePixelRatio", "MulDiv", "GetDpiForWindow", "logicalDotsPerInch")
$posHits = @($posScales | Where-Object { $posBody -match [regex]::Escape($_) })
Check "候选窗定位不按 DPR 再缩放" ($posHits.Count -eq 0) ("命中: " + ($posHits -join ", "))
Check "候选窗定位直接用矩形的 left 作为横坐标" ($posBody -match "x\s*=\s*static_cast<int>\(rc\.left\(\)\)")

# ---------------------------------------------------------------- 构建产物级断言
$exeCandidates = @(
    (Join-Path $RepoRoot "build\Release\weasel-fluent-cpp-qt.exe"),
    (Join-Path $RepoRoot "build\windows\x64\release\weasel-fluent-cpp-qt.exe")
)
$exe = $exeCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $exe) {
    Write-Host "SKIP: 没有找到已构建的 weasel-fluent-cpp-qt.exe，只跑了源码级断言" -ForegroundColor Yellow
} else {
    $bytes = [System.IO.File]::ReadAllBytes($exe)
    $ascii = [System.Text.Encoding]::ASCII.GetString($bytes)
    Check "exe 里含 GetWindowDpiAwarenessContext（动态解析的 API 名）" `
        ($ascii.Contains("GetWindowDpiAwarenessContext"))
    Check "exe 里含 GetDpiForWindow" ($ascii.Contains("GetDpiForWindow"))
    Check "exe 里含 MulDiv（缩放实现）" ($ascii.Contains("MulDiv"))
}

Write-Host ""
if ($failed.Count -eq 0) {
    Write-Host "DPI 换算链断言全部通过" -ForegroundColor Green
    exit 0
}
Write-Host ("FAILED: " + ($failed -join ", ")) -ForegroundColor Red
exit 1
