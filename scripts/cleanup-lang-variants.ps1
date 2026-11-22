# scripts/cleanup-lang-variants.ps1
# 清理「小狼毫」在系统语言列表里的**多余语言变体**。
#
# ── 为什么需要 ──────────────────────────────────────────────────────────
# 旧版注册逻辑对全部 5 种中文子语言都调过 `InstallLayoutOrTip`：
#   * Windows 在 HKCU\...\TIP\{A3F4CDED-…}\LanguageProfile 下建出
#     `Enable=1` 的繁体/港/新/澳 profile（只有 Enable 一个值，描述取机器级）；
#   * 并把 00000c04 / 00001004 / 00001404 及对应的 d001xxxx 布局写进了
#     `HKCU\Keyboard Layout\Preload`。
# 结果：TSF 把这些 (TIP, 语言) 组合也当成「可添加的输入语言」——
# Win+Space 列表里「繁体中文(中国台湾)」组下就多出一个「小狼毫」。
#
# 现在注册逻辑只跟系统 UI 语言对齐（见 tip/weasel_tsf_register.cpp），
# 但历史残留不会自己消失 —— 这个脚本负责回滚。
#
# ── 做什么 ──────────────────────────────────────────────────────────────
#   用户级（免提权）：
#     1. 删 HKCU 下与系统语言不匹配的语言 profile 键；
#     2. 写好生效变体的 Description（显示名）/ IconFile / Enable；
#     3. 清 Preload 里多余变体的布局项（d001xxxx + 没人用的孤儿 0000xxxx），
#        并把剩下的重新编号（值名必须是连续的 1..N）。
#   机器级（需管理员）：
#     4. 删 HKLM 下与系统语言不匹配的语言 profile 键
#        （那是官方 Weasel 安装器留下的，用户级删不掉）。
#
# ── 用法 ────────────────────────────────────────────────────────────────
#   # 用户级清理（免提权）
#   powershell -NoProfile -ExecutionPolicy Bypass -File scripts\cleanup-lang-variants.ps1
#
#   # 连机器级一起清理（自动弹 UAC）
#   powershell -NoProfile -ExecutionPolicy Bypass -File scripts\cleanup-lang-variants.ps1 -Elevate
#
#   # 只做机器级（请在管理员 PowerShell 里跑）
#   powershell -NoProfile -ExecutionPolicy Bypass -File scripts\cleanup-lang-variants.ps1 -MachineOnly
#
# 退出码：0 = 成功；1 = 有检查失败

param(
    [switch]$Elevate,      # 自动提权执行机器级部分
    [switch]$MachineOnly,  # 只做机器级清理
    [switch]$WhatIf        # 只报告要做什么，不真的改
)

$ErrorActionPreference = "Continue"

$OfficialClsid = "{A3F4CDED-B1E9-41EE-9CA6-7B4D0DE6CB0A}"
$OfficialProfile = "{3D02CAB6-2B8E-4781-BA20-1C9267529467}"
$ExpectedDesc = "小狼毫（简体中文）"

$AllZhLangs = @(0x0804, 0x0404, 0x0c04, 0x1004, 0x1404)

function Write-Step($text) { Write-Host "==> $text" -ForegroundColor Cyan }
function Write-Ok($text) { Write-Host "    [OK] $text" -ForegroundColor Green }
function Write-Info($text) { Write-Host "    $text" -ForegroundColor DarkGray }
function Write-Warn($text) { Write-Host "    [WARN] $text" -ForegroundColor Yellow }

if (-not ("WeaselLangTool" -as [type])) {
    Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;

public static class WeaselLangTool {
    [DllImport("kernel32.dll")]
    public static extern ushort GetUserDefaultUILanguage();

    public static bool IsAdmin() {
        var id = System.Security.Principal.WindowsIdentity.GetCurrent();
        var p = new System.Security.Principal.WindowsPrincipal(id);
        return p.IsInRole(System.Security.Principal.WindowsBuiltInRole.Administrator);
    }
}
"@
}

$sysLcid = [WeaselLangTool]::GetUserDefaultUILanguage()
$keepLang = switch ($sysLcid) {
    0x0404 { 0x0404 }
    0x0c04 { 0x0c04 }
    0x1004 { 0x1004 }
    default { 0x0804 }
}
$staleLangs = @($AllZhLangs | Where-Object { $_ -ne $keepLang })

