# scripts/test_langbar_ipc.ps1
# 语言栏（输入法工具栏）↔ 我们的服务端的**端到端**连通性测试。
#
# 背景：语言栏（输入法工具栏）不是一个独立的窗口，而是 **TIP 的一部分**：
#   * 图标与右键菜单从 TIP DLL 的资源段加载（LanguageBar.cpp 的
#     LoadMenuW / LoadImageW）；
#   * 菜单项被点中后，TIP 走 _HandleLangBarMenuSelect()：
#       - 本地处理：RERUN_SERVICE / INSTALLDIR / USERCONFIG / LOGDIR / WIKI / FORUM
#       - 其余（SETTINGS / DICT_MANAGEMENT / DEPLOY / SYNC / CHECKUPDATE /
#               HOMEPAGE / QUIT）转发给服务端：m_client.TrayCommand(wID)
#
# 转发那条路就是「工具栏直接与我们的程序连通」的关键路径：
#
#   TIP ──PipeMessage{WEASEL_IPC_TRAY_COMMAND, wParam=menuId}──▶ 命名管道
#     ──▶ ServerImpl::OnCommand() ──▶ m_MenuHandlers[menuId]()
#
# 而**语言栏最常用的交互是左键点图标切中/英**，它走的是同一个函数里
# 上游自带的另一个分支（ID_WEASELTRAY_ENABLE_ASCII / DISABLE_ASCII），
# 最终落到 m_pRequestHandler->SetOption(session_id, "ascii_mode", ...)。
# 本脚本对**这条分支**做实打实的断言：切完之后从引擎状态里读回
# `status.ascii_mode`，而不是只看「命令被接收了」。
#
# 线上格式（vendor 的 include/WeaselIPC.h + WeaselIPCServer/WeaselServerImpl.cpp）：
#   请求：PipeMessage{ uint32 Msg; uint32 wParam; uint32 lParam; } = 12 字节
#         —— 按键事件的 keycode 直接编码在 wParam 里（KeyEvent 是 4 字节的
#            {keycode:16, mask:16}），所以**没有额外的 body**；
#         —— START_SESSION 额外带一段 UTF-16LE 的客户端信息（服务端要解析它）。
#   回包：uint32（DWORD）+ 可选的 UTF-16LE 文本 body（状态/候选等）。
#
# 用法：
#   powershell -NoProfile -ExecutionPolicy Bypass -File scripts/test_langbar_ipc.ps1
#
# 退出码：0 = 通过；1 = 有检查失败；2 = 跳过（服务没在跑）

$ErrorActionPreference = "Continue"

$RepoRoot = Split-Path -Parent $PSScriptRoot

# ── 协议常量（与 vendor 的 include/WeaselIPC.h、include/resource.h 保持一致）──
$WEASEL_IPC_ECHO             = 0x8001
$WEASEL_IPC_START_SESSION    = 0x8002
$WEASEL_IPC_END_SESSION      = 0x8003
$WEASEL_IPC_PROCESS_KEY_EVENT = 0x8004
$WEASEL_IPC_CLEAR_COMPOSITION = 0x800C
$WEASEL_IPC_TRAY_COMMAND     = 0x800D

$ID_WEASELTRAY_ENABLE_ASCII  = 40013
$ID_WEASELTRAY_DISABLE_ASCII = 40014
$ID_WEASELTRAY_DEPLOY        = 40002
$ID_WEASELTRAY_SYNC          = 40012
$ID_UNREGISTERED             = 49000   # 故意不注册，用来验证「未命中 ⇒ 回包 0」

$VKEY_A = 0x41   # Rime keycode：可处理的普通字符键

function Open-Pipe {
    # 管道全名是 \\.\pipe\<用户名>\WeaselFluentNamedPipe
    # （WeaselIPC.h 的 GetPipeName()：用户名 + WEASEL_IPC_PIPE_NAME）。
    $pipe = New-Object System.IO.Pipes.NamedPipeClientStream(
        ".", "$env:USERNAME\WeaselFluentNamedPipe", [System.IO.Pipes.PipeDirection]::InOut)
    $pipe.Connect(3000)
    # 服务端是 PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE（PipeChannel.cpp 的
    # _ConnectServerPipe）⇒ 客户端也必须用消息模式，否则读回来的是碎片。
    # ⚠️ ReadMode 只能在 Connect() 之后设（句柄此时才初始化）。
    $pipe.ReadMode = [System.IO.Pipes.PipeTransmissionMode]::Message
    return $pipe
}

