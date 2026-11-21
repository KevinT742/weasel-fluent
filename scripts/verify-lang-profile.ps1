# scripts/verify-lang-profile.ps1
# 校验「小狼毫」在系统语言列表里**只以系统 UI 语言对应的那一个变体**出现。
#
# 为什么必须查（这是真实踩过的坑）：
#   旧版注册逻辑对全部 5 种中文子语言都调 `InstallLayoutOrTip`，Windows 会
#   在 HKCU 下建出 4 个 `Enable=1` 的繁体/港/新/澳 profile；
#   TSF 于是把这些 (TIP, 语言) 组合也列为「可添加的输入语言」——
#   Win+Space 列表里「繁体中文(中国台湾)」组下就多出一个「小狼毫」。
#   编译链接全绿、功能却是坏的，属于最难发现的那类回归。
#
# 断言：
#   1. HKCU 的 LanguageProfile 下只有系统 UI 语言对应的那一个 LANGID 子键；
#   2. 该子键的 Description 是约定的显示名；
#   3. HKCU\Keyboard Layout\Preload 里没有多余变体的 d001/孤儿布局项；
#   4. 机器级（HKLM）没有多余变体的 profile —— 需要管理员才能删，
#      非管理员运行时降级为 WARN（不计入失败）。
#
# 用法：
#   powershell -NoProfile -ExecutionPolicy Bypass -File scripts/verify-lang-profile.ps1
#
# 退出码：0 = 通过；1 = 有检查失败

$ErrorActionPreference = "Continue"

$OfficialClsid = "{A3F4CDED-B1E9-41EE-9CA6-7B4D0DE6CB0A}"
$OfficialProfile = "{3D02CAB6-2B8E-4781-BA20-1C9267529467}"
$ExpectedDesc = "小狼毫（简体中文）"

# 全部 5 种中文子语言的 LANGID（与 tip/weasel_tsf_register.cpp 的 kLangIds 一致）。
$AllZhLangs = @(0x0804, 0x0404, 0x0c04, 0x1004, 0x1404)

$problems = @()

function Write-Step($text) { Write-Host $text -ForegroundColor Cyan }

