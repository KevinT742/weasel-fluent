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
        #
        # ⚠️ 必须自己加超时：NamedPipeClientStream **不支持** ReadTimeout
        # （设了会抛 "Timeouts are not supported on this stream"），而服务进程
        # 崩掉时管道没人应答，Read 会**永远**挂着 —— 实测 2026-11-22：服务在
        # rime.dll 里崩掉之后，自检就这么卡住了，看起来像「测试没过」，
        # 实际上是「服务死了」。这两种情况必须能区分开。
        $buf = New-Object byte[] 65536
        $read = 0
        $task = $pipe.ReadAsync($buf, 0, $buf.Length)
        try {
            $done = $task.Wait(8000)
        } catch {
            throw ("pipe read failed: " + $_.Exception.GetBaseException().Message)
        }
        if (-not $done) {
            throw "no response from the Weasel-Fluent service within 8s (the service is probably dead)"
        }
        $n = $task.Result
        if ($n -gt 0) { $read = $n }
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

# 从响应 body 里读预编辑串（body 形如 "ctx.preedit=ni hao\n…"）。
# 没有组合态时这一行整个不存在 ⇒ 返回空串。
function Get-Preedit {
    param([string]$Body)
    $m = [regex]::Match($Body, "ctx\.preedit=(.*)")
    if ($m.Success) { return $m.Groups[1].Value.Trim() }
    return ""
}

Write-Host "=== Language bar <-> server IPC end-to-end ===" -ForegroundColor Cyan

$failures = 0