# 发一条 IPC 消息，返回 @{ Dword; Body; Raw }
function Send-Ipc {
    param([uint32]$Msg, [uint32]$wParam, [uint32]$lParam, [string]$Body = "")

    $pipe = Open-Pipe
    try {
        $req = New-Object byte[] 12
        [Array]::Copy([BitConverter]::GetBytes($Msg), 0, $req, 0, 4)
        [Array]::Copy([BitConverter]::GetBytes($wParam), 0, $req, 4, 4)
        [Array]::Copy([BitConverter]::GetBytes($lParam), 0, $req, 8, 4)
        $pipe.Write($req, 0, 12)
        if ($Body.Length -gt 0) {
            $b = [System.Text.Encoding]::Unicode.GetBytes($Body)
            $pipe.Write($b, 0, $b.Length)
        }
        $pipe.Flush()

        # 消息模式下一次 Read 拿到整条消息；body 紧跟在前 4 字节的 DWORD 后面。
        $buf = New-Object byte[] 65536
        $read = 0
        try {
            $n = $pipe.Read($buf, 0, $buf.Length)
            if ($n -gt 0) { $read = $n }
        } catch { }
    } finally {
        $pipe.Close()
    }

    $dword = if ($read -ge 4) { [BitConverter]::ToUInt32($buf, 0) } else { 0 }
    $bodyText = if ($read -gt 4) { [System.Text.Encoding]::Unicode.GetString($buf, 4, $read - 4) } else { "" }
    return @{ Dword = $dword; Body = $bodyText; Raw = $read }
}

function Send-TrayCommand {
    param([uint32]$MenuId, [uint32]$SessionId = 0)
    # 只回 DWORD：调用方要的是「这条命令有没有被接住」。
    return (Send-Ipc -Msg $WEASEL_IPC_TRAY_COMMAND -wParam $MenuId -lParam $SessionId).Dword
}

# 从响应 body 里读一个状态位（body 形如 "status.ascii_mode=1\nstatus.composing=0\n…"）
function Get-StatusFlag {
    param([string]$Body, [string]$Name)
    $m = [regex]::Match($Body, ("status\." + [regex]::Escape($Name) + "=(\d)"))
    if ($m.Success) { return [int]$m.Groups[1].Value }
    return -1
}

Write-Host "=== Language bar <-> server IPC end-to-end ===" -ForegroundColor Cyan

$failures = 0

# ── 0. 服务在不在？跑的是不是刚构建出来的那份？──────────────────────────────
#
# 这个测试连的是**正在跑的服务进程**（服务端是常驻服务，没法只为测试跑一次）。
# 所以要先说清楚被测的是哪一份二进制：
#   * 服务没跑 ⇒ 跳过（退出码 2），不算失败；
#   * 服务在跑但不是 build\Release 里刚构建出来的那份 ⇒ 打印醒目提示，
#     说明这次的结果只证明了「已安装的那份」是通的。
$svc = Get-Process | Where-Object { $_.ProcessName -eq 'weasel-fluent-cpp-qt' } |
        Select-Object -First 1
if (-not $svc) {
    Write-Host "SKIP: the Weasel-Fluent service is not running" -ForegroundColor Yellow
    Write-Host "      start it first (installed dir has start_service.bat)" -ForegroundColor Yellow
    exit 2
}

$freshExe = @(
    (Join-Path $RepoRoot 'build\Release\weasel-fluent-cpp-qt.exe'),
    (Join-Path $RepoRoot 'build\windows\x64\release\weasel-fluent-cpp-qt.exe')
) | Where-Object { Test-Path $_ } | Select-Object -First 1

$runningPath = $null
try { $runningPath = $svc.Path } catch { $runningPath = $null }
Write-Host "service pid: $($svc.Id)  path: $runningPath"
if ($freshExe -and $runningPath -and
    ([System.IO.Path]::GetFullPath($runningPath) -ne [System.IO.Path]::GetFullPath($freshExe))) {
    Write-Host "WARN: the running service is NOT the freshly built binary." -ForegroundColor Yellow
    Write-Host "      This run proves the INSTALLED build answers language bar commands." -ForegroundColor Yellow
    Write-Host "      Fresh build: $freshExe" -ForegroundColor Yellow
}

# ── 1. 管道可连接 ─────────────────────────────────────────────────────────
try {
    $probe = Send-TrayCommand -MenuId $ID_UNREGISTERED
    Write-Host "PASS: connected to the Weasel-Fluent pipe" -ForegroundColor Green
} catch {
    Write-Host "FAIL: cannot reach the service pipe ($_)" -ForegroundColor Red
    Write-Host "      Is the service running? Start it with the installed start_service.bat" -ForegroundColor Yellow
    exit 1
}

# ── 2. 未注册的菜单 ID ⇒ 回包 0（证明这条判据本身有效）─────────────────────
if ($probe -ne 0) {
    Write-Host "FAIL: unregistered menu id returned $probe (expected 0)" -ForegroundColor Red
    $failures++
} else {
    Write-Host "PASS: an unregistered menu id is reported as unhandled (0)" -ForegroundColor Green
}

# ── 3. 开一个会话（语言栏的命令要带 session_id 才作用于真实会话）────────────
$clientInfo = "action=session`nsession.client_app=langbar-selftest`nsession.client_type=ime`n.`n"
$start = Send-Ipc -Msg $WEASEL_IPC_START_SESSION -wParam 0 -lParam 0 -Body $clientInfo
$sessionId = [uint32]$start.Dword
if ($sessionId -eq 0) {
    Write-Host "FAIL: could not open a session with the server (START_SESSION returned 0)" -ForegroundColor Red
    $failures++
    Write-Host ""
    Write-Host "Language bar IPC end-to-end test failed" -ForegroundColor Red
    exit 1
}
Write-Host "PASS: opened a session with the engine (id=$sessionId)" -ForegroundColor Green

