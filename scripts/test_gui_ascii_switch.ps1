# scripts/test_gui_ascii_switch.ps1
# 真机 GUI 端到端：单击 Shift 切中/英。
#
# 这个脚本走的是**真实的 TIP 路径**（Windows → weasel-fluent-tsf.dll →
# WeaselIPC → 服务端引擎），和 test_langbar_ipc.ps1 那种「直接跟服务端说话」
# 的测试互补：后者证明引擎逻辑对，前者证明**真键盘上的那一下**能走到引擎。
#
# ⚠️ 这个脚本需要：
#   1. 一个真实桌面会话（会在屏幕上开窗、抢焦点）；
#   2. 服务已经在跑（否则 TIP 连不上，表现为「按键没反应」）；
#   3. 本脚本会**临时**把 zh-Hans-CN 的默认文本服务改成本项目的 TIP，
#      跑完原样改回去（见下面「默认文本服务」那一节）。
#
# 不在 scripts\run-selfchecks.ps1 里 —— 那套自检要能在无人值守的 CI 里跑。

$typeDefinition = @"
using System;
using System.Runtime.InteropServices;
using System.Text;

public class G {
    [StructLayout(LayoutKind.Sequential)]
    public struct MOUSEINPUT { public int dx; public int dy; public uint mouseData; public uint dwFlags; public uint time; public IntPtr dwExtraInfo; }
    [StructLayout(LayoutKind.Sequential)]
    public struct KEYBDINPUT { public ushort wVk; public ushort wScan; public uint dwFlags; public uint time; public IntPtr dwExtraInfo; }
    [StructLayout(LayoutKind.Sequential)]
    public struct HARDWAREINPUT { public uint uMsg; public ushort wParamL; public ushort wParamH; }
    [StructLayout(LayoutKind.Explicit)]
    public struct InputUnion {
        [FieldOffset(0)] public MOUSEINPUT mi;
        [FieldOffset(0)] public KEYBDINPUT ki;
        [FieldOffset(0)] public HARDWAREINPUT hi;
    }
    [StructLayout(LayoutKind.Sequential)]
    public struct INPUT { public uint type; public InputUnion U; }

    [DllImport("user32.dll", SetLastError = true)]
    public static extern uint SendInput(uint nInputs, INPUT[] pInputs, int cbSize);
    [DllImport("user32.dll")]
    public static extern bool SetProcessDPIAware();
    [DllImport("user32.dll")]
    public static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")]
    public static extern bool SetForegroundWindow(IntPtr hWnd);
    [DllImport("user32.dll")]
    public static extern bool SetCursorPos(int X, int Y);
    [DllImport("user32.dll")]
    public static extern void mouse_event(uint dwFlags, int dx, int dy, uint dwData, IntPtr dwExtraInfo);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    public static extern int GetClassName(IntPtr hWnd, StringBuilder s, int n);
    [DllImport("user32.dll")]
    public static extern bool IsWindowVisible(IntPtr hWnd);
    [DllImport("user32.dll")]
    public static extern bool EnumWindows(EnumWindowsProc cb, IntPtr p);
    [DllImport("user32.dll")]
    public static extern uint GetWindowThreadProcessId(IntPtr hWnd, out uint pid);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    public static extern IntPtr LoadKeyboardLayout(string id, uint flags);
    [DllImport("user32.dll")]
    public static extern IntPtr ActivateKeyboardLayout(IntPtr hkl, uint flags);
    public delegate bool EnumWindowsProc(IntPtr hWnd, IntPtr p);

    public const uint INPUT_KEYBOARD = 1;
    public const uint KEYEVENTF_KEYUP = 0x0002;
    public const uint KEYEVENTF_SCANCODE = 0x0008;
    public const uint KLF_ACTIVATE = 0x00000001;

    public static void Key(ushort vk, ushort scan, bool up) {
        INPUT[] inp = new INPUT[1];
        inp[0].type = INPUT_KEYBOARD;
        inp[0].U.ki.wVk = vk;
        inp[0].U.ki.wScan = scan;
        inp[0].U.ki.dwFlags = KEYEVENTF_SCANCODE | (up ? KEYEVENTF_KEYUP : 0);
        inp[0].U.ki.time = 0;
        inp[0].U.ki.dwExtraInfo = IntPtr.Zero;
        SendInput(1, inp, Marshal.SizeOf(typeof(INPUT)));
    }

