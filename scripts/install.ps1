# scripts/install.ps1
# 每用户安装 Weasel-Fluent（免管理员）。
#
# 安装形态（设计文档 §6.6）：
#   目标目录：%LOCALAPPDATA%\Programs\WeaselFluent
#   注册方式：HKCU CLSID **影子注册** —— 在 HKCU 下把**官方 Weasel 的 CLSID**
#             {A3F4CDED-…} 指到本仓的 TIP DLL。COM 解析
#             HKCU\Software\Classes\CLSID 优先于 HKLM，所以本仓的 DLL 会顶替
#             机器上已装的官方 Weasel，而**不需要管理员权限**。
#             卸载时删掉 HKCU 这一份即可让官方 Weasel 立刻恢复。
#   语言列表：官方安装留下的 0804:{A3F4CDED-…}{3D02CAB6-…} 条目直接生效
#             （本仓的 TIP 就注册在这个 CLSID + profile 下）。
#
# 用法：
#   powershell -NoProfile -ExecutionPolicy Bypass -File scripts/install.ps1
#   powershell -NoProfile -ExecutionPolicy Bypass -File scripts/install.ps1 -PayloadDir <发行包目录>
#   powershell -NoProfile -ExecutionPolicy Bypass -File scripts/install.ps1 -Uninstall
#
# 幂等：每一步都是「先检测后动作」，重复执行不报错。

param(
    [string]$PayloadDir = "",
    [switch]$Uninstall,
    [switch]$Purge
)

$ErrorActionPreference = "Stop"

$RepoRoot = Split-Path -Parent $PSScriptRoot
if ([string]::IsNullOrWhiteSpace($PayloadDir)) {
    # 默认从「发行包目录」取 payload；脚本在仓库里时就用仓库的构建产物。
    $PayloadDir = Join-Path $RepoRoot "dist\weasel-fluent"
}

$InstallDir = Join-Path $env:LOCALAPPDATA "Programs\WeaselFluent"
$ExeName = "weasel-fluent-cpp-qt.exe"
$TsfDllName = "weasel-fluent-tsf.dll"

# 上游官方 Weasel 的 CLSID / profile —— 影子注册的目标。
$OfficialClsid = "{A3F4CDED-B1E9-41EE-9CA6-7B4D0DE6CB0A}"
$OfficialProfile = "{3D02CAB6-2B8E-4781-BA20-1C9267529467}"

function Write-Step($text) { Write-Host "==> $text" -ForegroundColor Cyan }
function Write-Ok($text) { Write-Host "    [OK] $text" -ForegroundColor Green }
function Write-Info($text) { Write-Host "    $text" -ForegroundColor DarkGray }

function Stop-ProcessByName([string]$name) {
    $procs = Get-Process -Name $name -ErrorAction SilentlyContinue
    if (-not $procs) {
        Write-Info "没有正在运行的 $name"
        return
    }
    foreach ($p in $procs) {
        Write-Info "停止 $($p.ProcessName) (PID $($p.Id))"
        try {
            Stop-Process -Id $p.Id -Force -ErrorAction Stop
        } catch {
            Write-Host "    [WARN] 无法停止 $($p.ProcessName)：$($_.Exception.Message)" -ForegroundColor Yellow
        }
    }
}

function Invoke-RegSvr32 {
    param([string]$dllPath, [switch]$Unregister)
    if (-not (Test-Path $dllPath)) {
        Write-Host "    [WARN] DLL 不存在，跳过注册：$dllPath" -ForegroundColor Yellow
        return $false
    }
    $regsvr32 = Join-Path $env:SystemRoot "System32\regsvr32.exe"
    if (-not (Test-Path $regsvr32)) {
        Write-Host "    [WARN] 找不到 regsvr32.exe" -ForegroundColor Yellow
        return $false
    }
    $args = @("/s")
    if ($Unregister) { $args += "/u" }
    $args += $dllPath
    $proc = Start-Process -FilePath $regsvr32 -ArgumentList $args -Wait -PassThru -WindowStyle Hidden
    return ($proc.ExitCode -eq 0)
}

