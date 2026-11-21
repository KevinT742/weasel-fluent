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
    public delegate bool EnumWindowsProc(IntPtr hWnd, IntPtr p);

    public const uint INPUT_KEYBOARD = 1;
    public const uint KEYEVENTF_KEYUP = 0x0002;
    public const uint KEYEVENTF_SCANCODE = 0x0008;

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

# 扫描码（PS/2 set 1）：
#   左 Shift = 0x2A，右 Shift = 0x36，字母 A = 0x1E，空格 = 0x39，回车 = 0x1C
$SC = @{ ShiftL = 0x2A; A = 0x1E; N = 0x31; I = 0x17; H = 0x23; O = 0x18; Space = 0x39; Enter = 0x1C; B = 0x30; C = 0x2E }

$VK = @{ ShiftL = 0xA0; A = 0x41; N = 0x4E; I = 0x49; H = 0x48; O = 0x4F; Space = 0x20; Enter = 0x0D; B = 0x42; C = 0x43 }

function Press-Key {
    param([string]$Name)
    [G]::Key([uint16]$VK[$Name], [uint16]$SC[$Name], $false)
    Start-Sleep -Milliseconds 40
    [G]::Key([uint16]$VK[$Name], [uint16]$SC[$Name], $true)
    Start-Sleep -Milliseconds 60
}

function Click-Shift {
    # 单击左 Shift：按下 → 抬手（期间不按别的键）。
    [G]::Key([uint16]$VK.ShiftL, [uint16]$SC.ShiftL, $false)
    Start-Sleep -Milliseconds 80
    [G]::Key([uint16]$VK.ShiftL, [uint16]$SC.ShiftL, $true)
    Start-Sleep -Milliseconds 300
}

function Type-Word {
    param([string]$Word)
    foreach ($ch in $Word.ToCharArray()) {
        $name = $ch.ToString().ToUpperInvariant()
        if ($name -eq ' ') { Press-Key 'Space' } else { Press-Key $name }
    }
}

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
Start-Sleep -Milliseconds 500

# 后台进程没有 SetForegroundWindow 权限 ⇒ 用**合成鼠标点击**把前台抢过来。
# 鼠标输入不受前台限制规则管，点击落在我们自己的窗口上即可。
$pt = $form.PointToScreen((New-Object System.Drawing.Point(60, 40)))
[void][G]::SetCursorPos($pt.X, $pt.Y)
Start-Sleep -Milliseconds 150
[G]::mouse_event(0x0002, 0, 0, 0, [IntPtr]::Zero)   # LEFTDOWN
Start-Sleep -Milliseconds 80
[G]::mouse_event(0x0004, 0, 0, 0, [IntPtr]::Zero)   # LEFTUP
Start-Sleep -Milliseconds 500
$box.Focus()

$fg = [G]::GetForegroundWindow()
if ($fg -ne $form.Handle) {
    [void][G]::SetForegroundWindow($form.Handle)
    Start-Sleep -Milliseconds 400
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
    Type-Word "nihao"
    Start-Sleep -Milliseconds 700
    $textAfterTyping = $box.Text
    Write-Host "A) after typing 'nihao' in the default state, the edit box holds: '$textAfterTyping'"
    if ($textAfterTyping -ne '') {
        Write-Host "FAIL: the composition did not start (text went straight into the box) => the IME is not active" -ForegroundColor Red
        $failures++
    }
    Press-Key 'Space'
    Start-Sleep -Milliseconds 700
    $committed = $box.Text
    Write-Host "A) after Space, the edit box holds: '$committed'"
    if ($committed -notmatch '[\u4e00-\u9fff]') {
        Write-Host "FAIL: the IME did not commit Chinese text => the TIP path is broken" -ForegroundColor Red
        $failures++
    }
    Save-Screen (Join-Path $OutDir 'gui_1_chinese.png')

    # ── 阶段 B：单击左 Shift ⇒ 英文态 ───────────────────────────────────────
    $box.Clear()
    $box.Focus()
    Click-Shift
    Type-Word "abc"
    Start-Sleep -Milliseconds 700
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
    Click-Shift
    Type-Word "nihao"
    Start-Sleep -Milliseconds 700
    $backToChinese = $box.Text
    Write-Host "C) after a second Shift click + 'nihao', the edit box holds: '$backToChinese'"
    if ($backToChinese -ne '') {
        Write-Host "FAIL: the second Shift click did not switch back to Chinese" -ForegroundColor Red
        $failures++
    }
    Press-Key 'Space'
    Start-Sleep -Milliseconds 700
    Write-Host "C) after Space, the edit box holds: '$($box.Text)'"
    if ($box.Text -notmatch '[\u4e00-\u9fff]') {
        Write-Host "FAIL: the second Shift click did not restore the Chinese state" -ForegroundColor Red
        $failures++
    }
    Save-Screen (Join-Path $OutDir 'gui_3_back_to_chinese.png')
} finally {
    $form.Close()
    $form.Dispose()
}

Write-Host ""
if ($failures -eq 0) {
    Write-Host "GUI ascii switch end-to-end test passed" -ForegroundColor Green
    exit 0
}
Write-Host "$failures check(s) failed" -ForegroundColor Red
exit 1