    // 列出某个进程的所有可见顶层窗口（类名 + 标题）。
    public static string VisibleWindows(uint targetPid) {
        StringBuilder sb = new StringBuilder();
        EnumWindows(delegate(IntPtr h, IntPtr p) {
            uint pid;
            GetWindowThreadProcessId(h, out pid);
            if (pid == targetPid && IsWindowVisible(h)) {
                StringBuilder cn = new StringBuilder(256);
                GetClassName(h, cn, 256);
                sb.Append(cn.ToString()).Append(" | ");
            }
            return true;
        }, IntPtr.Zero);
        return sb.ToString();
    }
}
"@
Add-Type -TypeDefinition $typeDefinition
Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing

[void][G]::SetProcessDPIAware()

$ErrorActionPreference = "Stop"

$RepoRoot = Split-Path -Parent $PSScriptRoot
$OutDir = Join-Path $RepoRoot 'build\Release\preview'
if (-not (Test-Path $OutDir)) { New-Item -ItemType Directory -Path $OutDir | Out-Null }

function Save-Screen {
    param([string]$Path)
    $b = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds
    $bmp = New-Object System.Drawing.Bitmap($b.Width, $b.Height)
    $gfx = [System.Drawing.Graphics]::FromImage($bmp)
    $gfx.CopyFromScreen($b.X, $b.Y, 0, 0, $bmp.Size)
    $gfx.Dispose()
    $bmp.Save($Path, [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
    Write-Host "  screenshot -> $Path"
}

# 消息泵。
#
# ⚠️ 这一步不是可有可无的：Form.Show() **不启动** Application.Run，
# 窗口虽然建出来了、也拿到焦点了，但没有消息循环在转 ——
# SendInput 投递过去的按键只会堆在消息队列里，永远不会被派发，
# 于是「按键没进输入框、也没进输入法」，看起来就像输入法根本没工作。
# （实测 2026-11-25：没有 DoEvents 时探针进程的按键一个都没到服务端。）
function Pump {
    param([int]$Ms = 150)
    $end = [DateTime]::UtcNow.AddMilliseconds($Ms)
    while ([DateTime]::UtcNow -lt $end) {
        [System.Windows.Forms.Application]::DoEvents()
        Start-Sleep -Milliseconds 10
    }
}

# 扫描码（PS/2 set 1）：
#   左 Shift = 0x2A，右 Shift = 0x36，字母 A = 0x1E，空格 = 0x39，回车 = 0x1C
$SC = @{ ShiftL = 0x2A; A = 0x1E; N = 0x31; I = 0x17; H = 0x23; O = 0x18; Space = 0x39; Enter = 0x1C; B = 0x30; C = 0x2E }

$VK = @{ ShiftL = 0xA0; A = 0x41; N = 0x4E; I = 0x49; H = 0x48; O = 0x4F; Space = 0x20; Enter = 0x0D; B = 0x42; C = 0x43 }

function Press-Key {
    param([string]$Name)
    [G]::Key([uint16]$VK[$Name], [uint16]$SC[$Name], $false)
    Pump 60
    [G]::Key([uint16]$VK[$Name], [uint16]$SC[$Name], $true)
    Pump 120
}

function Click-Shift {
    # 单击左 Shift：按下 → 抬手（期间不按别的键）。
    [G]::Key([uint16]$VK.ShiftL, [uint16]$SC.ShiftL, $false)
    Pump 100
    [G]::Key([uint16]$VK.ShiftL, [uint16]$SC.ShiftL, $true)
    Pump 400
}

function Type-Word {
    param([string]$Word)
    foreach ($ch in $Word.ToCharArray()) {
        $name = $ch.ToString().ToUpperInvariant()
        if ($name -eq ' ') { Press-Key 'Space' } else { Press-Key $name }
    }
}

# ── 默认文本服务：临时把 zh-Hans-CN 指到本项目的 TIP ────────────────────
#
# TSF 决定「一个线程里到底跑哪个输入法」看的不是键盘布局，而是
# **语言 → 默认文本服务**。那项记在
#   HKCU\Software\Microsoft\CTF\Assemblies\<langid>\<TIP CLSID>
# 的 Default / Profile 两个值里。
#
# 机器上 zh-Hans-CN 的默认项默认是 {34745C63-…}（就是「纯键盘布局」那个
# 空壳 provider），所以光激活 00000804 布局是不够的 —— 探针进程里
# **我们的 TIP 根本不会被激活**，按键自然到不了服务端（实测 2026-11-25）。
#
# 这里临时把它指向我们的 TIP，跑完在 finally 里原样改回去。
$OfficialClsid = "{A3F4CDED-B1E9-41EE-9CA6-7B4D0DE6CB0A}"
$OfficialProfile = "{3D02CAB6-2B8E-4781-BA20-1C9267529467}"
$asmPath = "HKCU:\Software\Microsoft\CTF\Assemblies\0x00000804\$OfficialClsid"
$asmBackup = $null
if (Test-Path $asmPath) {
    $old = Get-ItemProperty $asmPath
    $asmBackup = @{
        Default        = $old.Default
        Profile        = $old.Profile
        KeyboardLayout = $old.KeyboardLayout
    }
} else {
    New-Item -Path $asmPath -Force | Out-Null
}
Set-ItemProperty -Path $asmPath -Name 'Default' -Value $OfficialProfile -Type String
Set-ItemProperty -Path $asmPath -Name 'Profile' -Value $OfficialProfile -Type String
Set-ItemProperty -Path $asmPath -Name 'KeyboardLayout' -Value 134481924 -Type DWord
Write-Host "default text service for zh-Hans-CN -> our TIP (restored on exit)"

# ── 建一个最小宿主：标准 EDIT 控件（TSF 输入法对它生效）────────────────────
$form = New-Object System.Windows.Forms.Form
$form.Text = "Weasel-Fluent IME GUI probe"
$form.Size = New-Object System.Drawing.Size(640, 220)
$form.StartPosition = "Manual"
$form.Location = New-Object System.Drawing.Point(120, 120)
$form.TopMost = $true

$box = New-Object System.Windows.Forms.TextBox
$box.Multiline = $true
$box.Font = New-Object System.Drawing.Font("Consolas", 16)
$box.Dock = "Fill"
$form.Controls.Add($box)

$form.Show()
$form.Activate()
Pump 500

# 后台进程没有 SetForegroundWindow 权限 ⇒ 用**合成鼠标点击**把前台抢过来。
# 鼠标输入不受前台限制规则管，点击落在我们自己的窗口上即可。
$pt = $form.PointToScreen((New-Object System.Drawing.Point(60, 40)))
[void][G]::SetCursorPos($pt.X, $pt.Y)
Pump 150
[G]::mouse_event(0x0002, 0, 0, 0, [IntPtr]::Zero)   # LEFTDOWN
Pump 80
[G]::mouse_event(0x0004, 0, 0, 0, [IntPtr]::Zero)   # LEFTUP
Pump 500
$box.Focus()
Pump 200

# 键盘布局切到简体中文（线程级）。上面的默认文本服务负责「用哪个输入法」，
# 这里负责「这套输入法的布局是 00000804」。
$hkl = [G]::LoadKeyboardLayout("00000804", [G]::KLF_ACTIVATE)
if ($hkl -eq [IntPtr]::Zero) {
    Write-Host "FAIL: could not load the zh-Hans-CN keyboard layout" -ForegroundColor Red
    $form.Close()
    exit 1
}
[void][G]::ActivateKeyboardLayout($hkl, [G]::KLF_ACTIVATE)
Pump 300

$fg = [G]::GetForegroundWindow()
if ($fg -ne $form.Handle) {
    [void][G]::SetForegroundWindow($form.Handle)
    Pump 400
    $fg = [G]::GetForegroundWindow()
}
Write-Host "form handle = $($form.Handle), foreground = $fg"
if ($fg -ne $form.Handle) {
    Write-Host "FAIL: could not give the probe window the keyboard focus" -ForegroundColor Red
    Write-Host "      foreground class: $([G]::VisibleWindows(0))" -ForegroundColor DarkGray
    $form.Close()
    exit 1
}

$failures = 0

try {
    # ── 阶段 A：中文态 ──────────────────────────────────────────────────────
    $box.Clear()
    $box.Focus()
    Pump 100
    Type-Word "nihao"
    Pump 700
    $textAfterTyping = $box.Text
    Write-Host "A) after typing 'nihao' in the default state, the edit box holds: '$textAfterTyping'"
    if ($textAfterTyping -ne '') {
        Write-Host "FAIL: the composition did not start (text went straight into the box) => the IME is not active" -ForegroundColor Red
        $failures++
    }
    Press-Key 'Space'
    Pump 700
    $committed = $box.Text
    Write-Host "A) after Space, the edit box holds: '$committed'"
    if ($committed -notmatch '[\u4e00-\u9fff]') {
        Write-Host "FAIL: the IME did not commit Chinese text => the TIP path is broken" -ForegroundColor Red
        $failures++
    }
    Save-Screen (Join-Path $OutDir 'gui_1_chinese.png')

    # ── 阶段 B：中文组合态**还在**的时候单击 Shift ⇒ 英文态 ─────────────────
    #
    # 这一条是「Shift 切换中英文」真正的回归点（实测 2026-11-25）。
    #
    # 只切 ascii_mode 是不够的：librime 的 ascii_composer 在**组合态还在**的时候，
    # 会把后续按键 ctx->PushInput 进那个中文组合里，而不是放行给应用
    # （src/rime/gear/ascii_composer.cc 的 ProcessKeyEvent）。于是模式明明已经
    # 是英文，屏幕却还是一屏中文 —— 表现就是「按 Shift 切不动中英」。
    #
    # 所以这里**故意不提交**上一段拼音，直接按 Shift：按下 Shift 的那一刻
    # 中文组合态还活着，后面敲的 abc 必须直接进输入框。
    $box.Clear()
    $box.Focus()
    Pump 100
    Type-Word "nihao"
    Pump 700
    $beforeClick = $box.Text
    Write-Host "B) composition is live, the edit box holds: '$beforeClick'"
    if ($beforeClick -ne '') {
        Write-Host "FAIL: the composition did not stay live => the IME is not active" -ForegroundColor Red
        $failures++
    }
    Click-Shift
    Type-Word "abc"
    Pump 700
    $english = $box.Text
    Write-Host "B) after a Shift click + 'abc', the edit box holds: '$english'"
    if ($english -ne 'abc') {
        Write-Host "FAIL: the Shift click did not switch to English (expected 'abc')" -ForegroundColor Red
        $failures++
    }
    Save-Screen (Join-Path $OutDir 'gui_2_english.png')

    # ── 阶段 C：再单击一次 ⇒ 切回中文 ───────────────────────────────────────
    $box.Clear()
    $box.Focus()
    Pump 100
    Click-Shift
    Type-Word "nihao"
    Pump 700
    $backToChinese = $box.Text
    Write-Host "C) after a second Shift click + 'nihao', the edit box holds: '$backToChinese'"
    if ($backToChinese -ne '') {
        Write-Host "FAIL: the second Shift click did not switch back to Chinese" -ForegroundColor Red
        $failures++
    }
    Press-Key 'Space'
    Pump 700
    Write-Host "C) after Space, the edit box holds: '$($box.Text)'"
    if ($box.Text -notmatch '[\u4e00-\u9fff]') {
        Write-Host "FAIL: the second Shift click did not restore the Chinese state" -ForegroundColor Red
        $failures++
    }
    Save-Screen (Join-Path $OutDir 'gui_3_back_to_chinese.png')
} finally {
    $form.Close()
    $form.Dispose()
    # 默认文本服务还原。
    try {
        if ($asmBackup) {
            Set-ItemProperty -Path $asmPath -Name 'Default' -Value $asmBackup.Default -Type String
            Set-ItemProperty -Path $asmPath -Name 'Profile' -Value $asmBackup.Profile -Type String
            Set-ItemProperty -Path $asmPath -Name 'KeyboardLayout' -Value $asmBackup.KeyboardLayout -Type DWord
        } else {
            Remove-Item -Path $asmPath -Recurse -Force -ErrorAction SilentlyContinue
        }
        Write-Host "default text service restored"
    } catch {
        Write-Host ("WARN: could not restore the default text service: " + $_.Exception.Message) -ForegroundColor Yellow
    }
}

Write-Host ""
if ($failures -eq 0) {
    Write-Host "GUI ascii switch end-to-end test passed" -ForegroundColor Green
    exit 0
}
Write-Host "$failures check(s) failed" -ForegroundColor Red
exit 1