if ($Uninstall) {
    Write-Step "卸载 Weasel-Fluent"

    Write-Step "停止进程"
    Stop-ProcessByName "weasel-fluent-cpp-qt"
    Stop-ProcessByName "WeaselServer"

    Write-Step "反注册 TIP DLL"
    $tsfDll = Join-Path $InstallDir $TsfDllName
    if (Test-Path $tsfDll) {
        if (Invoke-RegSvr32 -dllPath $tsfDll -Unregister) {
            Write-Ok "已反注册 $TsfDllName"
        } else {
            Write-Host "    [WARN] 反注册返回非零，请检查是否有权限问题" -ForegroundColor Yellow
        }
    } else {
        Write-Info "安装目录里没有 $TsfDllName，跳过"
    }

    Write-Step "清理 HKCU 影子注册"
    $clsidKey = "HKCU:\Software\Classes\CLSID\$OfficialClsid"
    if (Test-Path $clsidKey) {
        Remove-Item -Path $clsidKey -Recurse -Force -ErrorAction SilentlyContinue
        Write-Ok "已删除 HKCU 影子注册（官方 Weasel 恢复）"
    } else {
        Write-Info "没有 HKCU 影子注册"
    }

    Write-Step "移除自启项"
    $runKey = "HKCU:\Software\Microsoft\Windows\CurrentVersion\Run"
    if (Test-Path "$runKey\WeaselFluent") {
        Remove-Item "$runKey\WeaselFluent" -Force
        Write-Ok "已移除 Run\WeaselFluent"
    }

    Write-Step "移除安装目录"
    if (Test-Path $InstallDir) {
        Remove-Item -Path $InstallDir -Recurse -Force -ErrorAction SilentlyContinue
        Write-Ok "已删除 $InstallDir"
    }

    if ($Purge) {
        Write-Step "清除用户数据（--purge）"
        $userRime = Join-Path $env:APPDATA "Rime"
        if (Test-Path $userRime) {
            Remove-Item -Path $userRime -Recurse -Force -ErrorAction SilentlyContinue
            Write-Ok "已删除 $userRime"
        }
    } else {
        Write-Info "用户数据保留在 %APPDATA%\Rime（加 -Purge 可一并清除）"
    }

    Write-Host "卸载完成。" -ForegroundColor Green
    exit 0
}

# ── 安装 ────────────────────────────────────────────────────────────────
Write-Host "============================================================" -ForegroundColor Cyan
Write-Host "  安装 Weasel-Fluent（每用户，免管理员）                " -ForegroundColor Cyan
Write-Host "============================================================" -ForegroundColor Cyan

Write-Step "停旧进程"
Stop-ProcessByName "weasel-fluent-cpp-qt"
Stop-ProcessByName "WeaselServer"

Write-Step "准备安装目录：$InstallDir"
New-Item -ItemType Directory -Path $InstallDir -Force | Out-Null

Write-Step "复制 payload（来源：$PayloadDir）"
if (-not (Test-Path $PayloadDir)) {
    Write-Host "找不到 payload 目录：$PayloadDir" -ForegroundColor Red
    Write-Host "请先运行 scripts\package_dist.ps1 生成发行包，或用 -PayloadDir 指定。" -ForegroundColor Yellow
    exit 1
}
$copied = 0
$TsfDllInstalled = Join-Path $InstallDir $TsfDllName

# 复制整个 payload 目录树（含 Qt 运行时与 plugins/）。
#
# ⚠️ 必须连 Qt 一起复制：exe 启动时用 SetDefaultDllDirectories 把 DLL 搜索
# 固定成「exe 目录 + System32」，**完全忽略 PATH**（本机 PATH 里排着
# STM32CubeProgrammer 的 Qt 6.6.1，捡到就会报「无法定位程序输入点」）。
# 所以 exe 旁边必须有 Qt 6.8.2 的 DLL 与 plugins\。
function Copy-PayloadFile {
    param([string]$Src, [string]$DstDir)
    $name = Split-Path $Src -Leaf
    $dst = Join-Path $DstDir $name
    $ok = $false
    for ($i = 0; $i -lt 5 -and -not $ok; $i++) {
        try {
            Copy-Item $Src -Destination $dst -Force -ErrorAction Stop
            $ok = $true
        } catch {
            # 文件正被占用（比如服务还在退出）—— 等一下再试。
            Start-Sleep -Milliseconds 500
        }
    }
    if (-not $ok) {
        # TIP DLL 会被已经加载了它的应用（ctfmon / 各输入框宿主）占着，
        # 这种情况下换个带时间戳的文件名装，注册表指向新文件即可 ——
        # 旧文件等那些应用退出后自然释放。
        $stem = [IO.Path]::GetFileNameWithoutExtension($name)
        $ext = [IO.Path]::GetExtension($name)
        $dst = Join-Path $DstDir ("{0}.{1}{2}" -f $stem, (Get-Date -Format "yyyyMMddHHmmss"), $ext)
        Copy-Item $Src -Destination $dst -Force
        Write-Host "    [WARN] $name 正被占用，已改用新文件名安装：$(Split-Path $dst -Leaf)" -ForegroundColor Yellow
    }
    return $dst
}