# ── 4. 基线：切之前 ascii_mode 应该是 0 ───────────────────────────────────
function Read-AsciiMode {
    param([uint32]$Session)
    Send-Ipc -Msg $WEASEL_IPC_CLEAR_COMPOSITION -wParam 0 -lParam $Session | Out-Null
    $r = Send-Ipc -Msg $WEASEL_IPC_PROCESS_KEY_EVENT -wParam $VKEY_A -lParam $Session
    return Get-StatusFlag -Body $r.Body -Name "ascii_mode"
}

$before = Read-AsciiMode -Session $sessionId
if ($before -eq 0) {
    Write-Host "PASS: baseline status.ascii_mode = 0 (Chinese mode)" -ForegroundColor Green
} else {
    Write-Host "FAIL: baseline status.ascii_mode = $before (expected 0)" -ForegroundColor Red
    $failures++
}

# ── 5. 语言栏左键 ⇒ ENABLE_ASCII ⇒ 引擎真的切到英文模式 ────────────────────
#
# 这一条是本测试的核心：**不是**只看「命令被接收了」，而是从引擎状态里
# 读回 ascii_mode，确认语言栏那个图标真的能切中/英。
$enable = Send-TrayCommand -MenuId $ID_WEASELTRAY_ENABLE_ASCII -SessionId $sessionId
$afterEnable = Read-AsciiMode -Session $sessionId
if ($enable -eq 1 -and $afterEnable -eq 1) {
    Write-Host "PASS: the language bar's ASCII toggle reached the engine (ascii_mode 0 -> 1)" -ForegroundColor Green
} else {
    Write-Host ("FAIL: ENABLE_ASCII handled={0}, status.ascii_mode={1} (expected 1/1)" -f $enable, $afterEnable) -ForegroundColor Red
    $failures++
}

# ── 6. 再切回来（顺便把会话状态还原，不给用户留下副作用）─────────────────
$disable = Send-TrayCommand -MenuId $ID_WEASELTRAY_DISABLE_ASCII -SessionId $sessionId
$afterDisable = Read-AsciiMode -Session $sessionId
if ($disable -eq 1 -and $afterDisable -eq 0) {
    Write-Host "PASS: the ASCII toggle is reversible (ascii_mode 1 -> 0)" -ForegroundColor Green
} else {
    Write-Host ("FAIL: DISABLE_ASCII handled={0}, status.ascii_mode={1} (expected 1/0)" -f $disable, $afterDisable) -ForegroundColor Red
    $failures++
}

# ── 7. 我们自己注册的菜单命令（部署）也能命中 ─────────────────────────────
#
# 为什么用 DEPLOY：它是**唯一**没有可见副作用的已注册命令 ——
# 设置/词典管理会弹窗口，重启服务会杀掉进程，打开目录会拉起资源管理器。
# 重新部署只是重建 Rime 的 build 缓存，正是语言栏里「部署」按钮的正常行为。
$deploy = Send-TrayCommand -MenuId $ID_WEASELTRAY_DEPLOY -SessionId $sessionId
if ($deploy -eq 1) {
    Write-Host "PASS: the language bar 'Deploy' command reached our handler (handled=1)" -ForegroundColor Green
} else {
    Write-Host "FAIL: the 'Deploy' command was NOT handled by our server (got $deploy)" -ForegroundColor Red
    $failures++
}

# ── 8. 「同步用户数据」是独立的一条命令（不是重新部署的别名）────────────────
#
# 上游 WeaselServer 把 SYNC 交给 WeaselDeployer /sync（真同步），
# 我们一度误把它映射到 deploy（= 重新编译词库），菜单写着「同步用户数据」
# 实际却在重新部署 —— 语义不对。现在是独立的 sync_user_data。
$sync = Send-TrayCommand -MenuId $ID_WEASELTRAY_SYNC -SessionId $sessionId
if ($sync -eq 1) {
    Write-Host "PASS: the language bar 'Sync user data' command reached its own handler" -ForegroundColor Green
} else {
    Write-Host "FAIL: the 'Sync user data' command was NOT handled (got $sync)" -ForegroundColor Red
    $failures++
}

# ── 9. 收尾：清组合态、结束会话 ────────────────────────────────────────────
Send-Ipc -Msg $WEASEL_IPC_CLEAR_COMPOSITION -wParam 0 -lParam $sessionId | Out-Null
Send-Ipc -Msg $WEASEL_IPC_END_SESSION -wParam 0 -lParam $sessionId | Out-Null

Write-Host ""
if ($failures -eq 0) {
    Write-Host "Language bar IPC end-to-end test passed" -ForegroundColor Green
    exit 0
}
Write-Host "$failures check(s) failed" -ForegroundColor Red
exit 1