# ── 0. 服务在不在？跑的是不是刚构建出来的那份？──────────────────────────────
#
# 这个测试连的是**正在跑的服务进程**（服务端是常驻服务，没法只为测试跑一次）。
# 所以要先说清楚被测的是哪一份二进制：
#   * 服务没跑 ⇒ 跳过（退出码 2），不算失败；
#   * 服务在跑但内容不是 build\Release 里刚构建出来的那份 ⇒ 打印醒目提示，
#     说明这次的结果只证明了「已安装的那份」是通的。
#
# 比对的是**文件内容哈希**，不是路径：服务跑的是安装目录里的那份，
# 按路径比必然不相等 ⇒ 每次都报一句无意义的 WARN，久了就没人看了。
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
if ($freshExe -and $runningPath -and (Test-Path $runningPath)) {
    $freshHash = (Get-FileHash $freshExe -Algorithm SHA256).Hash
    $runHash = (Get-FileHash $runningPath -Algorithm SHA256).Hash
    if ($freshHash -ne $runHash) {
        Write-Host "WARN: the running service is NOT the freshly built binary." -ForegroundColor Yellow
        Write-Host "      This run proves the INSTALLED build answers language bar commands." -ForegroundColor Yellow
        Write-Host "      fresh: $freshHash" -ForegroundColor Yellow
        Write-Host "      running: $runHash" -ForegroundColor Yellow
    } else {
        Write-Host "service binary matches the fresh build (sha256 $freshHash)" -ForegroundColor DarkGray
    }
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

# 轮询直到 ascii_mode 变成期望值（超时返回最后一次读到的值）。
#
# 为什么必须轮询（2026-11-22 实测）：**librime 的 ascii_mode 是异步生效的** ——
# set_option 之后，get_status 要等引擎处理完待办事件才会反映出来。立刻读
# 一次会读到**改动前**的旧值：同一个测试连跑三次，读到的 ascii_mode 依次是
# 0 / 1 / 0 —— 纯粹是读早了，不是切换失败。极端情况下 get_status 直接失败，
# 回包里就没有 status.ascii_mode 这一行，Get-StatusFlag 返回 -1。
#
# 人手连点两下 Shift 之间隔了几百毫秒，引擎早跟上了；自检必须自己等。
function Wait-AsciiMode {
    param([uint32]$Session, [int]$Expected, [int]$TimeoutMs = 3000)
    $deadline = [DateTime]::UtcNow.AddMilliseconds($TimeoutMs)
    $last = -1
    while ([DateTime]::UtcNow -lt $deadline) {
        $last = Read-AsciiMode -Session $Session
        if ($last -eq $Expected) {
            return $last
        }
        Start-Sleep -Milliseconds 50
    }
    return $last
}

$before = Wait-AsciiMode -Session $sessionId -Expected 0
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
$afterEnable = Wait-AsciiMode -Session $sessionId -Expected 1
if ($enable -eq 1 -and $afterEnable -eq 1) {
    Write-Host "PASS: the language bar's ASCII toggle reached the engine (ascii_mode 0 -> 1)" -ForegroundColor Green
} else {
    Write-Host ("FAIL: ENABLE_ASCII handled={0}, status.ascii_mode={1} (expected 1/1)" -f $enable, $afterEnable) -ForegroundColor Red
    $failures++
}

# ── 6. 再切回来（顺便把会话状态还原，不给用户留下副作用）─────────────────
$disable = Send-TrayCommand -MenuId $ID_WEASELTRAY_DISABLE_ASCII -SessionId $sessionId
$afterDisable = Wait-AsciiMode -Session $sessionId -Expected 0
if ($disable -eq 1 -and $afterDisable -eq 0) {
    Write-Host "PASS: the ASCII toggle is reversible (ascii_mode 1 -> 0)" -ForegroundColor Green
} else {
    Write-Host ("FAIL: DISABLE_ASCII handled={0}, status.ascii_mode={1} (expected 1/0)" -f $disable, $afterDisable) -ForegroundColor Red
    $failures++
}

# ── 9. 中英文切换键：模拟 TIP 发来的 Shift 按下 / 抬起 ─────────────────────
#
# 线上格式与 TIP 发的一模一样：WEASEL_IPC_PROCESS_KEY_EVENT 的 wParam 就是
# weasel::KeyEvent 那 4 字节 {keycode:16, mask:16}（小端 ⇒ keycode 在低位、
# mask 在高位）。常量取自 vendor 的 include/KeyEvent.h：
#   ibus::Shift_L     = 0xFFE1
#   ibus::SHIFT_MASK  = 1 << 0
#   ibus::RELEASE_MASK= 1 << 14
# 「按下」= keycode | (SHIFT_MASK << 16)，「抬起」= keycode | (RELEASE_MASK << 16)。
#
# 判定规则：修饰键要「按下 → 抬手，期间没有别的键」才算一次单击，
# 所以这里必须把两半都发过去；Shift+A 那种「拿它当修饰键」的用法不会切。
$VKEY_SHIFT_L = 0xFFE1
$MASK_SHIFT   = 1
$MASK_RELEASE = 1 -shl 14
$shiftDown = [uint32]($VKEY_SHIFT_L -bor ($MASK_SHIFT -shl 16))
$shiftUp   = [uint32]($VKEY_SHIFT_L -bor ($MASK_RELEASE -shl 16))

# 先回到中文态。
Send-TrayCommand -MenuId $ID_WEASELTRAY_DISABLE_ASCII -SessionId $sessionId | Out-Null
$base = Wait-AsciiMode -Session $sessionId -Expected 0
Send-Ipc -Msg $WEASEL_IPC_PROCESS_KEY_EVENT -wParam $shiftDown -lParam $sessionId | Out-Null
Send-Ipc -Msg $WEASEL_IPC_PROCESS_KEY_EVENT -wParam $shiftUp   -lParam $sessionId | Out-Null
$afterClick = Wait-AsciiMode -Session $sessionId -Expected 1
if ($base -eq 0 -and $afterClick -eq 1) {
    Write-Host "PASS: a clean Shift click switched the engine to English (ascii_mode 0 -> 1)" -ForegroundColor Green
} else {
    Write-Host ("FAIL: a clean Shift click did not switch the ascii mode (base={0}, after={1}, expected 0/1)" -f $base, $afterClick) -ForegroundColor Red
    $failures++
}

# 再点一次 ⇒ 切回中文。
Send-Ipc -Msg $WEASEL_IPC_PROCESS_KEY_EVENT -wParam $shiftDown -lParam $sessionId | Out-Null
Send-Ipc -Msg $WEASEL_IPC_PROCESS_KEY_EVENT -wParam $shiftUp   -lParam $sessionId | Out-Null
$afterSecond = Wait-AsciiMode -Session $sessionId -Expected 0
if ($afterSecond -eq 0) {
    Write-Host "PASS: a second Shift click switched back to Chinese (ascii_mode 1 -> 0)" -ForegroundColor Green
} else {
    Write-Host ("FAIL: a second Shift click did not switch back (ascii_mode={0}, expected 0)" -f $afterSecond) -ForegroundColor Red
    $failures++
}

# Shift+A：切换键被当修饰键用了 ⇒ 不该切。
$beforeCombo = Wait-AsciiMode -Session $sessionId -Expected 0
Send-Ipc -Msg $WEASEL_IPC_PROCESS_KEY_EVENT -wParam $shiftDown -lParam $sessionId | Out-Null
Send-Ipc -Msg $WEASEL_IPC_PROCESS_KEY_EVENT -wParam $VKEY_A -lParam $sessionId | Out-Null
Send-Ipc -Msg $WEASEL_IPC_PROCESS_KEY_EVENT -wParam ([uint32]($VKEY_A -bor ($MASK_RELEASE -shl 16))) -lParam $sessionId | Out-Null
Send-Ipc -Msg $WEASEL_IPC_PROCESS_KEY_EVENT -wParam $shiftUp   -lParam $sessionId | Out-Null
$afterCombo = Wait-AsciiMode -Session $sessionId -Expected 0
if ($beforeCombo -eq $afterCombo) {
    Write-Host "PASS: Shift+A did NOT switch the ascii mode (it stays a case modifier)" -ForegroundColor Green
} else {
    Write-Host ("FAIL: Shift+A wrongly switched the ascii mode ({0} -> {1})" -f $beforeCombo, $afterCombo) -ForegroundColor Red
    $failures++
}

# ── 9b. 组合态中按 Shift：中文组合态必须了结，英文才真的生效 ─────────────
#
# 这一条才是「Shift 切换中英文」的真正回归点（实测 2026-11-22）。
#
# 只切 ascii_mode 是不够的：librime 的 ascii_composer 在**组合态还在**的时候，
# 会把后续按键 ctx->PushInput 进那个中文组合里，而不是放行给应用
# （src/rime/gear/ascii_composer.cc 的 ProcessKeyEvent）。于是模式明明已经是
# 英文，屏幕却还是一屏中文 —— 看起来就是「按 Shift 切不动中英」。
#
# 判据不能只看 ascii_mode（它在两种情况下都是 1），必须看**组合态**：
#   1) 先打出「nihao」，确认引擎真的进了中文组合态（composing=1）；
#   2) 单击 Shift；
#   3) 再按一个字母 —— 若切换生效，这个字母必须**直接进宿主**，
#      组合态既不该还在（composing=0），也不该被这个字母撑大（preedit 为空）。
Send-Ipc -Msg $WEASEL_IPC_CLEAR_COMPOSITION -wParam 0 -lParam $sessionId | Out-Null
Send-TrayCommand -MenuId $ID_WEASELTRAY_DISABLE_ASCII -SessionId $sessionId | Out-Null
Start-Sleep -Milliseconds 300

# 打出「nihao」；最后一条回包里就是组合态的证据。
$last = $null
foreach ($k in @(0x6E, 0x69, 0x68, 0x61, 0x6F)) {   # n i h a o
    $last = Send-Ipc -Msg $WEASEL_IPC_PROCESS_KEY_EVENT -wParam $k -lParam $sessionId
}
$composingBefore = Get-StatusFlag -Body $last.Body -Name "composing"
if ($composingBefore -ne 1) {
    Write-Host "FAIL: could not build a Chinese composition before the Shift click (composing=$composingBefore)" -ForegroundColor Red
    $failures++
} else {
    Write-Host "PASS: a Chinese composition is active (status.composing = 1)" -ForegroundColor Green
}

# 单击 Shift（按下 → 抬起，期间不按别的键）。
Send-Ipc -Msg $WEASEL_IPC_PROCESS_KEY_EVENT -wParam $shiftDown -lParam $sessionId | Out-Null
Send-Ipc -Msg $WEASEL_IPC_PROCESS_KEY_EVENT -wParam $shiftUp   -lParam $sessionId | Out-Null
Start-Sleep -Milliseconds 400

# 切换之后按一个字母：它必须直接进宿主，而不是被塞回中文组合。
$after = Send-Ipc -Msg $WEASEL_IPC_PROCESS_KEY_EVENT -wParam $VKEY_A -lParam $sessionId
$asciiAfter = Get-StatusFlag -Body $after.Body -Name "ascii_mode"
$composingAfter = Get-StatusFlag -Body $after.Body -Name "composing"
$preeditAfter = Get-Preedit -Body $after.Body
if ($asciiAfter -eq 1 -and $composingAfter -eq 0 -and [string]::IsNullOrEmpty($preeditAfter)) {
    Write-Host "PASS: the Shift click ended the Chinese composition and the next key went straight through" -ForegroundColor Green
} else {
    Write-Host ("FAIL: the Shift click left the Chinese composition alive (ascii_mode={0}, composing={1}, preedit='{2}')" -f $asciiAfter, $composingAfter, $preeditAfter) -ForegroundColor Red
    $failures++
}

# 收尾：切回中文、清组合态，别给后面的用例留状态。
Send-Ipc -Msg $WEASEL_IPC_PROCESS_KEY_EVENT -wParam $shiftDown -lParam $sessionId | Out-Null
Send-Ipc -Msg $WEASEL_IPC_PROCESS_KEY_EVENT -wParam $shiftUp   -lParam $sessionId | Out-Null
Send-Ipc -Msg $WEASEL_IPC_CLEAR_COMPOSITION -wParam 0 -lParam $sessionId | Out-Null
Send-TrayCommand -MenuId $ID_WEASELTRAY_DISABLE_ASCII -SessionId $sessionId | Out-Null

# ── 8. 我们自己注册的菜单命令也能命中 ─────────────────────────────────────
#
# SYNC（同步用户数据）走的是「自定义菜单命令 → 处理器」这条路，
# 正是语言栏菜单项走的同一条链路。
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

# ── 9. 维护之后，服务必须还活着（回归）──────────────────────────────────
#
# 「同步用户数据」会重建 Rime 的 SessionManager，旧的 Rime 会话随之消失，
# 但客户端手里还攥着旧 ipc_id。之后任何针对它的 Rime 调用都会打进一个
# 不存在的会话 —— librime 1.17 在这些接口里**不做空指针检查** ⇒
# rime.dll 抛 0xC0000005，整个服务进程直接崩掉（实测 2026-11-22：
# 同步完随后来一条普通消息，服务就没了）。
#
# 服务端现在在进 Rime 之前先问一句「这个会话还在吗」（find_session 只查表，
# 安全），死了就把这条消息丢掉。所以：同步之后会话失效是**预期**的，
# 但服务必须照常活着 —— 这一条就是那道回归。
Start-Sleep -Milliseconds 800   # 等同步真正跑完（它在服务端线程上）
$alive = Get-Process -Id $svc.Id -ErrorAction SilentlyContinue
if (-not $alive) {
    Write-Host "FAIL: the service DIED after 'Sync user data' (stale-session crash)" -ForegroundColor Red
    $failures++
} else {
    Write-Host "PASS: the service survived 'Sync user data'" -ForegroundColor Green
}

# 同步之后再发一条普通消息（正是当初把服务打崩的那条）。
$afterSync = Send-Ipc -Msg $WEASEL_IPC_CLEAR_COMPOSITION -wParam 0 -lParam $sessionId
$alive = Get-Process -Id $svc.Id -ErrorAction SilentlyContinue
if (-not $alive) {
    Write-Host "FAIL: the service DIED on a message carrying the stale session id" -ForegroundColor Red
    $failures++
} else {
    Write-Host "PASS: a message with a stale session id is dropped instead of crashing the service" -ForegroundColor Green
}

# ── 10. 收尾：清组合态、结束会话 ───────────────────────────────────────────
Send-Ipc -Msg $WEASEL_IPC_END_SESSION -wParam 0 -lParam $sessionId | Out-Null

$alive = Get-Process -Id $svc.Id -ErrorAction SilentlyContinue
if (-not $alive) {
    Write-Host "FAIL: the service DIED on END_SESSION" -ForegroundColor Red
    $failures++
}

Write-Host ""
if ($failures -eq 0) {
    Write-Host "Language bar IPC end-to-end test passed" -ForegroundColor Green
    exit 0
}
Write-Host "$failures check(s) failed" -ForegroundColor Red
exit 1