foreach ($item in (Get-ChildItem -Path $PayloadDir -Force)) {
    if ($item.PSIsContainer) {
        $sub = Join-Path $InstallDir $item.Name
        if (-not (Test-Path $sub)) { New-Item -ItemType Directory -Path $sub -Force | Out-Null }
        foreach ($f in (Get-ChildItem -Path $item.FullName -Recurse -File -Force)) {
            $rel = $f.FullName.Substring($item.FullName.Length).TrimStart('\')
            $targetDir = Join-Path $sub (Split-Path $rel -Parent)
            if (-not (Test-Path $targetDir)) { New-Item -ItemType Directory -Path $targetDir -Force | Out-Null }
            $placed = Copy-PayloadFile -Src $f.FullName -DstDir $targetDir
            $copied++
            if ($item.Name -eq "plugins") { continue }   # 插件太多，不逐个打印
            if ($f.Name -eq $TsfDllName) { $TsfDllInstalled = $placed }
        }
        Write-Ok "复制 $($item.Name)\"
    } else {
        $placed = Copy-PayloadFile -Src $item.FullName -DstDir $InstallDir
        $copied++
        if ($item.Name -eq $TsfDllName) { $TsfDllInstalled = $placed }
        Write-Ok "复制 $($item.Name)"
    }
}
if ($copied -eq 0) {
    Write-Host "payload 里一个可执行文件都没找到，安装中止。" -ForegroundColor Red
    exit 1
}

Write-Step "写 start_service.bat（TIP 用它拉起服务）"
$startBat = @"
@echo off
chcp 65001 >nul
start "" "%~dp0$ExeName"
"@
$startBat | Out-File -FilePath (Join-Path $InstallDir "start_service.bat") -Encoding ascii
Write-Ok "已写 start_service.bat"

Write-Step "注册 TIP DLL（HKCU 影子注册，免提权）"
if (Invoke-RegSvr32 -dllPath $TsfDllInstalled) {
    Write-Ok "已注册 $(Split-Path $TsfDllInstalled -Leaf)"
} else {
    Write-Host "    [WARN] regsvr32 返回非零；请检查是否有杀毒软件拦截。" -ForegroundColor Yellow
}

# 清掉历史遗留的多余语言变体。
#
# 旧版注册对全部 5 种中文子语言都调过 InstallLayoutOrTip，于是 HKCU 下留下
# 繁体/港/新/澳的 Enable=1 profile，Win+Space 列表里「繁体中文(中国台湾)」组下
# 会多出一个「小狼毫」。注册逻辑已经改成只跟系统语言对齐，但残留要显式回滚。
Write-Step "清理多余的语言变体"
$cleanup = Join-Path $PSScriptRoot "cleanup-lang-variants.ps1"
if (Test-Path $cleanup) {
    & powershell -NoProfile -ExecutionPolicy Bypass -File $cleanup
    if ($LASTEXITCODE -eq 0) {
        Write-Ok "语言变体已清理"
    } else {
        Write-Host "    [WARN] 语言变体清理脚本返回 $LASTEXITCODE" -ForegroundColor Yellow
    }
} else {
    Write-Info "找不到 cleanup-lang-variants.ps1，跳过"
}

Write-Step "校验 HKCU 影子注册"
$clsidKey = "HKCU:\Software\Classes\CLSID\$OfficialClsid\InprocServer32"
if (Test-Path $clsidKey) {
    $registered = (Get-ItemProperty -Path $clsidKey -Name "(default)" -ErrorAction SilentlyContinue)."(default)"
    Write-Ok "官方 CLSID 已指向：$registered"
} else {
    Write-Host "    [WARN] HKCU 下没有官方 CLSID 的影子注册，输入法可能仍指向官方 Weasel。" -ForegroundColor Yellow
}

Write-Step "写 HKCU\Software\Rime\Weasel\WeaselRoot"
New-Item -Path "HKCU:\Software\Rime\Weasel" -Force | Out-Null
Set-ItemProperty -Path "HKCU:\Software\Rime\Weasel" -Name "WeaselRoot" -Value $InstallDir -Type String
Write-Ok "WeaselRoot = $InstallDir"

Write-Step "写自启项 HKCU\...\Run\WeaselFluent"
$runKey = "HKCU:\Software\Microsoft\Windows\CurrentVersion\Run"
New-Item -Path $runKey -Force | Out-Null
Set-ItemProperty -Path $runKey -Name "WeaselFluent" -Value ('"' + (Join-Path $InstallDir $ExeName) + '"') -Type String
Write-Ok "Run\WeaselFluent 已写入"

Write-Step "启动服务"
Start-Process -FilePath (Join-Path $InstallDir $ExeName) -WorkingDirectory $InstallDir | Out-Null
Write-Ok "已启动 $ExeName"

Write-Host ""
Write-Host "安装完成。" -ForegroundColor Green
Write-Host "  * 输入法：Win+Space 在已装输入法之间切换；语言栏里原来的「小狼毫」现在指向本仓的 Fluent 前端。" -ForegroundColor Gray
Write-Host "  * 设置中心：托盘图标右键 → 输入法设置。" -ForegroundColor Gray
Write-Host "  * 卸载：powershell -ExecutionPolicy Bypass -File scripts\uninstall.ps1" -ForegroundColor Gray
exit 0