function Get-LangKeyName([int]$langId) { return "0x{0:X8}" -f $langId }

Write-Step "小狼毫语言变体清理"
Write-Info ("系统 UI LCID = 0x{0:X4}  →  保留 0x{1:X4}，清理 {2}" -f `
    $sysLcid, $keepLang, (($staleLangs | ForEach-Object { "0x{0:X4}" -f $_ }) -join ", "))

# ── 机器级：需要管理员 ──────────────────────────────────────────────────
if ($MachineOnly) {
    Write-Step "机器级清理（HKLM）"
    if (-not [WeaselLangTool]::IsAdmin()) {
        Write-Host "需要管理员权限：请在管理员 PowerShell 里重新运行本脚本。" -ForegroundColor Red
        exit 1
    }
    $hklmTip = "HKLM:\Software\Microsoft\CTF\TIP\$OfficialClsid\LanguageProfile"
    $removed = 0
    foreach ($lang in $staleLangs) {
        $name = Get-LangKeyName $lang
        $key = Join-Path $hklmTip $name
        if (Test-Path $key) {
            if ($WhatIf) {
                Write-Info "[WhatIf] 将删除 $key"
            } else {
                Remove-Item -Path $key -Recurse -Force -ErrorAction SilentlyContinue
            }
            $removed++
        }
    }
    if ($removed -gt 0) {
        Write-Ok "机器级已清理 $removed 个多余变体的语言 profile"
    } else {
        Write-Info "机器级没有多余变体的语言 profile"
    }
    exit 0
}

if ($Elevate) {
    Write-Step "请求管理员权限以清理机器级注册"
    $self = $PSCommandPath
    $args = @("-NoProfile", "-ExecutionPolicy", "Bypass", "-File", $self, "-MachineOnly")
    if ($WhatIf) { $args += "-WhatIf" }
    try {
        $p = Start-Process -FilePath "powershell" -ArgumentList $args -Verb RunAs -Wait -PassThru
        Write-Info "机器级清理进程退出码 = $($p.ExitCode)"
    } catch {
        Write-Warn "提权失败（UAC 被取消？）：$($_.Exception.Message)"
    }
}

# ── 用户级：语言 profile 键 ─────────────────────────────────────────────
Write-Step "用户级清理（HKCU）"
$hkcuTip = "HKCU:\Software\Microsoft\CTF\TIP\$OfficialClsid\LanguageProfile"

foreach ($lang in $staleLangs) {
    $name = Get-LangKeyName $lang
    $key = Join-Path $hkcuTip $name
    if (Test-Path $key) {
        if ($WhatIf) {
            Write-Info "[WhatIf] 将删除 $key"
        } else {
            Remove-Item -Path $key -Recurse -Force -ErrorAction SilentlyContinue
            Write-Ok "已删除多余变体的用户级 profile $name"
        }
    }
}

# 生效变体的 profile 键要**在语言列表写入之后**再写（见下面那一节的说明），
# 这里先把要用到的路径算出来。
$keepName = Get-LangKeyName $keepLang
$keepKey = Join-Path $hkcuTip "$keepName\$OfficialProfile"
$iconFile = ""
$clsidKey = "HKCU:\Software\Classes\CLSID\$OfficialClsid\InprocServer32"
if (Test-Path $clsidKey) {
    $iconFile = (Get-ItemProperty -Path $clsidKey -Name "(default)" -ErrorAction SilentlyContinue)."(default)"
}
if (-not $iconFile) {
    $iconFile = Join-Path $env:LOCALAPPDATA "Programs\WeaselFluent\weasel-fluent-tsf.dll"
}

# ── 用户级：语言列表里的 TIP 条目 ────────────────────────────────────────
#
# 这是「能在 Win+Space / 任务栏指示器里**点到**小狼毫并切过去」的前提：
# 系统语言列表里没有这条，切换器里就没有这个输入法（或者有条目但点不动）。
#
# 列表项由 Set-WinUserLanguageList 维护（「设置 → 时间和语言 → 语言选项」背后
# 的那套 WinRT API），逐用户、不需要管理员。
#
# ⚠️ 绝不能用系统的 InstallLayoutOrTip 来做这件事：对**逐用户注册**的 TIP，
# 它会把列表里原有的这一条**删掉**，并往 HKCU\Keyboard Layout\Preload 里注入
# 00000c04 / 00001004 / 00001404 三个多余的中文变体（实测 2026-11-22）。
$langTag = switch ($keepLang) {
    0x0404 { "zh-Hant-TW" }
    0x0c04 { "zh-Hant-HK" }
    0x1004 { "zh-Hans-SG" }
    0x1404 { "zh-Hant-MO" }
    default { "zh-Hans-CN" }
}
$tipEntry = "0804:$OfficialClsid$OfficialProfile"
$langList = Get-WinUserLanguageList
$target = $langList | Where-Object { $_.LanguageTag -eq $langTag } | Select-Object -First 1
if (-not $target) {
    Write-Warn "用户语言列表里没有 $langTag，跳过 TIP 条目写入"
} elseif ($target.InputMethodTips -contains $tipEntry) {
    Write-Ok "$langTag 的输入方法列表里已经有本 TIP"
} else {
    if (-not $WhatIf) {
        # 追加而不是覆盖：同语言下的其它输入法（微软拼音等）必须保留。
        $target.InputMethodTips.Add($tipEntry)
        Set-WinUserLanguageList $langList -Force
    }
    Write-Ok "$langTag 的输入方法列表已加入本 TIP"
}

# ── 用户级：生效变体的 profile 键（写齐 Description / IconFile / Enable）──
#
# ⚠️ **必须排在语言列表写入之后**（实测 2026-11-22）：
# `Set-WinUserLanguageList -Force` 会让系统重新走一遍「注册这条 TIP」的流程，
# 顺手把 HKCU 下**逐用户**的 LanguageProfile 键删掉（机器级没有我们的键，
# 于是删干净了）。所以顺序反了的话，刚写好的 Description / IconFile /
# Enable 全被抹掉 —— 表现就是「切换器里点得到小狼毫，但指示器没有字形」。
#
# IconFile 取影子注册里实际指向的 DLL（安装目录里的那个可能带时间戳）。
if (-not $WhatIf) {
    New-Item -Path $keepKey -Force | Out-Null
    Set-ItemProperty -Path $keepKey -Name "Description" -Value $ExpectedDesc -Type String
    if ($iconFile -and (Test-Path $iconFile)) {
        Set-ItemProperty -Path $keepKey -Name "IconFile" -Value $iconFile -Type String
    }
    Set-ItemProperty -Path $keepKey -Name "IconIndex" -Value 0 -Type DWord
    Set-ItemProperty -Path $keepKey -Name "Enable" -Value 1 -Type DWord
}
Write-Ok "生效变体 $keepName 的显示名已设为「$ExpectedDesc」"

# ── 用户级：键盘布局 Preload ────────────────────────────────────────────
#
# Preload 的每一项是独立的 REG_SZ（值名 "1" "2" …），删项后必须**重新编号**，
# 否则值名不连续，Windows 读布局列表会出问题。
$preloadPath = "Keyboard Layout\Preload"
$preload = [Microsoft.Win32.Registry]::CurrentUser.OpenSubKey($preloadPath, $true)
if ($preload) {
    $entries = @()
    foreach ($name in $preload.GetValueNames()) {
        $entries += [pscustomobject]@{ Name = $name; Value = "$($preload.GetValue($name))" }
    }

    # 用户语言列表里真正在用的语言（它们的布局要留着）。
    $inUse = @{}
    $langList = Get-WinUserLanguageList -ErrorAction SilentlyContinue
    foreach ($tag in @($langList.LanguageTag)) {
        switch -Regex ("$tag") {
            '^zh-Hans-CN$'  { $inUse[0x0804] = $true }
            '^zh-Hant-TW$'  { $inUse[0x0404] = $true }
            '^zh-Hant-HK$'  { $inUse[0x0c04] = $true }
            '^zh-Hans-SG$'  { $inUse[0x1004] = $true }
            '^zh-Hant-MO$'  { $inUse[0x1404] = $true }
        }
    }

    $kept = @()
    foreach ($e in $entries) {
        $value = $e.Value
        $langId = $null
        $isProfileLayout = $false
        if ($value -match '^d001([0-9a-fA-F]{4})$') {
            # d001xxxx = TSF profile 专有的布局标记，一定是我们注册 TIP 造成的。
            $langId = [Convert]::ToInt32($Matches[1], 16)
            $isProfileLayout = $true
        } elseif ($value -match '^0*([0-9a-fA-F]{4})$') {
            $langId = [Convert]::ToInt32($Matches[1], 16)
        }

        $stale = ($null -ne $langId) -and ($staleLangs -contains $langId) -and
                 ($isProfileLayout -or -not $inUse.ContainsKey($langId))
        if ($stale) {
            Write-Info "[清理] 布局项 $value （$($e.Name)）"
        } else {
            $kept += $e
        }
    }

    if ($kept.Count -ne $entries.Count) {
        if (-not $WhatIf) {
            foreach ($name in $preload.GetValueNames()) {
                $preload.DeleteValue($name, $false)
            }
            for ($i = 0; $i -lt $kept.Count; $i++) {
                $preload.SetValue([string]($i + 1), $kept[$i].Value,
                                  [Microsoft.Win32.RegistryValueKind]::String)
            }
        }
        Write-Ok ("Preload 已从 {0} 项精简到 {1} 项并重新编号" -f $entries.Count, $kept.Count)
    } else {
        Write-Info "Preload 里没有多余变体的布局项"
    }
    $preload.Close()
}

# ── 用户级：Substitutes 里的对应残留 ────────────────────────────────────
$subPath = "Keyboard Layout\Substitutes"
$sub = [Microsoft.Win32.Registry]::CurrentUser.OpenSubKey($subPath, $true)
if ($sub) {
    foreach ($name in @($sub.GetValueNames())) {
        $langId = $null
        if ($name -match '^0*([0-9a-fA-F]{4})$') {
            $langId = [Convert]::ToInt32($Matches[1], 16)
        }
        if ($null -ne $langId -and $staleLangs -contains $langId) {
            if (-not $WhatIf) {
                $sub.DeleteValue($name, $false)
            }
            Write-Info "[清理] Substitutes 项 $name"
        }
    }
    $sub.Close()
}

# ── 安装目录：陈旧的 TIP DLL 副本 ────────────────────────────────────────
#
# TIP DLL 被宿主进程加载后不能覆盖，安装器因此会退回「带时间戳的文件名」。
# 每次升级都在安装目录里留下一份旧副本（实测 2026-11-22 已积到 9 份），
# 而注册表（COM 影子 + profile 的 IconFile）只指向当前那一份。
# 这里把**没有**被注册表引用、且没有被进程占用的旧副本清掉。
$installDir = Join-Path $env:LOCALAPPDATA "Programs\WeaselFluent"
if (Test-Path $installDir) {
    $registered = ""
    if ($iconFile) {
        try { $registered = [System.IO.Path]::GetFullPath($iconFile) } catch { $registered = "" }
    }
    $removed = 0
    $copies = @(Get-ChildItem -Path $installDir -Filter "weasel-fluent-tsf*.dll" -File -ErrorAction SilentlyContinue)
    foreach ($f in $copies) {
        $full = [System.IO.Path]::GetFullPath($f.FullName)
        if ($registered -and $full -eq $registered) {
            continue
        }
        if (-not $WhatIf) {
            try {
                Remove-Item -LiteralPath $f.FullName -Force -ErrorAction Stop
                $removed++
                Write-Info "[清理] 旧 TIP 副本 $(Split-Path $f.FullName -Leaf)"
            } catch {
                Write-Warn "旧 TIP 副本 $(Split-Path $f.FullName -Leaf) 仍被进程占用，跳过"
            }
        } else {
            Write-Info "[清理] 旧 TIP 副本 $(Split-Path $f.FullName -Leaf)"
        }
    }
    if ($removed -gt 0) {
        Write-Ok "已清理 $removed 份旧 TIP 副本"
    } elseif ($copies.Count -le 1) {
        Write-Info "安装目录里没有多余的 TIP 副本"
    }
}

Write-Host ""
Write-Host "清理完成。注销或重启一次 Windows 才会刷新语言面板的显示。" -ForegroundColor Green
exit 0