if (-not ("WeaselLangProbe" -as [type])) {
    Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;

public static class WeaselLangProbe {
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

$sysLcid = [WeaselLangProbe]::GetUserDefaultUILanguage()
$keepLang = switch ($sysLcid) {
    0x0404 { 0x0404 }
    0x0c04 { 0x0c04 }
    0x1004 { 0x1004 }
    default { 0x0804 }
}
$staleLangs = @($AllZhLangs | Where-Object { $_ -ne $keepLang })

Write-Host "=== Language profile verification ===" -ForegroundColor Cyan
Write-Step ("系统 UI LCID = 0x{0:X4}  →  保留变体 0x{1:X4}  多余变体: {2}" -f `
    $sysLcid, $keepLang, (($staleLangs | ForEach-Object { "0x{0:X4}" -f $_ }) -join ", "))

function Get-LangKeyName([int]$langId) {
    return "0x{0:X8}" -f $langId
}

# ── 1. HKCU 的语言 profile ──────────────────────────────────────────────
$hkcuTip = "HKCU:\Software\Microsoft\CTF\TIP\$OfficialClsid\LanguageProfile"
if (-not (Test-Path $hkcuTip)) {
    Write-Host "FAIL: HKCU 下没有 $OfficialClsid 的 LanguageProfile —— TIP 尚未注册" -ForegroundColor Red
    $problems += "HKCU LanguageProfile is missing (the TIP is not registered)"
} else {
    $present = @()
    Get-ChildItem $hkcuTip -ErrorAction SilentlyContinue | ForEach-Object {
        $present += $_.PSChildName
    }

    foreach ($lang in $staleLangs) {
        $name = Get-LangKeyName $lang
        if ($present -contains $name) {
            $problems += "HKCU still has the stale variant profile $name"
            Write-Host ("FAIL: HKCU 仍存在多余变体的语言 profile {0}" -f $name) -ForegroundColor Red
        }
    }

    $keepName = Get-LangKeyName $keepLang
    if (-not ($present -contains $keepName)) {
        $problems += "HKCU is missing the active variant profile $keepName"
        Write-Host ("FAIL: HKCU 缺少生效变体的语言 profile {0}" -f $keepName) -ForegroundColor Red
    } else {
        $profileKey = Join-Path $hkcuTip "$keepName\$OfficialProfile"
        if (-not (Test-Path $profileKey)) {
            $problems += "HKCU $keepName has no profile entry for our profile GUID"
            Write-Host ("FAIL: HKCU {0} 下没有 profile {1}" -f $keepName, $OfficialProfile) -ForegroundColor Red
        } else {
            $desc = (Get-ItemProperty -Path $profileKey -Name "Description" -ErrorAction SilentlyContinue).Description
            if ($desc -ne $ExpectedDesc) {
                $problems += "HKCU $keepName Description is '$desc', expected '$ExpectedDesc'"
                Write-Host ("FAIL: 显示名是 '{0}'，期望 '{1}'" -f $desc, $ExpectedDesc) -ForegroundColor Red
            } else {
                Write-Host ("PASS: HKCU 生效变体 {0} 的显示名是「{1}」" -f $keepName, $desc) -ForegroundColor Green
            }
        }
    }

    if ($problems.Count -eq 0) {
        Write-Host "PASS: HKCU 下只有系统 UI 语言对应的那一个语言变体" -ForegroundColor Green
    }
}

# ── 2. 键盘布局 Preload 里的残留 ────────────────────────────────────────
#
# d001xxxx 是 TSF profile 专有的布局标记，只有注册 TIP 才会产生；
# 0000xxxx 里没被用户语言列表用到的则是孤儿（语言装过又删了）。
$preloadKey = "HKCU:\Keyboard Layout\Preload"
if (Test-Path $preloadKey) {
    $key = [Microsoft.Win32.Registry]::CurrentUser.OpenSubKey("Keyboard Layout\Preload")
    $layouts = @()
    if ($key) {
        # ⚠️ Preload 里的每一项都是**独立的 REG_SZ**（值名 "1" "2" …），
        #    不是「默认值」那个 MULTI_SZ —— 读 GetValue($null) 只会拿到空。
        foreach ($valueName in $key.GetValueNames()) {
            $v = $key.GetValue($valueName)
            if ($v) { $layouts += "$v" }
        }
        $key.Close()
    }

    # 用户语言列表里实际在用的布局（语言 Tag → LANGID）。
    #
    # ⚠️ `Get-WinUserLanguageList` 返回的是 `List<WinUserLanguage>`，
    #    用 `@(...)` 包**不会**展开（实测 Count=1、LanguageTag 变成
    #    "zh-Hans-CN en-US zh-Hant-TW" 拼起来的一坨）——
    #    必须走成员访问枚举 `$langList.LanguageTag`。
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

    foreach ($layout in $layouts) {
        $layoutStr = "$layout"
        $langId = $null
        # d001xxxx 是 TSF profile 的布局标记：前缀 4 位 + 语言码 4 位。
        # 0000xxxx 是标准键盘布局：8 位十六进制，前导零要吃掉。
        if ($layoutStr -match '^d001([0-9a-fA-F]{4})$') {
            $langId = [Convert]::ToInt32($Matches[1], 16)
        } elseif ($layoutStr -match '^0*([0-9a-fA-F]{4})$') {
            $langId = [Convert]::ToInt32($Matches[1], 16)
        }
        if ($null -eq $langId) { continue }
        if ($staleLangs -notcontains $langId) { continue }
        if ($inUse.ContainsKey($langId)) { continue }   # 该语言仍在用 → 保留

        $problems += "HKCU Keyboard Layout Preload still lists the stale layout '$layoutStr'"
        Write-Host ("FAIL: Preload 里仍有残留布局项 {0}" -f $layoutStr) -ForegroundColor Red
    }
    Write-Host ("    Preload 当前项: {0}" -f ($layouts -join ", ")) -ForegroundColor DarkGray
}

# ── 3. 机器级 profile ───────────────────────────────────────────────────
$hklmTip = "HKLM:\Software\Microsoft\CTF\TIP\$OfficialClsid\LanguageProfile"
if (Test-Path $hklmTip) {
    $machineStale = @()
    foreach ($lang in $staleLangs) {
        $name = Get-LangKeyName $lang
        if (Test-Path (Join-Path $hklmTip $name)) { $machineStale += $name }
    }
    if ($machineStale.Count -gt 0) {
        $msg = "机器级仍有 " + ($machineStale -join ", ") + " 的语言 profile"
        if ([WeaselLangProbe]::IsAdmin()) {
            $problems += $msg
            Write-Host ("FAIL: {0}" -f $msg) -ForegroundColor Red
        } else {
            Write-Host ("WARN: {0}（需要管理员权限才能删除；请用 scripts\cleanup-lang-variants.ps1 -Elevate）" -f $msg) -ForegroundColor Yellow
        }
    } else {
        Write-Host "PASS: 机器级没有多余变体的语言 profile" -ForegroundColor Green
    }
}

Write-Host ""
if ($problems.Count -eq 0) {
    Write-Host "Language profile verification passed" -ForegroundColor Green
    exit 0
}
foreach ($p in $problems) {
    Write-Host "  - $p" -ForegroundColor Red
}
exit 1
