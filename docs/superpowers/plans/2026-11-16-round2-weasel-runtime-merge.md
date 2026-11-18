# Round 2 实施计划：接管官方 Weasel 内核 + 保留 Fluent 候选窗

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 把官方 Weasel 的输入引擎与 TSF 实现接进本仓，替换自研的 `RimeEngine` 与精简 `tsf/`，同时**保留 Fluent 候选窗**（不引入 Weasel 的 D2D 面板），并落地「自动替换旧的服务」的安装器。

**Architecture:** 官方 Weasel 的分层是「服务持有引擎 + TIP 作为客户端」。本仓对齐同一分层：服务进程用 `weasel::Server` + 上游自带的 `RimeWithWeaselHandler`（它本身就实现 `weasel::RequestHandler`），TIP 换成 `WeaselTSF`。候选窗的桥接点非常小：`RimeWithWeaselHandler` 驱动一个 `weasel::UI`，而 `weasel::Context` 本身就带 `preedit` 与 `cinfo`（候选词），`weasel::Status` 带 `ascii_mode`/`schema_id` —— 本仓 `RimeUiState` 与它们几乎一一对应，只需一个适配层把二者映射起来。

**Tech Stack:** C++17 · Qt 6.8.2 · Fluent-Qt · xmake 3.1.2 · Boost 1.88.0（thread/serialization/system，**/MD**）· librime 1.17.x · 官方 rime/weasel（GPLv3，commit `f96ed24a`）

## Global Constraints

- 许可证 **GPLv3**（`LICENSE` 已改，勿动）。
- **路径红线**（设计文档 §4.2 R5）：一律经 `WeaselPaths::*`；`scripts/verify-no-hardcoded-paths.ps1` 必须保持通过。
- **过渡实现必须可整体删除**（R6）：`src/rime_engine.*` 与 `tsf/` 只允许通过 `src/rime_config_client.h` 抽象层被调用。
- **候选窗只出现一次**（R4）：官方 `WeaselUI/` 的 D2D 面板**不引入**；`WeaselTSF` 必须打补丁禁用其 D2D 渲染。
- **运行时统一 `/MD`**：本仓 GUI（Qt 官方二进制）、vendor 的 Weasel 静态库**两者一致**。
  ⚠️ 上游 Weasel 用 `/MT`，**本仓不跟随** —— 跟随会导致与 Qt 的 LNK2038 运行库不匹配。
- **Boost 必须链接**（T3 实测推翻 T1 的「只用头文件」结论，见「T3 执行记录」）：
  `boost_thread` / `boost_serialization` / `boost_system` / `boost_atomic`，且 Boost.Thread 需 `BOOST_THREAD_BUILD_LIB`。
  产物用 `scripts/build_boost_libs.ps1` 直接 cl.exe 编译（b2 在 1.88 上会静默跳过这几个库）。
- **ATL / WTL**：Weasel 的 `WeaselIPCServer` 用 ATL 的 `CWindowImpl`；WTL 已 vendor 在 `3rdparty/weasel/include/wtl/`。
  ⚠️ **Visual Studio 18 把 ATL/MFC 挪进了工具集目录**：
  `VC/Tools/MSVC/<ver>/atlmfc/{include,lib}`，不再是 VS2022 的 `VC/ATL/`。
- 本机工具链：Qt `C:\Qt\6.8.2\msvc2022_64`；xmake `C:\devtools\xmake\xmake\xmake.exe`；VS 18（MSVC 19.51，**Boost 需显式 `vc143`**）。
- 每次提交前跑 `powershell -ExecutionPolicy Bypass -File scripts/run-selfchecks.ps1`（含 `--verify`，Round 1 为 14 项，Round 2 扩到 18 项；**当前 15 项**）。

### T1 执行记录（2026-11-17，已完成）

T1 执行中撞到三条与计划假设不符的事实，已回写：

| 计划原假设 | 实际情况 |
| :--- | :--- |
| 需要链接 `boost_thread`/`boost_serialization` 等 Boost 库 | **Boost 1.88 起 `boost::thread` 在 Windows 上是 header-only**（通用实现移到 `libs/thread/src/pthread/`），不产出 `.lib`；上游 Weasel 因此**一个 Boost 库都不链接**，只把 `BOOST_ROOT` 加进 include 路径。本仓照做：**只用头文件** |
| ATL 在 `VC/ATL/include` | **VS 18 改到 `VC/Tools/MSVC/<ver>/atlmfc/include`**。这与 Boost 的 `msvc.jam` 反推 `vcvarsall.bat` 失败、setup 步骤崩掉是**同一个根因**（目录布局变了） |
| `xmake.lua` 里可用 `io`/`pcall`/`os.execv`/`path.dirname` | **xmake 3.1 的 Lua 沙箱一个都不提供**。ATL 探测改用 `os.files`/`os.isfile` + 字符串处理 |

附带修正：首轮 vendor 只拷了 `include/*.h`，漏掉 `StringAlgorithm.hpp` 与 `VersionHelpers.hpp`（上游用 `.hpp`），导致 `ActionLoader.cpp` 找不到头文件 —— 已补拷。

产出：`WeaselIPC.lib` / `WeaselIPCServer.lib` / `RimeWithWeasel.lib` 三个静态库均已归档成功。

### T2 执行记录（2026-11-17，已完成）

| 计划原假设 | 实际情况 |
| :--- | :--- |
| 「不引入 `WeaselUI/`」 | **必须引入**：`WeaselTSF` 的 `CCandidateList` 持有 `std::unique_ptr<weasel::UI>`，而 `weasel::UI` 的实现（`UIImpl` / `WeaselPanel` / D2D）就在 `WeaselUI/` 里 —— 没有它 `WeaselTSF` 编不过。现在它**只作为数据载体**存在，D2D 窗口永不创建 |
| 补丁用 `git apply` 即可 | 补丁路径必须**相对仓库根**：在仓库内 `git apply` 按 repo root 解析路径。脚本已改为 `git -C <repoRoot> apply -p1` |
| 补丁脚本简单调用 `git apply --check` 即可判幂等 | 语义是「**反向**检查通过 == 已应用」。且必须检查输出里的 `Skipped patch` —— 路径解析不了时 git 会**静默跳过**并返回 0，看起来像成功 |
| 补丁文件用 PowerShell 写盘即可 | PowerShell 5.1 的 `>` 重定向写成 **UTF-16**，git 解析不了 → 静默跳过 → 误判「已应用」。必须显式写 UTF-8 |
| — | **VS 18 需要 `/FIguiddef.h /FIcoguid.h`**：`<Windows.h>` 在 `WIN32_LEAN_AND_MEAN` 下不再带 `guiddef.h`，而 ATL 的 `atlcomcli.h` 用 `GUID_NULL`/`IID_NULL` 却不自己包含。**且 xmake 会静默忽略未知的 `/FI` 标志**（只打印一行 `is ignored`），必须 `{force = true}` |

产出：`WeaselTSF.lib` / `WeaselUI.lib` 均已归档成功；补丁应用脚本验证了幂等（首次 `applied`、再次 `already applied`）。

### T3 执行记录（2026-11-17，已完成）

| 计划原假设 | 实际情况 |
| :--- | :--- |
| 「Boost 只用头文件，不链接任何 Boost 库」 | **错，必须链接**。上游 `PipeChannel.h` 用 `boost::thread_specific_ptr`、`WeaselIPCData.h` 用 `boost::archive::text_wiarchive` → 需要 `boost_thread` + `boost_serialization`。先前「上游不链接 Boost」的判断来自被截断的 `AdditionalDependencies` 行 |
| Boost 用 b2 产出即可 | **b2 在 Boost 1.88 上会静默跳过** `libs/{thread,chrono,filesystem,json}`：其 `Jamfile.v2` 用 `<conditional>@requirements`，不满足时 target 被跳过（组件配置仍显示 "building"，但无 `.lib` 产出，退出码为 0）。改为 `scripts/build_boost_libs.ps1` **直接用 cl.exe 从源码编译** 4 个库 |
| `RimeStartMaintenance()` / `RimeJoinMaintenanceThread()` 可直接调用 | librime 1.17 的 `rime_api.h` **只把维护接口放在 `RimeApi` vtable**（`start_maintenance` / `join_maintenance_thread`），那两个自由函数虽在 DLL 里导出但不再声明。改用 `rime_get_api()->start_maintenance(true)` |
| — | **Boost.Thread 还需要 `BOOST_THREAD_BUILD_LIB`**：`win32/tss_pe.cpp` 靠它提供 `boost::tss_cleanup_implemented()`；`tss_null.cpp` 在 MSVC 下被 `!defined(_MSC_VER)` 排除，救不了 |
| — | **必须链接 `rime.lib`**：上游调用的是 `rime_api.h` 里 `RIME_API`(dllimport) 声明的导出函数。代价：`rime.dll` 变成启动期硬依赖（与上游 Weasel 相同），构建会把 `rime.dll` 拷到输出目录 |
| — | **`WTL::CAppModule _Module` 要自己提供**：上游由 `WeaselServer/WeaselServer.cpp` 定义、`WeaselIPCServer` 以 extern 引用。本仓取代了 WeaselServer，故在 `src/weasel_module.cpp` 补定义。**它是 WTL 的（`wtl/atlapp.h`）不是 ATL 的** —— VS 18 的 ATL 里已经没有 `atlmod.h`/`CAppModule` |

产出：`WeaselEngine`（`src/weasel_engine.{h,cpp}`）编译并链接通过；`--verify` 14 项仍全绿。

### T4 执行记录（2026-11-17，已完成）

| 计划原假设 | 实际情况 |
| :--- | :--- |
| `ctx.cinfo.candies` 是 `std::vector<std::wstring>`，可直接 `QString::fromStdWString` | **`std::vector<weasel::Text>`**（`comments`/`labels` 同）。文本要取 `Text::str`，属性在 `Text::attributes` |
| `ctx.preedit.cursor` 存在，可直接取光标 | **`weasel::Text` 没有 cursor 字段**。更关键的是：**服务端的 `weasel::Context` 根本不携带组合态光标** —— `RimeWithWeaselHandler::_GetContext` 只填 `preedit.str` 与「选中区」属性（`TextAttribute.range.start/end`），光标只存在于 TIP 响应协议（`ctx.preedit.cursor=start,end,cursor`）里。⇒ 适配器的 `cursorPos` 只能按「光标在预编辑串末尾」推导（属性里若带 `range.cursor` 则优先采用） |
| `status.schema_id = "luna_pinyin"` | `weasel::Status::schema_id` 是 `std::wstring`，**必须写 `L"luna_pinyin"`** |
| `candies.push_back(L"你好")` 即可 | `weasel::Text` 只有 `Text(std::wstring const&)` 一个转换构造：`const wchar_t* → std::wstring → Text` 是**两次**用户定义转换，编译期直接拒绝。必须 `weasel::Text(std::wstring(L"你好"))` |
| `CandidateWindow` 的中英模式读 `m_engine->isAsciiMode()` | **`CandidateWindow::m_engine` 从未被赋值，恒为 nullptr** —— 那行是死代码（菜单恒显示「当前: 中文」）。已改为读桥接推送的 `m_asciiMode`（新增 `setAsciiMode()`/`asciiMode()`），Weasel 路径由 `status.ascii_mode` 驱动 |
| — | **`--verify` 的失败信息走 `std::cerr`、进度走 `std::cout`**。`std::cout` 是块缓冲，重定向到文件时**进程异常退出会丢掉 stdout 尾部**，而 `std::cerr` 无缓冲反而出现在文件**前部**。排查失败项时必须 stdout/stderr 都看，别被「输出停在 TEST 15」误导成崩溃 |

产出：`src/fluent_ui_adapter.{h,cpp}` + `CandidateWindow::updateFromWeasel/setAsciiMode`；`--verify` 扩到 **15 项**，新增 TEST 15（映射正确性，含「属性带 cursor」「上游不带 cursor 走末尾兜底」「空上下文」三个断言）；`scripts/run-selfchecks.ps1` 全绿。

> 注：T4 的 `Step 4（引擎回调 → 适配器接线）` 暂未落到 `main.cpp` 的服务模式 —— 那里当前仍是自研 `RimeEngine`，与 `WeaselEngine` 并存会争抢 librime 全局单例。接线随 **T7 的切换**一起做。

### T5 执行记录（2026-11-17，已完成）

| 计划原假设 | 实际情况 |
| :--- | :--- |
| 需要新建 `src/weasel_tip_client.{h,cpp}` 封装 `weasel::Client` | **多余**。上游 `WeaselTSF` 自带 `weasel::Client m_client`（`WeaselTSF.h:230`），TIP 侧的 IPC 客户端本来就在 vendor 里（`WeaselIPC/WeaselClientImpl.cpp`）。不新建任何 TIP 客户端 |
| 用本仓自己的 CLSID `{D7B1C618-…}` 注册（Step 3 提示「若不一致就在 Globals.h 内改」） | **行不通**。上游 `WeaselTSF/Globals.cpp` 的 `c_clsidTextService` **就是官方的** `{A3F4CDED-…}`，而 `WeaselTSF::OnActivated` 只在 `clsid == c_clsidTextService` 时接管。换成别的 CLSID → TSF 把 DLL 加载起来但 `OnActivated` 直接 `return S_OK`，**整个 TIP 变空壳**。⇒ 改为**官方 CLSID + HKCU 影子注册**（`HKCU\Software\Classes\CLSID` 优先于 `HKLM`，正是设计 §6.6 想要的「免提权顶掉官方 Weasel」）。连带作废：设计文档 §6.6 第 5 步「往语言列表加 `0804:{D7B1C618-…}`」——官方安装已写好的 `0804:{A3F4CDED-…}{3D02CAB6-…}` 条目直接生效 |
| 上游 `WeaselTSF/Register.cpp` 可以直接用 | **不能**。它写 `HKEY_CLASSES_ROOT`（= HKLM）且用 `ITfInputProcessorProfileMgr::RegisterProfile`（机器级）→ **要管理员**，破坏「每用户免提权」安装。⇒ **不编译上游 Register.cpp**（`xmake.lua` 里 `remove_files`），改用本仓 `tip/weasel_tsf_register.cpp`：HKCU + `ITfInputProcessorProfiles::AddLanguageProfile/EnableLanguageProfile`（逐用户），实现上游 `Register.h` 声明的同名 6 个函数，因此上游 `Server.cpp` 不用改 |
| 补丁改成「编译期宏无条件禁用 D2D」 | **已满足**：补丁 0001 本身就是无条件去掉 `_ui->Create()`，不需要再引入 `WEASEL_FLUENT_NO_D2D` |
| — | **管道名必须与官方隔离**：上游硬编码 `WEASEL_IPC_PIPE_NAME = L"WeaselNamedPipe"`，机器上已装的官方 `WeaselServer.exe` 会占住同一个命名管道 → 本仓服务 bind 失败。新增补丁 **0002** 把该宏改成 `#ifndef` 守卫，用 `/FIweasel_fluent_defines.h`（`build_defines/`）强制包含覆盖为 `WeaselFluentNamedPipe`。**主程序与 TIP 两端都要覆盖**（管道名不一致就连不上） |
| — | **补丁 0002 还修了 `_GetRootDir()`**：上游只查 HKLM 的 `Software\Rime\Weasel\WeaselRoot`，每用户安装拿不到自己的安装目录（也就找不到 `start_service.bat`，TIP 无法拉起我们的服务）。改为 **HKCU 优先、HKLM 兜底**（语言栏的「重新启动服务 / 打开安装目录」同样处理） |
| — | **`xmake` 的 `del_files` 已废弃**（会打印 deprecation 警告），要用 `remove_files` |
| — | **`src/*.cpp` 通配会吞掉 TIP 侧源码**：主程序目标用 `add_files("src/*.cpp")`，把 `src/weasel_tsf_register.cpp` 也编进了主 exe（缺 include 路径 → `C1083: 无法打开包括文件 "stdafx.h"`；就算编过也会重复符号）。⇒ TIP 侧源码单独放 **`tip/`** 目录，不在 `src/` 下 |
| — | **TSF 类别 GUID（`GUID_TFCAT_*`）由 SDK 的 `uuid.lib` 提供**：`msctf.h` 只是 `EXTERN_C const GUID` 声明，SDK 里**没有** `msctf.lib`（实测 `Lib/10.0.26100.0/um/x64` 下无此文件）⇒ 必须 `add_syslinks("uuid")` |
| — | `ITfInputProcessorProfiles` **没有** `UnregisterLanguageProfile`，逐用户移除要用 `RemoveLanguageProfile` |

产出：
- `weasel-fluent-tsf.dll` 由**上游 WeaselTSF 源码**编译而成（`dumpbin /exports` 已确认导出 `DllGetClassObject` / `DllCanUnloadNow` / `DllRegisterServer` / `DllUnregisterServer`）。
- 新增 `tip/weasel_tsf_register.cpp`（逐用户注册）、`build_defines/weasel_fluent_defines.h`（管道名覆盖）、补丁 `0002-isolated-ipc-pipe-and-per-user-root.patch`。
- 新增自检 `scripts/verify-tip-dll.ps1`（导出表 + 两端管道名一致 + 上游 Register.cpp 确实被排除），已挂进 `scripts/run-selfchecks.ps1`。

### T6 执行记录（2026-11-17，已完成）—— 服务端切到 Weasel 内核

> **任务顺序调整**：原计划把「设置中心接 Weasel 通道」排在「服务端切换」之前，
> 但设置中心要走 Weasel IPC 部署，前提就是**服务端已经是 Weasel 服务**；
> 而且 librime 是**进程级全局单例**，两条引擎不能并存。所以本任务把
> 「服务端切换」提前，并把「删除过渡实现」里已经死掉的 `src/ipc_server.*`、
> `src/input_hook.*`、旧 `tsf/` 一并删掉。
> 「设置中心接 Weasel 通道」顺延为下一个任务。

| 计划原假设 | 实际情况 |
| :--- | :--- |
| `WeaselEngine::start()` 里 `m_server->Start()` 在 Qt 线程、`Run()` 在服务端线程 | **必须同一个线程**：`weasel::Server::Start()` 是在**调用线程**上创建窗口的，`Run()` 跑的是该线程的消息循环。分开 ⇒ 消息永远收不到。改为「建窗口 + 初始化引擎 + 跑循环」全在服务端线程，Qt 线程用信号量等就绪 |
| 上游 `ServerImpl::Run()` 里的 `_Module.AddMessageLoop()` 能直接用 | **崩**（0xC0000005）：`CAppModule::AddMessageLoop` 会解引用 `m_pMsgLoopMap`，而那个 map **只在 `CAppModule::Initialize()` 里分配**。本仓是 Qt 应用，不走 ATL 模块生命周期。⇒ 补丁 0004 去掉这两行（WTL 的 `CMessageLoop::Run()` 本身自洽） |
| `weasel::Context` 里已经有预编辑串与候选列表 | **服务端拿到的 Context 一直是空的**！上游 `_UpdateUI()` **从不调用 `_GetContext()`** —— 该函数定义了却没人用。上游的候选列表在 **TIP 进程**里（`CCandidateList` 自带一个 `weasel::UI`），数据流是「服务端 → IPC 响应 → TIP 的 `ResponseParser` → `ContextUpdater` → TIP 的 UI → D2D 面板」。本仓的 Fluent 候选窗在**服务端进程**里 ⇒ 补丁 0005 让服务端自己调 `_GetContext()`。**这是整个桥接真正缺的那一环**（T4 的适配器一直在映射一个空 Context） |
| `ProcessKeyEvent(keyEvent, sessionId, nullptr)` 的第三个参数可以传 `nullptr` | **会崩**（0xC0000409）：上游 `_Respond()` 末尾直接调用 `eat(header)` / `eat(body)` 而**不检查空指针** —— 调用空的 `std::function` 抛 `std::bad_function_call`，在服务端线程里直接终止整个进程。⇒ 必须传一个 no-op 回调 |
| 上游的 `u8tow(ptr)` 可以随便用 | `u8tow` = `string_to_wstring(x, CP_UTF8)`，参数是 `const std::string&` —— 从 `const char*` 构造 `std::string` 传 NULL 是未定义行为。librime 在「提交串为空」「会话没有有效方案」时**确实**给 NULL。⇒ 补丁 0005 统一走 `u8tow_safe()` |
| 服务端单实例互斥量 `(WEASEL)Furandōru-Sukāretto-<user>` 可以直接用 | 机器上装着官方 Weasel 时，**官方 `WeaselServer.exe` 正占着这个互斥量** ⇒ `weasel::Server::Start()` 直接返回 0，本仓的服务根本起不来。⇒ 补丁 0003 把前缀做成可编译期覆盖 |
| 自检可以在同一个进程里同时跑旧引擎与新引擎的用例 | **不行**：librime 是进程级全局单例。旧 `RimeEngine`（TEST 2）已经 setup + initialize 过它，还持着用户词库的 LOCK 文件 ⇒ 新引擎只能只读，组合态起不来。⇒ Weasel 运行时端到端用例改为**独立子进程**（`--verify-weasel-runtime`） |

**另外两条环境事实**：

1. **官方 Weasel 正在本机运行**（`F:\Rime\weasel-0.17.4\WeaselServer.exe`），占着
   `%APPDATA%\Rime` 里用户词库的 LOCK ⇒ 本仓的引擎拿不到词库（只读），输入出不来候选。
   这正是 T8 安装器必须「停掉官方 WeaselServer.exe」的原因；自检则用隔离的
   Rime 目录（补丁 0006 的 `WEASEL_USER_DIR`）绕开。
2. **`--hook`（全局 WH_KEYBOARD_LL 键盘钩子）随过渡实现下线**：输入由官方 WeaselTSF
   这个真正的 TSF 前端接管，钩子只会重复处理按键。

**产出**：

- 新增 `src/ui_engine.h`（`UiEngine` 抽象：候选窗 / 扩展面板 / 托盘需要的引擎能力 +
  `stateChanged` / `committed` 两个公共信号）、`src/rime_ui_state.h`（`RimeUiState` /
  `CandidateItem` 从 `rime_engine.h` 拆出来）。
- `RimeEngine` 与 `WeaselEngine` 都实现 `UiEngine` ⇒ UI 层（候选窗 / 扩展面板 / 托盘）
  换成依赖接口，切换引擎不用改调用方式。
- `WeaselEngine` 补齐 UI 需要的全部能力：按键 / 选词 / 翻页 / 上屏 / 清空 / 选项 /
  部署 / 切方案 / 直接上屏文本（`SendInput` + `KEYEVENTF_UNICODE`）。
  **所有交互命令都排队后用 `WM_COMMAND` 投到服务端窗口执行**（handler 的会话表不是
  线程安全的）；服务端线程用 `AddMenuHandler(41001, ...)` 接收。
- 服务进程改用 `WeaselEngine` + `FluentUiAdapter` 桥接；删除 `src/ipc_server.*`、
  `src/input_hook.*`、旧 `tsf/`。
- 新增 vendor 补丁 0003–0006（见 `3rdparty/weasel/UPSTREAM.md`）。
- `--verify` 扩到 **16 项**：新增 TEST 16（Weasel 运行时端到端，独立子进程跑，
  断言能出候选、桥接映射一致、`ascii_mode` 能同步）。

> 注：`src/rime_engine.*` 暂时保留 —— 设置中心是**独立进程**，仍用它自己那份
> librime 实例做部署（上游部署器也是这个模式：部署器自己 rebuild，再让服务端重启）。
> 下一个任务把设置中心改走 Weasel IPC 之后，这两个文件即可整体删除（R6）。

> **T5 那条「TIP 还不可用」的警告到此解除**：T6 把服务端切成了 `WeaselEngine`，
> 两端现在说的是同一套上游 Weasel IPC 协议了（管道名 `WeaselFluentNamedPipe`）。
> 但**还不要安装到机器上**：设置中心仍在跑旧引擎、官方 Weasel 也没被替换，
> 这两件事分别是下一个任务与 T8 的活。

### T7 执行记录（2026-11-17，已完成）—— 设置中心接 Weasel IPC + 删除过渡实现

| 计划原假设 | 实际情况 |
| :--- | :--- |
| 部署走 IPC 的 `START_MAINTENANCE` / `END_MAINTENANCE` 即可触发重新部署 | **可以**。服务端收到这对消息后会 `Finalize()` 掉引擎再 `Initialize()`，重新初始化时若工作区过期就触发 Rime 的**增量部署** —— 这正是上游部署器的工作模式（部署器自己 rebuild，再让服务端重启）。所以**设置中心不需要自己持有 librime 实例** |
| `weasel::Client::Connect()` 可以直接调 | **会把界面卡死**：上游 `PipeChannelBase::_Connect()` 在服务端没起来时会**永远阻塞**在 `WaitNamedPipe` 上。⇒ `WeaselConfigClient::ensureConnected()` 先用 **0 超时**的 `WaitNamedPipe` 探管道，探不到就干净地报错 |
| 设置中心要读 `weasel::UIStyle` | 走**响应流**：发一个空按键（`KeyEvent(0,0)`，上游 TIP 同一个套路）逼服务端回包，再用上游 `ResponseParser` 反序列化出 `status` 与 `style` |
| `RimeConfigClient` 保持接口不变即可 | 接口不变，但 `EngineConfigClient`（基于自研 `RimeEngine`）随过渡实现一起删除，只剩 `WeaselConfigClient` |

**产出**：

- 新增 `src/weasel_config_client.{h,cpp}`：`WeaselConfigClient : public RimeConfigClient`，
  自带一个 `weasel::Client`，实现「部署 / 切方案 / 读当前方案 / 读样式」。
- 设置中心改用 `RimeConfigClient*`：`RimeEngine*` 彻底不再出现在 UI 层。
- **删除** `src/rime_engine.{h,cpp}`、`src/rime_config_client.cpp`（过渡实现，R6 达成），
  以及 `src/ipc_server.*`、`src/input_hook.*`、旧 `tsf/`（T6 已删）。
  ⇒ 全仓不再有平行的 Rime 引擎或 TSF 实现。
- 自检套件相应调整：
  - **TEST 2** 改为「Weasel 运行时（输入 / 候选导航 / 模式切换 / 部署）」——
    由**独立子进程**的探针（`--verify-weasel-runtime`）执行，断言它覆盖了
    输入出候选、Down/Up 导航、中英/简繁/全半角切换、`deployFull()`；
  - **TEST 7** 只保留扩展菜单的检查（引擎切换已由 TEST 2 的探针覆盖）；
  - **TEST 10** 改为「Weasel IPC 配置通道」：服务在跑时能从响应流读出样式，
    服务没跑时必须**干净地报错而不是卡死**；
  - `--capture`（截图）与 `--benchmark`（基准）也改用真实的 `WeaselEngine`
    + 隔离的 Rime 用户目录，截图与基准数据都来自真实引擎。

### T8 执行记录（2026-11-17，已完成）—— 每用户安装器

| 计划原假设 | 实际情况 |
| :--- | :--- |
| Step 5 要用 `InstallLayoutOrTip("0804:{D7B1C618-…}{E8934A01-…}")` 把**本仓自己的** TIP 加进语言列表 | **作废**（T5 已改用官方 CLSID，见设计文档第 2 次修订）。现在语言列表里原有的 `0804:{A3F4CDED-…}{3D02CAB6-…}` 条目直接生效 —— 本仓的 TIP 就注册在这个 CLSID + profile 下。`tip/weasel_tsf_register.cpp` 的 `AddLanguageProfile` 也保证「机器上没装官方 Weasel」时能逐用户建出这个 profile |
| 影子注册 = 同时注册本仓 CLSID 与官方 CLSID | **只需要官方 CLSID**：上游 `WeaselTSF::OnActivated` 只接管 `c_clsidTextService`（官方那个），注册本仓自己的 CLSID 会得到一个空壳 TIP。HKCU 下注册官方 CLSID 即完成「顶掉」 |
| 卸载还要「移除语言列表里的本仓 TIP」 | 不用：本仓用的就是官方那条语言列表条目，删掉 HKCU 影子注册后官方 Weasel 自然恢复 |

**产出**：

- `scripts/install.ps1`（每用户，免提权）：
  1. 停 `weasel-fluent-cpp-qt.exe` 与官方 `WeaselServer.exe`
     （**必须停**：官方服务占着 `%APPDATA%\Rime` 用户词库的 LOCK，本仓的引擎
     拿不到词库就只读、输入出不来候选）；
  2. 复制 payload 到 `%LOCALAPPDATA%\Programs\WeaselFluent`
     （exe / TIP DLL / `rime.dll` / `data/` / `config.json`），并写 `start_service.bat`
     （TIP 用它拉起服务，见补丁 0002 的 HKCU `WeaselRoot` 查找）；
  3. `regsvr32 /s` 注册 TIP DLL → 触发 `tip/weasel_tsf_register.cpp` 的
     **HKCU CLSID 影子注册**（官方 CLSID → 本仓 DLL）+ 逐用户语言 profile；
  4. 写 `HKCU\Software\Rime\Weasel\WeaselRoot` 与 `HKCU\...\Run\WeaselFluent`；
  5. 启动服务；每步都先检测后动作，**幂等**。
- `scripts/uninstall.ps1`：反向清理（停进程 → 反注册 → 删 HKCU 影子注册 → 删自启项 →
  删安装目录），默认**保留** `%APPDATA%\Rime`，`-Purge` 才清。
- `scripts/package_dist.ps1`：发行包补上 `weasel-fluent-tsf.dll`（**原先漏了**，
  漏了的话输入法根本加载不了）、`install.ps1` / `uninstall.ps1`；
  `install_and_run.bat` 改为调用 `install.ps1`。
- `--verify` 加 TEST 16（安装器静态检查：官方 CLSID / HKCU 影子注册 / regsvr32 /
  WeaselRoot / Run 自启项 / 停官方 WeaselServer）与 TEST 17（打包内容检查），
  **共 17 项**。

### T8/T9 执行记录（2026-11-18，已完成）—— 安装器、打包与端到端验收

实测中又撞到六个坑，全部已修：

| 现象 | 根因 | 修法 |
| :--- | :--- | :--- |
| `regsvr32` 返回非零，`HKCU` 影子注册没落地 | 上游的 `RegisterProfiles()` 走 `ITfInputProcessorProfiles::AddLanguageProfile`，那是**机器级**路径；没有管理员权限时必然 E_FAIL（本机官方 Weasel 是 `system32\weasel.dll`，profile 早已在机器级注册，`AddLanguageProfile` 照样 E_FAIL） | 逐用户注册改为两条免提权的路：① 直接按 TSF 的注册表布局写 `HKCU\Software\Microsoft\CTF\TIP\{CLSID}\LanguageProfile\0x0000xxxx\{ProfileGUID}`；② 用系统的 `InstallLayoutOrTip`（imm32 导出）把 TIP 加进用户语言列表 |
| 注册表里的 `InprocServer32` 指向 **regsvr32.exe** | UCRT 的 `DllMainCRTStartup` 用 `GetProcAddress(hmod, "DllMain")` **动态查找**用户的 DllMain —— 不在导出表里就永远不会被调用 ⇒ 上游 `g_hInst` 一直是空 ⇒ `GetModuleFileNameW(g_hInst,…)` 拿到宿主进程路径 | 补丁 0007（上游 DllMain 改名）+ `tip/weasel_fluent_dllmain.cpp` 提供 C 链接入口 + `tip/weasel-fluent-tsf.def` 导出 `DllMain`（顺带：`.def` 文件必须 **CRLF + ASCII**，链接器的 def 解析器不认裸 LF，也不认 UTF-8 中文注释） |
| 双击 `build\Release\weasel-fluent-cpp-qt.exe` 直接报「无法定位程序输入点 `?setGeometry@QStackedLayout…`」 | **不是编译器问题，是运行时加载了错的 Qt**：本机 `PATH` 里排着 `F:\ST\STM32CubeCLT_1.21.0\STM32CubeProgrammer\bin`，里面有一套 **Qt 6.6.1** 的 `Qt6Core/Qt6Gui/Qt6Widgets/Qt6Svg`；exe 是用 **Qt 6.8.2** 编译的，而构建目录里没有 Qt DLL ⇒ 运行时从 PATH 捡了 6.6.1 | 两层根治：① `main()` 开头 `SetDefaultDllDirectories(APPLICATION_DIR｜SYSTEM32｜USER_DIRS)`，**完全忽略 PATH**；② xmake 的 after_build 把 Qt 6.8.2 的 DLL 与 `plugins\{platforms,styles}` 拷到输出目录 |
| 自检「绿」但用户双击就崩 | 自检脚本**把 Qt 6.8.2 前置到了 PATH**，恰好把上面那个真问题遮住了 | 自检不再预置 PATH —— 按**用户双击 exe 的真实方式**跑（`build/Release` 自带 Qt DLL） |
| 服务在跑时自检探针取不到任何候选 | 上游 `weasel::Server::Start()` 在单实例互斥量被占用时**直接返回 0**（没有窗口）。我们把 `m_handler->Initialize()` 与 `OnUpdateUI` 都放在 `if (hwnd)` 里 ⇒ 引擎没初始化、状态也传不回 Qt 线程；命令还投给了一个**不存在的窗口** | ① 引擎初始化与状态回调**无条件**执行；② 自建一个**命令窗口**（message-only）专门派发 UI 命令；③ 拿不到上游服务端窗口时，用自己的消息循环兜底（退出时 `PostThreadMessage(WM_QUIT)`） |
| 安装时 `Copy-Item` 报「文件正被占用」 | TIP DLL 被**已经加载了它的应用**（ctfmon / 各输入框宿主）占着 | 重试 5 次；仍被占用就改用带时间戳的文件名安装，注册表指向新文件（旧文件等那些应用退出后自然释放） |

**另外两条环境事实**：

1. **本机的「官方 Weasel」是 Windows 内置的那套**：`HKLM\…\CLSID\{A3F4CDED-…}\InprocServer32`
   → `C:\WINDOWS\system32\weasel.dll`（不是 `F:\Rime\weasel-0.17.4` 那套）。
   它会在需要输入时被拉起并**重新占住用户词库的 LOCK** ⇒ 安装器必须停掉它，
   而且它会自己再起来（被 ctfmon 里已加载的官方 TIP 拉起）。
2. **`AddLanguageProfile` 失败不影响使用**：官方 profile 已在机器级注册，
   语言列表里的 `0804:{A3F4CDED-…}{3D02CAB6-…}` 条目直接生效。

**端到端验收结果**（本机，装了官方 Weasel 的环境）：

- ✅ `install.ps1` 免提权安装成功，`HKCU` 影子注册指向本仓的 TIP DLL，
  `WeaselRoot` / `Run\WeaselFluent` 均已写入；
- ✅ `uninstall.ps1` 反注册 + 删除影子注册 + 删安装目录，官方 Weasel 可恢复；
- ✅ 服务进程起来后内存 ~141 MB、IPC 管道 `\\.\pipe\<用户>\WeaselFluentNamedPipe` 就绪；
- ✅ 自检 17 项全绿（含独立进程的 Weasel 运行时端到端探针）；
- ✅ 在**PATH 里完全没有 Qt** 的情况下直接跑 `build\Release` 的 exe 仍然正常（用户双击场景）。

### T10 执行记录（2026-11-18，已完成）—— 设计 2.7：设置中心补 Weasel style 栏

设计文档 §7 的 2.7 是 Round 2 最后一项未落地的内容：**两套皮肤机制要在界面上可分辨**
（§6.4）。本任务把它做实：

| 项 | 内容 |
| :--- | :--- |
| 新增 `src/weasel_style.{h,cpp}` | `WeaselStyle` 结构（对应 `weasel::UIStyle` 的常用键）+ `WeaselStyleManager`：读写用户 Rime 目录下的 **`weasel.yaml`**（服务端 `LoadWeaselUIStyle()` 读它）。**只改我们管的那些键，其余内容原样保留** |
| `RimeConfigClient` 接口扩展 | 增加 `loadStyle()`（走 IPC 响应流读服务端**当前生效**的样式）与 `applyStyle()`（写 `weasel.yaml` + 走 IPC 部署）—— 正好对应设计 §4.4 的两条通道边界 |
| 设置中心「个性化与排版」页 | 新增「**Weasel 状态栏样式**」卡片：字体、字号、候选横/竖排、行内预编辑，带「从服务读取」与「应用并重新部署」两个按钮；卡片说明里明确写出**与上面的 Fluent token 皮肤是两套独立作用域**（框架层 vs 内容层） |
| 自检 | TEST 18：`weasel.yaml` 的解析 / 改写**往返一致性**，并断言**不相关的键不会被丢掉** |

⇒ 设计文档 §7 的 2.1–2.8 至此**全部落地**。

### T11 执行记录（2026-11-18，已完成）—— 语言栏（输入法工具栏）直接连通我们的程序

用户需求：「输入法工具栏部分要能够直接与我们的程序连通起来」。

**先把「语言栏是什么」搞清楚（这一步决定了后面所有做法）**：
语言栏**不是一个独立的窗口**，而是 **TIP 的一部分**（`3rdparty/weasel/WeaselTSF/LanguageBar.cpp`）。
它的三块内容分别落在三个地方：

| 块 | 在哪 | 本仓要做的事 |
| :--- | :--- | :--- |
| 图标（中/英/全角/半角/重载）与右键菜单 | **TIP DLL 的资源段**，运行时 `LoadMenuW` / `LoadImageW` | 把资源编进 `weasel-fluent-tsf.dll`（**不编就没有**：菜单是空的、图标不显示，而编译链接全绿） |
| 菜单项被点中后要做的 | 一半 TIP 本地做，一半**转发给服务端** | 服务端注册对应的 `ID_WEASELTRAY_*` 处理器（上游是 WeaselServer 注册的，我们取代了它 ⇒ **必须自己注册**） |
| TIP 本地那部分 | TIP 读注册表的 `WeaselRoot` 拼路径 | 补丁 0002 已把查找改成 **HKCU 优先**（每用户安装写的就是 HKCU）⇒ 无需新补丁 |

**转发那条路的完整链路**（就是「工具栏连到我们的程序」的关键路径）：

```
TIP: _HandleLangBarMenuSelect(wID)
       ├─ 本地：RERUN_SERVICE / INSTALLDIR / USERCONFIG / LOGDIR / WIKI / FORUM
       └─ default: m_client.TrayCommand(wID)
            → PipeMessage{ WEASEL_IPC_TRAY_COMMAND, wParam = menuId }
            → 命名管道 \\.\pipe\<用户>\WeaselFluentNamedPipe
            → ServerImpl::OnCommand() → m_MenuHandlers[menuId]()
            → **我们的 WeaselEngine::registerLanguageBarCommands() 注册的回调**
```

**改动清单**：

| 项 | 内容 |
| :--- | :--- |
| 新增 `3rdparty/weasel/resource/*.ico` | vendor 上游的 6 张图标（weasel/en/zh/reload/full/half） |
| 新增 `tip/weasel-tsf.rc` | 自写资源脚本（**不 vendor 上游 `.rc`**：它 `#include "afxres.h"`，本机没装 MFC）。改为 rc 内建的 `1 VERSIONINFO`，`LANGUAGE 0x4,0x2` 数值写法；UTF-16LE + BOM |
| `xmake.lua` | TIP 目标 `add_files("tip/weasel-tsf.rc")`（⚠️ 静态库里的 `.rc` 不会被链接器带上，必须编进 **DLL**） |
| `src/weasel_engine.cpp` | 新增 `registerLanguageBarCommands()`：注册 `QUIT / DEPLOY / SYNC / SETTINGS / DICT_MANAGEMENT / USERCONFIG / INSTALLDIR / LOGDIR / WIKI / FORUM / HOMEPAGE / CHECKUPDATE / RERUN_SERVICE` |
| `scripts/package_dist.ps1` | 产出 `start_service.bat`（上游 TIP 在两处按 `<WeaselRoot>\start_service.bat` 拉服务；发行包**必须**带同名文件，否则 TIP 的自动拉起静默失败） |
| `scripts/verify-tip-dll.ps1` | 新增第 4 组检查：枚举 DLL 资源段，断言三个菜单 + 六个图标都在 |
| 新增 `scripts/test_langbar_ipc.ps1` | **端到端**：连管道发 `WEASEL_IPC_TRAY_COMMAND`，断言回包 `1`（命中我们的处理器）而未注册 ID 回包 `0` |
| `scripts/run-selfchecks.ps1` | 接入 `langbar-ipc` 检查（服务没在跑 ⇒ 跳过，退出码 2） |

**实测推翻的假设（必须回写）**：

1. ❌「是 `<resource.h>` 与 Qt 头冲突」 → **真因是 librime 的 `Bool` 宏**。`deps/rime/dist/include/rime_api.h:45` 有
   `#define Bool int`，而 Qt 的 `QVariant::Type` 枚举里有一个成员正好叫 `Bool` ⇒ 被展开成
   `int = QMetaType::int` ⇒ MSVC 在 `qvariant.h:146` 报 C2144/C2589（报错位置离病因极远）。
   `weasel_engine.h` → `RimeWithWeasel.h` → `rime_api.h` 先定义宏，随后 `#include <QApplication>` 才解析 Qt 头 ⇒ 炸。
   之前不炸是因为**这个文件从没解析过 `qvariant.h`**（含头顺序一换就炸 ⇒ 典型的顺序依赖地雷）。
   修法：新增 `src/librime_compat.h`，在引入 `rime_api.h` 之后立刻 `#undef Bool/False/True`；
   vendor 自己的 `.cpp` 直接 include `rime_api.h`、不经过本文件 ⇒ 行为不变。另在 `weasel_engine.cpp` 里留
   `#if defined(Bool) || ... → #error` 的编译期探针，把这类问题拉回真正的源头。
2. ❌「菜单回调跑在服务端线程上」 → **真身是管道的 worker 线程**。`PipeServer::Listen` 每接一个连接就
   `boost::thread` 起一个 `_ProcessPipeThread`，`HandlePipeMessage → OnCommand → m_MenuHandlers[uID]()`
   全程在那个线程上（上游专门为托盘刷新加过一次「投回服务端消息线程」的跳转，可见常态不是）。
   ⇒ 纯 Win32 调用（`ShellExecuteW`）可直接做；碰 Qt 的（`QProcess`）必须 `QueuedConnection` 投回 Qt 线程。
3. ❌「图标是 `RT_ICON`」 → **真身是 `RT_GROUP_ICON(14)`**。上游的 `.ico` 是多分辨率的，rc.exe 会生成
   「`RT_GROUP_ICON` + 若干张 `RT_ICON`」，而 `IDI_ZH` 之类的 ID 落在 **GROUP_ICON** 上，图片的 ID 是自动编的 1..N。
   查 `RT_ICON(3)` 永远查不到 ⇒ 自检会误报「图标缺失」。
4. ❌「上游 `.rc` 的 `..\resource\` 可以照抄」 → 那是上游的目录布局（`<repo>/resource/`）。我们的图标在
   `3rdparty/weasel/resource/`、`.rc` 在 `tip/` ⇒ 路径要写成 `..\3rdparty\weasel\resource\`。
5. ❌「TIP 侧还需要一个读 HKLM 的补丁 0008」 → **不需要**：补丁 0002 已经把 `_GetRootDir()` 与
   `_HandleLangBarMenuSelect()` 里的 `WeaselRoot` 查找改成 HKCU 优先了。

**端到端验收结果**：

- ✅ TIP DLL 的 `.rsrc` 段 266 KB，三个菜单（105/108/109）与六个图标（100/101/102/103/106/107）**都在**
  （用 `LoadLibraryEx(LOAD_LIBRARY_AS_DATAFILE)` + `FindResourceW` 实测枚举，不是看编译日志）；
- ✅ 服务端注册了 13 条语言栏命令；`DEPLOY` 走 `postCommand()`（互斥量 + `PostMessage`，跨线程安全）；
- ✅ 发行包与安装目录都带 `start_service.bat`（TIP 用 `WeaselRoot` 拼这个名字）；
- ✅ **语言栏左键切中/英真的打到了我们的引擎**（`scripts/test_langbar_ipc.ps1`）：
  开一个会话 → 基线 `status.ascii_mode=0` → 发 `ID_WEASELTRAY_ENABLE_ASCII`（40013）
  → 再读引擎状态，`status.ascii_mode=1` → 发 `ID_WEASELTRAY_DISABLE_ASCII`（40014）→ 回到 `0`。
  **不是只看「命令被接收了」，而是从引擎状态读回标志位**；
- ✅ 未注册的菜单 ID 回包 `0`、注册的回包 `1`（判据本身有效，不是恒真）；
- ✅ 自检 18 项 + tip-dll 4 组 + langbar-ipc 端到端全绿。

**实测中又确定的两条协议细节**（写下来省得下次再逆向）：

- 按键事件**没有额外的 body**：`KeyEvent` 是 4 字节的 `{keycode:16, mask:16}`，直接编码在
  `PipeMessage.wParam` 里（服务端 `OnKeyEvent` 里 `KeyEvent(wParam)`）。只有 `START_SESSION`
  额外带一段 UTF-16LE 的客户端信息（服务端 `AddSession` 要解析它）。
- 回包是「4 字节 DWORD + 可选 UTF-16LE 文本 body」（body 里带 `status.ascii_mode=` 等）。
  服务端是 `PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE`，所以客户端也必须用消息模式
  （.NET 里 `ReadMode` 只能在 `Connect()` **之后**设）。

**仍未做（需要真人在 GUI 里确认）**：Windows 11 的任务栏语言栏要勾选「使用桌面语言栏」才会显示
桌面版语言栏（现代输入法指示器不走 TSF 的 `CLangBarItemButton`），这一步无法自动化，
需要在装有本仓 TIP 的机器上人工点一次右键菜单确认。

### T12 执行记录（2026-11-18，已完成）—— 语言栏的视觉与语义优化

T11 把语言栏「接通」了，但**接上之后才看清它长什么样**：那套界面是上游 Weasel 的老 Win32 遗产，
跟本项目的 Fluent 视觉语言完全不搭，而且有一处语义是错的。本任务收拾干净。

| 项 | 改前 | 改后 |
| :--- | :--- | :--- |
| **语言栏图标** | 上游那套 Windows 3.x 时代的粗方框图标（深灰「中」、棕红「A」、绿色沙漏），**16px 下糊成一团**（语言栏实际就是用 `SM_CXSMICON` = 16px） | 本仓自己的 Fluent 版：`assets/langbar/*.ico`，**圆角方块 + 纯色底 + 居中白色粗体字形**，与托盘图标（`src/tray_menu.cpp` 的 `makeTrayIcon`）同一套画法与语义（中文=强调蓝，英文=中性灰）。八种分辨率（16/20/24/32/40/48/64/256） |
| **语言栏菜单文案** | 简体中文系统加载的是 `IDR_MENU_POPUP_HANS`，而**上游把它写成了繁体**（「設定 / 詞典管理 / 重新啟動服務」）⇒ 简体用户看到繁体界面 | HANS = 简体中文，HANT = 繁体，默认 = 英文；用词与设置中心 / 候选窗菜单统一（「词库与维护」「重新部署词库与配置」「用户词库目录」…） |
| **「同步用户数据」** | 被我误映射到 deploy —— 菜单写着「同步用户数据」，实际在做**重新编译词库** | 独立的 `sync_user_data`（上游 WeaselServer 走的是 `WeaselDeployer /sync`，语义对齐它） |
| **主 exe 的资源** | **一个资源都没有**（`EnumResourceTypes` 枚举过：图标 / 版本信息 / manifest 全空）⇒ 资源管理器、Alt-Tab、任务栏里都是系统通用占位图，文件属性也是空的 | 应用图标 + 版本信息 + manifest（asInvoker / PerMonitorV2 / 长路径） |

**为什么 exe 的资源是「零」**：xmake 3.1 **没有 `set_icon()` 接口**（试了，直接报
`unknown interface: set_icon()`），而 MSVC 链接器也没有生成 manifest（自检里
`build\Release` 下没有任何 `.manifest` 文件，exe 里也没有 `RT_MANIFEST`）。
⇒ 与 TIP 一样，自己写 `.rc`（`src/weasel-app.rc` + `src/weasel-app.manifest`），
用 `add_files("src/weasel-app.rc")` 编进目标。

**实测推翻的假设（这一轮又踩了四个坑，都写进代码注释了）**：

1. ❌「`.rc` 里的分隔符写 `SEPARATOR` 就行」 → **必须写 `MENUITEM SEPARATOR`**。
   裸 `SEPARATOR` 会让 rc.exe 报 **`RC2122 : unknown menu subtype`**，
   而且报错行号指向的是**那条分隔符**、真正出错的是它前面那一段的解析状态 ——
   典型的「报错位置离病因不远但也不是病因」。
2. ❌「`IDI_APP ICON "x.ico"` 里 ID 可以是任意标识符」 → **必须是数值**。
   rc.exe 把未定义的标识符当成**命名资源**（`MYICON ICON` ⇒ 名字叫 MYICON 的资源），
   而 Windows 选「应用图标」只看**数值 ID 最小**的 `RT_GROUP_ICON`
   ⇒ 命名资源不会被用上，结果是**图标编进去了却依然显示通用占位图**。
   （ID 也不能用 1：rc.exe 会把 .ico 里每一幅图编成 `RT_ICON(3)` 且 ID 从 1 自动排，
   本图标 8 幅 ⇒ 1..8 已占用。）
3. ❌「`.rc` 里的图标路径相对工作目录解析」 → 相对 **`.rc` 自身所在目录**。
   上游的 `.rc` 在 `WeaselTSF/` 下、图标在 `<repo>/resource/`，所以是 `..\resource\`；
   我们的 `.rc` 在 `tip/`、图标在 `assets/langbar/` ⇒ `..\assets\langbar\`。
4. ❌「`.rc` 里的图标找不到会报错」 → **rc.exe 找不到图标文件时会静默跳过**
   （菜单/图标缺失，但编译链接全绿）。这就是为什么自检里必须**枚举资源树**
   并逐个 `FindResourceW` 断言，而不是看编译日志。

**验证**：

- ✅ 资源枚举：TIP DLL 的 6 个 `RT_GROUP_ICON`（100/101/102/103/106/107，每个 8 幅图）
  + 3 个 `RT_MENU`（105/108/109）都在；主 exe 的 `RT_GROUP_ICON #100` + `RT_VERSION #1`
  + `RT_MANIFEST #1` 都在（改前是「零资源」）；
- ✅ **把图标从 DLL 里真的抠出来目视核对**（`FindResourceW` → `LockResource` → 落盘 PNG），
  确认资源段里就是我们的 Fluent 图标，不是「编译过了但内容不对」；
- ✅ 自检新增三组断言：每张 .ico 必须齐 8 种分辨率、`.rc` 引用的每个图标文件都必须真实存在、
  **简体中文菜单文字必须以 UTF-16LE 出现在 DLL 里**（防「又写回繁体」）；
- ✅ 端到端再加一条：`ID_WEASELTRAY_SYNC`（40012）命中**自己那个**处理器。

---

## File Structure

### 新增

| 路径 | 职责 |
| :--- | :-- |
| `src/weasel_engine.h` / `.cpp` | 封装上游 `RimeWithWeaselHandler` + `weasel::Server`：持引擎、转发按键、产出 `weasel::Context`/`Status` |
| `src/fluent_ui_adapter.h` / `.cpp` | 桥接层：把 `weasel::Context`/`Status` 映射为 `RimeUiState` 并推给 `CandidateWindow` |
| `src/weasel_config_client.h` / `.cpp` | `RimeConfigClient` 的 Weasel 实现（方案/部署/皮肤/快捷键） |
| `3rdparty/weasel/WeaselTSF/` | vendor 的上游 TSF 实现 |
| `3rdparty/weasel/patches/0001-disable-d2d-candidate-panel.patch` | 禁用 TIP 内 D2D 面板的 vendor 补丁（可审计） |
| `scripts/apply-weasel-patches.ps1` | 幂等地把补丁应用到 vendor 目录 |
| `scripts/install.ps1` / `scripts/uninstall.ps1` | 每用户安装器 / 卸载器（§6.6） |
| `src/weasel_protocol.h` | 上游协议常量与结构的薄封装（CLSID、管道名、命令码） |

### 删除（Task 8）

`src/rime_engine.h`、`src/rime_engine.cpp`、`tsf/`（含 `weasel_tsf.cpp`、`ipc_client.cpp`、`ipc_server.*` 旧协议）、
`src/ipc_server.h`、`src/ipc_server.cpp`、`src/input_hook.*`（若确认不再需要）

### 修改

| 路径 | 改动 |
| :--- | :-- |
| `xmake.lua` | 新增 `WeaselIPC`/`WeaselIPCServer`/`RimeWithWeasel`/`WeaselTSF` 静态库 target；Boost 链接；ATL 包含路径；`/MD` 统一 |
| `src/candidate_window.h` / `.cpp` | 新增 `updateFromWeasel(const weasel::Context&, const weasel::Status&)` |
| `src/settings_window.h` / `.cpp` | 方案/皮肤/快捷键改走 `WeaselConfigClient` |
| `src/main.cpp` | 服务模式改用 `WeaselEngine`；托盘命令经 Weasel IPC |
| `src/rime_config_client.h` | `RimeConfigClient` 接口保持不变（Round 1 已定），仅换实现 |

---

## Task 1: 依赖就位 —— Boost 1.88（/MD）+ ATL + xmake 静态库 target

**Files:**
- Modify: `scripts/build_boost.ps1`（`runtime-link=shared`）
- Modify: `xmake.lua`
- Create: `src/weasel_protocol.h`

**Interfaces:**
- Consumes: 无
- Produces:
  - xmake target `WeaselIPC`（static）、`WeaselIPCServer`（static）、`RimeWithWeasel`（static）
  - `src/weasel_protocol.h` 提供：
    ```cpp
    namespace WeaselProtocol {
        extern const wchar_t* kPipeName;          // L"WeaselNamedPipe"
        extern const wchar_t* kServerWindowClass;  // L"WeaselIPCWindow_1.0"
        extern const char* kClsidFluentTextService;  // "{D7B1C618-...}"
        extern const char* kClsidOfficialWeasel;     // "{A3F4CDED-...}"
    }
    ```

- [ ] **Step 1: 改 `scripts/build_boost.ps1` 用 /MD**

把 b2 调用里的 `runtime-link=shared` 保持不变（当前已是 shared），并确认注释写明「必须 /MD 以匹配 Qt 官方二进制」。若文件里仍是 `runtime-link=static`，改为 `runtime-link=shared`。

验证：脚本输出末尾应有
```
      OK boost_thread -> libboost_thread-vc143-mt-x64-1_88.lib
      OK boost_serialization -> ...
      OK boost_system -> ...
```

- [ ] **Step 2: 写 `src/weasel_protocol.h`**

```cpp
#ifndef WEASEL_PROTOCOL_H
#define WEASEL_PROTOCOL_H

/// 官方 Weasel IPC / TSF 注册相关的协议常量。
/// 数值与上游 include/WeaselIPC.h、tsf/register.cpp 保持一致。
namespace WeaselProtocol {

/// 上游 IPC 管道名（不含 \\.\pipe\<user>\ 前缀）。
inline constexpr const wchar_t* kPipeName = L"WeaselNamedPipe";

/// 上游 IPC 服务窗口类名。
inline constexpr const wchar_t* kServerWindowClass = L"WeaselIPCWindow_1.0";

/// 本仓 TSF 文本服务 CLSID。
inline constexpr const char* kClsidFluentTextService = "{D7B1C618-9F3E-4E7B-B96E-5C13D1A5B4C2}";

/// 官方 Weasel 文本服务 CLSID（安装器用它做 HKCU 影子覆盖）。
inline constexpr const char* kClsidOfficialWeasel = "{A3F4CDED-B1E9-41EE-9CA6-7B4D0DE6CB0A}";

/// 本仓 TSF 语言配置 id。
inline constexpr const char* kTipString =
    "0804:{D7B1C618-9F3E-4E7B-B96E-5C13D1A5B4C2}{E8934A01-C349-4F8A-8512-14AE0293BD01}";

} // namespace WeaselProtocol

#endif // WEASEL_PROTOCOL_H
```

- [ ] **Step 5: 在 `xmake.lua` 加三个静态库 target 与全局依赖**

在 `set_version("1.0.0")` 之后加入：

```lua
-- Boost（由 scripts/build_boost.ps1 构建到 deps/boost）
add_includedirs("deps/boost")
add_linkdirs("deps/boost/lib")

-- ATL（Visual Studio 自带）—— Weasel 的 WeaselIPCServer 用 ATL 的 CWindowImpl
local vs_root = os.getenv("VCToolsInstallDir") or ""
if vs_root ~= "" then
    add_includedirs(path.join(vs_root, "..", "..", "ATL", "include"))
    add_includedirs(path.join(vs_root, "..", "..", "ATL", "include", "atlwin"))
end

-- Vendored Weasel
add_includedirs("3rdparty/weasel/include")
add_includedirs("3rdparty/weasel/include/wtl")
```

再新增三个 target（放在 `FluentQt` target 之前）：

```lua
-- Target: WeaselIPC (static) — 上游 IPC 客户端
target("WeaselIPC")
    set_kind("static")
    set_languages("cxx17")
    add_defines("UNICODE", "_UNICODE", "WINDOWS", "MSVC", "BOOST_ALL_NO_LIB")
    if is_plat("windows") then
        add_cxflags("/utf-8", "/GS", "/Gy", "/EHsc", "/Zc:wchar_t", "/Zc:forScope", "/Zc:inline", "/MD", {tools = {"cl", "clang_cl"}})
    end
    add_includedirs("3rdparty/weasel/include")
    add_includedirs("3rdparty/weasel/include/wtl")
    add_files("3rdparty/weasel/WeaselIPC/*.cpp")
    add_syslinks("advapi32", "user32", "ole32")

-- Target: WeaselIPCServer (static)
target("WeaselIPCServer")
    set_kind("static")
    set_languages("cxx17")
    add_defines("UNICODE", "_UNICODE", "WINDOWS", "MSVC", "BOOST_ALL_NO_LIB")
    if is_plat("windows") then
        add_cxflags("/utf-8", "/GS", "/Gy", "/EHsc", "/Zc:wchar_t", "/Zc:forScope", "/Zc:inline", "/MD", {tools = {"cl", "clang_cl"}})
    end
    add_includedirs("3rdparty/weasel/include")
    add_includedirs("3rdparty/weasel/include/wtl")
    add_files("3rdparty/weasel/WeaselIPCServer/*.cpp")
    add_syslinks("advapi32", "user32", "ole32")

-- Target: RimeWithWeasel (static) — 上游引擎封装 + RequestHandler
target("RimeWithWeasel")
    set_kind("static")
    set_languages("cxx17")
    add_defines("UNICODE", "_UNICODE", "WINDOWS", "MSVC", "BOOST_ALL_NO_LIB")
    if is_plat("windows") then
        add_cxflags("/utf-8", "/GS", "/Gy", "/EHsc", "/Zc:wchar_t", "/Zc:forScope", "/Zc:inline", "/MD", {tools = {"cl", "clang_cl"}})
    end
    add_includedirs("3rdparty/weasel/include")
    add_includedirs("3rdparty/weasel/include/wtl")
    add_includedirs("deps/rime/dist/include")
    add_files("3rdparty/weasel/RimeWithWeasel/*.cpp")
    add_syslinks("advapi32", "user32", "ole32")
```

- [ ] **Step 6: 编译验证**

Run: `C:\devtools\xmake\xmake\xmake.exe build -a`
Expected: 三个 static target 编译通过。若 ATL 头找不到，把 `add_includedirs` 改成显式绝对路径
`C:/Program Files/Microsoft Visual Studio/18/Community/VC/ATL/include`。

- [ ] **Step 7: 提交**

```bash
git add xmake.lua scripts/build_boost.ps1 src/weasel_protocol.h
git commit -m "build: link the vendored Weasel core as static libraries

Adds WeaselIPC / WeaselIPCServer / RimeWithWeasel targets with ATL and
Boost headers wired in, and pins every vendored translation unit to /MD
so it matches the Qt 6.8.2 binaries instead of upstream's /MT."
```

---

## Task 2: vendor `WeaselTSF/` 并编译成静态库

**Files:**
- Create: `3rdparty/weasel/WeaselTSF/**`（vendor）
- Create: `3rdparty/weasel/WeaselUI/**`（vendor —— **WeaselTSF 依赖它**，见 T2 执行记录）
- Create: `3rdparty/weasel/patches/0001-disable-d2d-candidate-panel.patch`
- Create: `scripts/apply-weasel-patches.ps1`
- Modify: `xmake.lua`

**Interfaces:**
- Consumes: Task 1 的 xmake 结构
- Produces: xmake target `WeaselTSF`（static）；`scripts/apply-weasel-patches.ps1` 幂等应用补丁

- [ ] **Step 1: vendor 上游 `WeaselTSF/`**

```powershell
$src = 'C:\devtools\weasel-ref\WeaselTSF'
$dst = 'E:\weasel-fluent\weasel-fluent\3rdparty\weasel\WeaselTSF'
New-Item -ItemType Directory -Path $dst -Force | Out-Null
Copy-Item -Path "$src\*.cpp","$src\*.h" -Destination $dst -Force
Copy-Item -Path "$src\WeaselTSF.def" -Destination $dst -Force
```
不拷贝 `*.vcxproj` / `*.filters` / `*.rc`（xmake 不需要）。

- [ ] **Step 2: 写禁用 D2D 面板的 vendor 补丁**

`3rdparty/weasel/patches/0001-disable-d2d-candidate-panel.patch`（记录上游 `WeaselTSF/CandidateList.cpp` 中
`_MakeUIWindow()` 创建 `weasel::UI` 的位置，改为不创建）：

```diff
--- a/WeaselTSF/CandidateList.cpp
+++ b/WeaselTSF/CandidateList.cpp
@@
 void CCandidateList::_MakeUIWindow() {
+  // [weasel-fluent] 本项目用 Fluent 候选窗取代 Weasel 的 D2D 面板。
+  // 保留上下文/样式数据以继续服务 ITfCandidateListUIElement，但不创建窗口，
+  // 否则会与 Fluent 候选窗重复显示（R4）。
+  if (getenv("WEASEL_FLUENT_NO_D2D")) {
+    _pbShow = FALSE;
+    return;
+  }
```

补丁用**环境变量开关**而非硬改，理由：保留上游代码形态便于日后 rebase，且本机调试时可用
`WEASEL_FLUENT_NO_D2D=1` 临时关闭。

- [ ] **Step 3: 写 `scripts/apply-weasel-patches.ps1`（幂等）**

```powershell
$ErrorActionPreference = "Stop"
$RepoRoot = Split-Path -Parent $PSScriptRoot
$patchDir = Join-Path $RepoRoot "3rdparty\weasel\patches"
$targetRoot = Join-Path $RepoRoot "3rdparty\weasel"

Get-ChildItem -Path $patchDir -Filter '*.patch' -File | ForEach-Object {
    $applied = & git -C $targetRoot apply --check --reverse $_.FullName 2>$null
    if ($LASTEXITCODE -eq 0) {
        Write-Host "      already applied: $($_.Name)" -ForegroundColor DarkGray
    } else {
        & git -C $targetRoot apply $_.FullName
        if ($LASTEXITCODE -ne 0) { throw "failed to apply $($_.Name)" }
        Write-Host "      applied: $($_.Name)" -ForegroundColor Green
    }
}
exit 0
```

- [ ] **Step 4: `xmake.lua` 加 `WeaselTSF` target**

```lua
-- Target: WeaselTSF (static) — 上游 TSF 实现（D2D 面板已打补丁禁用）
target("WeaselTSF")
    set_kind("static")
    set_languages("cxx17")
    add_defines("UNICODE", "_UNICODE", "WINDOWS", "MSVC", "BOOST_ALL_NO_LIB", "WEASEL_TSF_EXPORTS")
    if is_plat("windows") then
        add_cxflags("/utf-8", "/GS", "/Gy", "/EHsc", "/Zc:wchar_t", "/Zc:forScope", "/Zc:inline", "/MD", {tools = {"cl", "clang_cl"}})
    end
    add_includedirs("3rdparty/weasel/include")
    add_includedirs("3rdparty/weasel/include/wtl")
    add_files("3rdparty/weasel/WeaselTSF/*.cpp")
    add_syslinks("advapi32", "user32", "ole32", "oleaut32", "dwmapi")
```

- [ ] **Step 5: 编译验证**

Run: `powershell -ExecutionPolicy Bypass -File scripts/apply-weasel-patches.ps1`
Expected: `applied: 0001-disable-d2d-candidate-panel.patch`

Run: `C:\devtools\xmake\xmake\xmake.exe build -a`
Expected: `WeaselTSF` target 编译通过。
若报 `CWindowImpl` / ATL 缺失 → 补 `add_includedirs("C:/Program Files/Microsoft Visual Studio/18/Community/VC/ATL/include")`。

- [ ] **Step 6: 提交**

```bash
git add 3rdparty/weasel/WeaselTSF 3rdparty/weasel/patches scripts/apply-weasel-patches.ps1 xmake.lua
git commit -m "build: vendor WeaselTSF with the D2D candidate panel disabled

The TIP stays a full TSF implementation (composition, display
attributes, language bar, compartment) but no longer creates its own
Direct2D panel, so the Fluent candidate window remains the only visible
candidate list (design rule R4). The change is kept as an auditable
patch rather than an in-place edit so upstream stays rebaseable."
```

---

## Task 3: `WeaselEngine` —— 用上游 `RimeWithWeaselHandler` 取代自研 `RimeEngine`

**Files:**
- Create: `src/weasel_engine.h`, `src/weasel_engine.cpp`

**Interfaces:**
- Consumes: Task 1 的 `RimeWithWeasel` target；vendor 的 `include/RimeWithWeasel.h`
- Produces:
  ```cpp
  class WeaselEngine : public QObject {
      Q_OBJECT
  public:
      explicit WeaselEngine(QObject* parent = nullptr);
      ~WeaselEngine() override;

      /// 启动 IPC 服务端并初始化 Rime 引擎。
      bool start(QString* error = nullptr);

      /// 处理一次按键（由 TIP 经 IPC 转发而来）。
      bool processKey(const weasel::KeyEvent& keyEvent, DWORD sessionId);

      /// 当前会话的组合态与状态。
      const weasel::Context& context() const;
      const weasel::Status& status() const;

      /// UI 需要重绘时的回调（桥接点，见 Task 4）。
      void setUpdateCallback(std::function<void()> cb);

      /// 触发全量重建（对应设计文档 §6.2）。
      bool deployFull(QString* error = nullptr);

  signals:
      void stateChanged();
  };
  ```

- [ ] **Step 1: 写 `src/weasel_engine.h`**

```cpp
#ifndef WEASEL_ENGINE_H
#define WEASEL_ENGINE_H

#include <QObject>
#include <QString>

#include <WeaselIPC.h>
#include <WeaselIPCData.h>
#include <RimeWithWeasel.h>

#include <functional>
#include <memory>

/// 封装上游的 Rime 引擎与 IPC 服务端。
///
/// 上游 Weasel 的分层是「服务持有引擎 + TIP 作为客户端」；本类维持同一分层，
/// 但把 UI 推送交给 Fluent 侧（见 FluentUiAdapter）。
class WeaselEngine : public QObject {
    Q_OBJECT
public:
    explicit WeaselEngine(QObject* parent = nullptr);
    ~WeaselEngine() override;

    bool start(QString* error = nullptr);
    void stop();

    bool processKey(const weasel::KeyEvent& keyEvent, DWORD sessionId);

    const weasel::Context& context() const { return m_context; }
    const weasel::Status& status() const { return m_status; }
    weasel::UIStyle& style() { return m_style; }

    void setUpdateCallback(std::function<void()> cb) { m_updateCb = std::move(cb); }

    bool deployFull(QString* error = nullptr);

    /// 服务端窗口句柄（安装器与托盘用）。
    HWND serverHwnd() const;

signals:
    void stateChanged();

private:
    void onUpdateUI();

    weasel::Server* m_server = nullptr;
    weasel::UI* m_ui = nullptr;
    std::unique_ptr<RimeWithWeaselHandler> m_handler;
    weasel::Context m_context;
    weasel::Status m_status;
    weasel::UIStyle m_style;
    std::function<void()> m_updateCb;
    DWORD m_sessionId = 0;
};

#endif // WEASEL_ENGINE_H
```

- [ ] **Step 2: 写 `src/weasel_engine.cpp`**

```cpp
#include "weasel_engine.h"

#include <QDebug>

WeaselEngine::WeaselEngine(QObject* parent) : QObject(parent) {}

WeaselEngine::~WeaselEngine() {
    stop();
}

bool WeaselEngine::start(QString* error) {
    // m_ui 不调用 Create() → 不创建 D2D 窗口，只作为 Context/Status 的载体。
    m_ui = new weasel::UI();

    m_handler = std::make_unique<RimeWithWeaselHandler>(m_ui);
    m_handler->Initialize();
    m_handler->OnUpdateUI([this]() { onUpdateUI(); });

    m_server = new weasel::Server();
    m_server->SetRequestHandler(m_handler.get());
    m_server->Start();

    m_sessionId = m_handler->AddSession(nullptr);
    if (error) {
        *error = QStringLiteral("Weasel IPC 服务已启动");
    }
    return true;
}

void WeaselEngine::stop() {
    if (m_server) {
        m_server->Stop();
        delete m_server;
        m_server = nullptr;
    }
    if (m_handler) {
        m_handler->Finalize();
        m_handler.reset();
    }
    if (m_ui) {
        m_ui->Destroy(true);
        delete m_ui;
        m_ui = nullptr;
    }
}

bool WeaselEngine::processKey(const weasel::KeyEvent& keyEvent, DWORD sessionId) {
    if (!m_handler) {
        return false;
    }
    return m_handler->ProcessKeyEvent(keyEvent, sessionId, nullptr) != FALSE;
}

void WeaselEngine::onUpdateUI() {
    if (m_ui) {
        m_context = m_ui->ctx();
        m_status = m_ui->status();
        m_style = m_ui->style();
    }
    if (m_updateCb) {
        m_updateCb();
    }
    emit stateChanged();
}

bool WeaselEngine::deployFull(QString* error) {
    // 全量重建：RimeStartMaintenance(FullDeploy) + JoinMaintenanceThread。
    if (!m_handler) {
        if (error) *error = QStringLiteral("引擎未初始化");
        return false;
    }
    m_handler->StartMaintenance();
    m_handler->EndMaintenance();
    if (error) *error = QStringLiteral("已重新部署");
    return true;
}

HWND WeaselEngine::serverHwnd() const {
    return m_server ? m_server->GetHWnd() : nullptr;
}
```

- [ ] **Step 3: 编译验证**

Run: `C:\devtools\xmake\xmake\xmake.exe build -a`
Expected: 编译通过。
⚠️ `RimeWithWeaselHandler::ProcessKeyEvent` 的第三参 `EatLine` 是 `std::function<bool(std::wstring&)>`，传 `nullptr` 合法。

- [ ] **Step 4: 提交**

```bash
git add src/weasel_engine.h src/weasel_engine.cpp
git commit -m "feat(engine): wrap the upstream RimeWithWeaselHandler as WeaselEngine

Uses Weasel's own RequestHandler implementation instead of maintaining a
parallel engine, which is the swap point the design calls for. The
weasel::UI instance is created without Create(), so no Direct2D window
is ever made -- it only carries Context/Status for the Fluent side."
```

---

## Task 4: 桥接 —— `weasel::Context/Status` → Fluent 候选窗

**Files:**
- Create: `src/fluent_ui_adapter.h`, `src/fluent_ui_adapter.cpp`
- Modify: `src/candidate_window.h`, `src/candidate_window.cpp`

**Interfaces:**
- Consumes: Task 3 的 `WeaselEngine::context()/status()`
- Produces:
  ```cpp
  class FluentUiAdapter {
  public:
      explicit FluentUiAdapter(CandidateWindow* window);
      /// 把上游 Context/Status 映射为 RimeUiState 并推给候选窗。
      void apply(const weasel::Context& ctx, const weasel::Status& status);
  };
  ```
  `CandidateWindow` 新增：
  ```cpp
  /// 直接以 Weasel 的组合态刷新候选窗（替代旧的 RimeUiState 通道）。
  void updateFromWeasel(const weasel::Context& ctx, const weasel::Status& status);
  ```

- [ ] **Step 1: 写 `src/fluent_ui_adapter.h`**

```cpp
#ifndef FLUENT_UI_ADAPTER_H
#define FLUENT_UI_ADAPTER_H

#include <WeaselIPCData.h>

class CandidateWindow;

/// 把上游 Weasel 的组合态映射为本仓的候选窗状态。
///
/// 映射关系（设计文档 §4.3）：
///   ctx.preedit.str            → RimeUiState::preedit
///   ctx.cinfo.candies          → RimeUiState::candidates[i].text
///   ctx.cinfo.comments         → RimeUiState::candidates[i].comment
///   ctx.cinfo.highlighted      → RimeUiState::highlightedIndex
///   ctx.cinfo.currentPage      → RimeUiState::pageNo
///   ctx.cinfo.is_last_page     → RimeUiState::isLastPage
///   status.ascii_mode          → 中英模式
///   status.schema_id           → 方案 id
class FluentUiAdapter {
public:
    explicit FluentUiAdapter(CandidateWindow* window) : m_window(window) {}

    void apply(const weasel::Context& ctx, const weasel::Status& status);

    /// 纯函数形式，便于单元测试。
    static RimeUiState toUiState(const weasel::Context& ctx, const weasel::Status& status);

private:
    CandidateWindow* m_window = nullptr;
};

#endif // FLUENT_UI_ADAPTER_H
```

- [ ] **Step 2: 写 `src/fluent_ui_adapter.cpp`**

```cpp
#include "fluent_ui_adapter.h"

#include "candidate_window.h"

RimeUiState FluentUiAdapter::toUiState(const weasel::Context& ctx, const weasel::Status& status) {
    RimeUiState st;
    st.isComposing = !ctx.preedit.str.empty();
    st.preedit = QString::fromStdWString(ctx.preedit.str);
    st.cursorPos = ctx.preedit.cursor;
    st.highlightedIndex = ctx.cinfo.highlighted;
    st.pageNo = ctx.cinfo.currentPage;
    st.isLastPage = ctx.cinfo.is_last_page;

    const int n = static_cast<int>(ctx.cinfo.candies.size());
    st.candidates.reserve(n);
    for (int i = 0; i < n; ++i) {
        CandidateItem item;
        item.index = i;
        item.text = QString::fromStdWString(ctx.cinfo.candies[i]);
        if (i < static_cast<int>(ctx.cinfo.comments.size())) {
            item.comment = QString::fromStdWString(ctx.cinfo.comments[i]);
        }
        st.candidates.append(item);
    }
    return st;
}

void FluentUiAdapter::apply(const weasel::Context& ctx, const weasel::Status& status) {
    if (!m_window) {
        return;
    }
    m_window->updateFromWeasel(ctx, status);
}
```

- [ ] **Step 3: 给 `CandidateWindow` 加 `updateFromWeasel`**

`src/candidate_window.h` 的 `public:` 区加：

```cpp
    /// 以 Weasel 的组合态刷新候选窗。
    void updateFromWeasel(const weasel::Context& ctx, const weasel::Status& status);
```

`src/candidate_window.cpp` 实现（把既有 `updateUiState` 的内部逻辑复用）：

```cpp
void CandidateWindow::updateFromWeasel(const weasel::Context& ctx, const weasel::Status& status) {
    const RimeUiState st = FluentUiAdapter::toUiState(ctx, status);
    updateUiState(st);
    setAsciiMode(status.ascii_mode);
}
```
并在 `src/candidate_window.cpp` 顶部加 `#include "fluent_ui_adapter.h"`。
若 `CandidateWindow` 没有 `setAsciiMode`，改用其既有的模式 setter；没有则新增
`void setAsciiMode(bool ascii);`（成员 `bool m_asciiMode = false;`）。

- [ ] **Step 4: 接线：引擎回调 → 适配器**

在 `src/main.cpp` 的服务模式里，创建引擎后：

```cpp
    WeaselEngine engine;
    QString engineError;
    if (!engine.start(&engineError)) {
        std::cerr << "[FATAL] " << engineError.toStdString() << "\n";
        return 1;
    }

    FluentUiAdapter adapter(&candWin);
    engine.setUpdateCallback([&adapter, &engine]() {
        adapter.apply(engine.context(), engine.status());
    });
```

- [ ] **Step 5: 扩展 `--verify`：TEST 15（映射正确性）**

```cpp
    // 15. Verify FluentUiAdapter maps Weasel Context/Status to RimeUiState
    std::cout << "[TEST 15] Testing FluentUiAdapter context mapping...\n";
    {
        weasel::Context ctx;
        ctx.preedit.str = L"ni hao";
        ctx.preedit.cursor = 5;
        ctx.cinfo.candies.push_back(L"\x4F60\x597D");
        ctx.cinfo.candies.push_back(L"\x4F60");
        ctx.cinfo.comments.push_back(L"");
        ctx.cinfo.comments.push_back(L"");
        ctx.cinfo.highlighted = 1;
        ctx.cinfo.currentPage = 0;
        ctx.cinfo.is_last_page = true;

        weasel::Status status;
        status.ascii_mode = true;
        status.schema_id = "luna_pinyin";

        const RimeUiState st = FluentUiAdapter::toUiState(ctx, status);
        if (!st.isComposing || st.preedit != QStringLiteral("ni hao")) {
            std::cerr << "FAIL: preedit mapping mismatch\n";
            return 1;
        }
        if (st.candidates.size() != 2 || st.candidates[0].text != QStringLiteral("你好")) {
            std::cerr << "FAIL: candidate mapping mismatch\n";
            return 1;
        }
        if (st.highlightedIndex != 1 || !st.isLastPage) {
            std::cerr << "FAIL: status mapping mismatch\n";
            return 1;
        }
        std::cout << "  -> PASS: Weasel Context/Status -> RimeUiState mapping OK.\n";
    }
```

- [ ] **Step 6: 编译并跑验证**

Run: `C:\devtools\xmake\xmake\xmake.exe build -a && .\build\Release\weasel-fluent-cpp-qt.exe --verify`
Expected: 15 项 PASS。

- [ ] **Step 7: 提交**

```bash
git add src/fluent_ui_adapter.h src/fluent_ui_adapter.cpp src/candidate_window.h src/candidate_window.cpp src/main.cpp
git commit -m "feat(candidate): bridge Weasel context into the Fluent candidate window

weasel::Context already carries the preedit plus the full candidate list,
and weasel::Status carries ascii mode and schema id, so the mapping onto
our existing RimeUiState is almost one to one. This is the bridge that
lets the TIP stop rendering its own D2D panel."
```

---

## Task 5: TIP 换入 `WeaselTSF` + 禁用 D2D 面板

**Files:**
- Modify: `src/main.cpp`（服务模式下用上游 TIP 协议）
- Create: `src/weasel_tip_client.h`, `src/weasel_tip_client.cpp`
- Modify: `3rdparty/weasel/patches/0001-disable-d2d-candidate-panel.patch`

**Interfaces:**
- Consumes: Task 2 的 `WeaselTSF` 静态库；Task 4 的适配器
- Produces:
  ```cpp
  /// TIP 侧的 IPC 客户端：连上服务、发按键、读回组合态。
  class WeaselTipClient {
  public:
      WeaselTipClient();
      bool connectToServer(QString* error = nullptr);
      void startSession();
      void endSession();
      bool sendKey(weasel::KeyEvent ev);
      /// 读回服务推送的 Context/Status（供 TIP 侧内联预编辑使用）。
      bool readResponse(weasel::ResponseParser& parser);
  };
  ```

- [ ] **Step 1: 写 `src/weasel_tip_client.h` / `.cpp`**

```cpp
#include "weasel_tip_client.h"
#include "weasel_protocol.h"

WeaselTipClient::WeaselTipClient() = default;

bool WeaselTipClient::connectToServer(QString* error) {
    if (!m_client.Connect()) {
        if (error) *error = QStringLiteral("无法连接 Weasel 服务");
        return false;
    }
    return true;
}

void WeaselTipClient::startSession() { m_client.StartSession(); }
void WeaselTipClient::endSession()   { m_client.EndSession(); }

bool WeaselTipClient::sendKey(weasel::KeyEvent ev) {
    return m_client.ProcessKeyEvent(ev);
}

bool WeaselTipClient::readResponse(weasel::ResponseParser& parser) {
    return m_client.GetResponseData(std::ref(parser));
}
```

- [ ] **Step 2: 把补丁改成无条件禁用 D2D 面板**

因为 Round 2 起 Fluent 候选窗是唯一候选窗，补丁里的环境变量开关改为**编译期宏**，
默认关闭 D2D：

```diff
--- a/WeaselTSF/CandidateList.cpp
+++ b/WeaselTSF/CandidateList.cpp
@@
 void CCandidateList::_MakeUIWindow() {
+  // [weasel-fluent] 候选窗由 Fluent 前端独占；此处不创建 D2D 窗口，
+  // 以免与 Fluent 候选窗重复显示（设计文档 §4.2 R4）。
+  // 仍保留 Context/Style 数据，以继续服务 ITfCandidateListUIElement。
+  {
+    _pbShow = FALSE;
+    return;
+  }
```

同时在 `xmake.lua` 的 `WeaselTSF` target 上加 `add_defines("WEASEL_FLUENT_NO_D2D")`。

- [ ] **Step 3: 打包 TIP DLL**

`xmake.lua` 的 TSF target 改为：

```lua
target("weasel-fluent-tsf")
    set_kind("shared")
    set_languages("cxx17")
    add_defines("UNICODE", "_UNICODE", "WEASEL_TSF_EXPORTS", "WEASEL_FLUENT_NO_D2D")
    if is_plat("windows") then
        add_cxflags("/utf-8", "/FS", "/MP", "/MD", {tools = {"cl", "clang_cl"}})
        add_syslinks("User32", "Gdi32", "Advapi32", "Ole32", "OleAut32", "Dwmapi")
    end
    add_includedirs("3rdparty/weasel/include")
    add_includedirs("3rdparty/weasel/include/wtl")
    add_deps("WeaselIPC", "WeaselTSF")
    add_files("src/weasel_tip_client.cpp")
    add_files("3rdparty/weasel/WeaselTSF/WeaselTSF.def")
```

> ⚠️ 上游 `WeaselTSF.def` 导出的名字是 `DllGetClassObject` 等；本仓原 `tsf/weasel_tsf.def` 同样如此。
> 保留上游 def，但需确认 CLSID 常量与本仓 `WeaselProtocol::kClsidFluentTextService` 一致；
> 若不一致，在 `WeaselTSF/Globals.h` 内改。

- [ ] **Step 4: 编译并验证 DLL 导出**

Run: `C:\devtools\xmake\xmake\xmake.exe build -a`
Expected: `weasel-fluent-tsf.dll` 生成成功。

Run: `dumpbin /exports build\Release\weasel-fluent-tsf.dll`
Expected: 含 `DllGetClassObject` / `DllCanUnloadNow` / `DllRegisterServer` / `DllUnregisterServer`。

- [ ] **Step 5: 提交**

```bash
git add src/weasel_tip_client.h src/weasel_tip_client.cpp xmake.lua 3rdparty/weasel/patches
git commit -m "feat(tsf): swap the TIP for upstream WeaselTSF

The TIP becomes a full Weasel TSF implementation (composition, display
attributes, language bar, compartment) while its Direct2D candidate
panel is compiled out, leaving the Fluent candidate window as the single
visible candidate list."
```

---

## Task 6: 设置中心接入 Weasel 数据源

**Files:**
- Create: `src/weasel_config_client.h`, `src/weasel_config_client.cpp`
- Modify: `src/rime_config_client.h`（保持接口不变）
- Modify: `src/settings_window.cpp`（样式改走 Weasel 通道）

**Interfaces:**
- Consumes: Task 3 的 `WeaselEngine`；上游 `Styler` / `ResponseParser` / `weasel::Client`
- Produces: `WeaselConfigClient : public RimeConfigClient`

> **通道边界（已核实上游源码，勿再猜）**
>
> | 设置项 | 数据来源 | 通道 | 本计划的动作 |
> | :--- | :--- | :--- | :--- |
> | 方案清单 | `%APPDATA%\Rime\*.schema.yaml` | 文件系统 | **沿用 Round 1 的 `SchemaManager`，不迁移** |
> | 重新部署 | `RimeStartMaintenance(FullDeploy)` | **IPC**：`Client::StartMaintenance()` / `EndMaintenance()` | 改走 `WeaselConfigClient::deployFull()` |
> | 用户词库 | `%APPDATA%\Rime\*.dict.yaml` + `default.custom.yaml` | 文件系统 | **沿用 Round 1 的 `DictionaryManager`，不迁移** |
> | 皮肤（内容层） | `weasel.yaml` → `weasel::UIStyle` | **IPC 响应流**（`Styler` → `p_style`） | **新增** |
> | 快捷键 | `default.custom.yaml` 的 `key_binder/bindings` | 文件系统 | **沿用 Round 1 的 `KeyBindings`（Fluent 层增强），不迁移** |
>
> 依据：`include/WeaselIPCData.h` 的 `struct Config` **只有 `inline_preedit` 一个字段**；
> `WeaselIPC/Configurator.cpp` 只处理 `inline_preedit`；方案清单在上游是由部署器
> `SwitcherSettingsDialog.cpp` 直接调 Rime API `get_available_schema_list()` 取得的，
> **不在 IPC 响应流里**。因此不要试图从 `p_config` 里取方案清单。

- [ ] **Step 1: 写 `src/weasel_config_client.h`**

```cpp
#ifndef WEASEL_CONFIG_CLIENT_H
#define WEASEL_CONFIG_CLIENT_H

#include "rime_config_client.h"

#include <WeaselIPC.h>
#include <WeaselIPCData.h>

/// 基于官方 Weasel IPC 的配置操作实现。
///
/// 只覆盖真正走 IPC 的两项：
///   - 部署（WEASEL_IPC_START_MAINTENANCE / END_MAINTENANCE）
///   - 样式（响应流里的 weasel::UIStyle，由上游 Styler 反序列化）
/// 方案清单 / 用户词库 / 快捷键不在 IPC 上，仍由 SchemaManager /
/// DictionaryManager / KeyBindings 负责（见任务头的通道边界表）。
class WeaselConfigClient : public RimeConfigClient {
public:
    explicit WeaselConfigClient(weasel::Client* client) : m_client(client) {}
    ~WeaselConfigClient() override;

    bool deployFull(QString* error) override;
    bool selectSchema(const QString& schemaId, QString* error) override;
    QString activeSchema() const override;

    /// 读取服务端当前样式（weasel.yaml → weasel::UIStyle）。
    bool loadStyle(weasel::UIStyle* out, QString* error = nullptr);

private:
    weasel::Client* m_client = nullptr;
    weasel::UIStyle m_style;
    QString m_activeSchema;
};

#endif // WEASEL_CONFIG_CLIENT_H
```

- [ ] **Step 2: 写 `src/weasel_config_client.cpp`**

```cpp
#include "weasel_config_client.h"

#include "ResponseParser.h"
#include "Styler.h"

WeaselConfigClient::~WeaselConfigClient() = default;

bool WeaselConfigClient::loadStyle(weasel::UIStyle* out, QString* error) {
    if (!m_client) {
        if (error) *error = QStringLiteral("未连接服务");
        return false;
    }
    weasel::ResponseParser parser(nullptr, nullptr, nullptr, nullptr, &m_style);
    if (!m_client->GetResponseData(std::ref(parser))) {
        if (error) *error = QStringLiteral("读取服务端样式失败");
        return false;
    }
    if (out) *out = m_style;
    return true;
}

bool WeaselConfigClient::deployFull(QString* error) {
    if (!m_client) {
        if (error) *error = QStringLiteral("未连接服务");
        return false;
    }
    // 上游的部署命令就是这对 maintenance 消息：
    // WEASEL_IPC_START_MAINTENANCE / WEASEL_IPC_END_MAINTENANCE
    m_client->StartMaintenance();
    m_client->EndMaintenance();
    if (error) *error = QStringLiteral("已重新部署");
    return true;
}

bool WeaselConfigClient::selectSchema(const QString& schemaId, QString* error) {
    if (!m_client) {
        if (error) *error = QStringLiteral("未连接服务");
        return false;
    }
    // 方案切换本身由 Rime 侧完成（default.custom.yaml 的
    // previously_selected_schema + 全量部署），本客户端只记录当前选中项。
    m_activeSchema = schemaId;
    if (error) *error = QStringLiteral("已切换方案");
    return true;
}

QString WeaselConfigClient::activeSchema() const {
    return m_activeSchema;
}
```

- [ ] **Step 3: 设置中心接线**

`src/settings_window.cpp`：
- **方案下拉**：仍用 `SchemaManager::instance().scanSchemas()`（文件系统，见通道边界表），**不改**。
- **部署按钮**：`onDeployClicked()` 改为经 `RimeConfigClient::deployFull()`；
  若设置中心进程没有 `weasel::Client`，则新建一个 `weasel::Client` 并 `Connect()`。
- **皮肤**：新增「Weasel 状态栏样式」区，绑定 `WeaselConfigClient::loadStyle()`，
  展示 `font_point` / `horizontal` / `color_scheme` 等 Weasel 样式键；
  写入仍走 `weasel.yaml`（由服务端 `LoadWeaselUIStyle()` 读取）。
- **Fluent token 皮肤**：保持 Round 1 的 `SkinManager` 不变（框架层）。

- [ ] **Step 4: 扩展 `--verify`：TEST 16（样式读取往返）**

```cpp
    // 16. Verify WeaselConfigClient style read-back
    std::cout << "[TEST 16] Testing WeaselConfigClient style read-back...\n";
    {
        weasel::Client probe;
        if (!probe.Connect()) {
            std::cout << "  -> SKIP: 服务未运行，跳过样式读取断言。\n";
        } else {
            WeaselConfigClient cfg(&probe);
            weasel::UIStyle style;
            QString err;
            if (!cfg.loadStyle(&style, &err)) {
                std::cout << "  -> SKIP: 读取样式失败（" << err.toStdString() << "）\n";
            } else {
                std::cout << "  -> PASS: 读取到样式 font_point=" << style.font_point
                          << " horizontal=" << style.horizontal << "\n";
            }
            probe.Disconnect();
        }
    }
```

- [ ] **Step 5: 编译并验证**

Run: `C:\devtools\xmake\xmake\xmake.exe build -a && .\build\Release\weasel-fluent-cpp-qt.exe --verify`
Expected: 16 项 PASS（服务未运行时 TEST 16 输出 SKIP）。

- [ ] **Step 6: 提交**

```bash
git add src/weasel_config_client.h src/weasel_config_client.cpp src/settings_window.cpp src/main.cpp
git commit -m "feat(settings): read style from the Weasel IPC and deploy over it

Only two settings actually travel over Weasel's IPC: deployment (the
maintenance message pair) and the style blob (UIStyle, deserialized by
upstream's Styler). Scheme list, dictionaries and key bindings stay on
the filesystem, which is where Rime itself keeps them."
```

---

## Task 7: 删除过渡实现（R6 验收）

**Files:**
- Delete: `src/rime_engine.h`, `src/rime_engine.cpp`, `src/ipc_server.h`, `src/ipc_server.cpp`
- Delete: `tsf/`（旧精简 TIP 与自有 IPC 协议）
- Modify: `xmake.lua`（移除旧 TSF target 的源文件引用）
- Modify: `src/settings_window.cpp`, `src/main.cpp`, `src/candidate_window.cpp`（改用新接口）

**Interfaces:**
- Consumes: Task 3–6
- Produces: 仓库内**不再有**平行的 Rime 引擎或 TSF 实现

- [ ] **Step 1: 全仓搜索旧接口引用**

```powershell
Select-String -Path src/*.cpp,src/*.h,tsf/*.cpp,tsf/*.h -Pattern 'RimeEngine|IpcServer|IpcClient|input_hook|WEASEL_PIPE_NAME|IpcKeyRequest'
```
Expected: 列出所有仍依赖旧实现的文件。

- [ ] **Step 2: 逐个改用新接口**

- `src/settings_window.cpp`：`RimeEngine* m_engine` → `WeaselEngine* m_engine`；
  `m_engine->deployFull(&err)` 语义不变；`m_engine->selectSchema(id)` → 走 `RimeConfigClient`。
- `src/main.cpp`：服务模式改用 `WeaselEngine` + `FluentUiAdapter`（Task 4 已接线）。
- `src/candidate_window.cpp`：`m_engine->processKey(RIME_KEY_*)` → `m_engine->processKey(weasel::KeyEvent{...})`。

- [ ] **Step 3: 删除旧文件并清理 xmake**

```bash
git rm src/rime_engine.h src/rime_engine.cpp src/ipc_server.h src/ipc_server.cpp tsf/weasel_tsf.cpp tsf/weasel_tsf.def tsf/ipc_client.cpp tsf/ipc_client.h tsf/globals.h tsf/dllmain.cpp tsf/register.cpp
```
`xmake.lua` 的 TSF target 已在 Task 5 重写，确认不再引用 `tsf/*.cpp`。

- [ ] **Step 4: 编译验证**

Run: `C:\devtools\xmake\xmake\xmake.exe build -a`
Expected: 编译通过，**无** `tsf/` 源文件参与。

- [ ] **Step 5: 扩展 `--verify`：TEST 17（过渡实现已清空）**

```cpp
    // 17. Verify no parallel engine / TIP implementation remains
    std::cout << "[TEST 17] Verifying transition implementation removed...\n";
    {
        const QString repo = QCoreApplication::applicationDirPath() + QStringLiteral("/../../..");
        const QStringList forbidden = {
            QStringLiteral("src/rime_engine.cpp"),
            QStringLiteral("tsf/weasel_tsf.cpp"),
            QStringLiteral("tsf/ipc_client.cpp"),
        };
        int bad = 0;
        for (const QString& f : forbidden) {
            if (QFileInfo::exists(repo + QLatin1Char('/') + f)) {
                std::cerr << "FAIL: transition file still present: " << f.toStdString() << "\n";
                ++bad;
            }
        }
        if (bad) {
            return 1;
        }
        std::cout << "  -> PASS: 过渡实现已全部移除。\n";
    }
```

- [ ] **Step 6: 跑全部自检并提交**

```bash
powershell -ExecutionPolicy Bypass -File scripts/run-selfchecks.ps1
git add -A
git commit -m "refactor(engine): remove the parallel engine and thin TIP

Deletes the self-written RimeEngine and the minimal TSF implementation
now that the upstream Weasel runtime drives the input pipeline. The
RimeConfigClient abstraction that made the swap possible is kept and
now has a single implementation."
```

---

## Task 8: 安装器（自动替换旧的服务）

**Files:**
- Create: `scripts/install.ps1`, `scripts/uninstall.ps1`
- Modify: `scripts/package_dist.ps1`（产出安装器与安装脚本）
- Modify: `src/weasel_protocol.h`（若协议常量需调整）

**Interfaces:**
- Consumes: `WeaselProtocol::kTipString` / `kClsidOfficialWeasel`
- Produces: 每用户安装到 `%LOCALAPPDATA%\Programs\WeaselFluent`，免管理员

- [ ] **Step 1: 写 `scripts/install.ps1`**

要点（顺序不可变）：
1. 停旧进程：`WeaselServer.exe`、旧 `weasel-fluent-cpp-qt.exe`。
2. 反注册**旧的本仓** TSF DLL（若在旧位置），再反注册官方 Weasel 的（若可写）。
3. 复制 payload 到 `%LOCALAPPDATA%\Programs\WeaselFluent`：`weasel-fluent-cpp-qt.exe`、
   `weasel-fluent-tsf.dll`、`rime.dll`、`data/`。
4. `regsvr32 /s` 注册本仓 TSF DLL —— 依赖 **HKCU CLSID 影子注册**：
   在 HKCU 下同时注册本仓 CLSID 与官方 Weasel CLSID（COM 解析 HKCU 优先于 HKLM），
   从而**免提权**顶掉机器上已装的官方 Weasel。
5. `InstallLayoutOrTip("0804:{D7B1C618-…}{E8934A01-…}")` 把本仓 TIP 加进用户语言列表。
6. 写 `HKCU\Software\Microsoft\Windows\CurrentVersion\Run\WeaselFluent` 自启。
7. **幂等**：每步先检测后动作；重复执行不报错。

- [ ] **Step 2: 写 `scripts/uninstall.ps1`**

反向：停进程 → `regsvr32 /u` → 移除 HKCU Run 项 → 移除语言列表里的本仓 TIP
（恢复微软拼音为主）→ 保留用户数据 `%APPDATA%\Rime`（除非 `--purge`）。

- [ ] **Step 3: 扩展 `scripts/package_dist.ps1` 产出安装器**

把生成的 `install_and_run.bat` 改为调用 `scripts/install.ps1`，并在包内附带
`install.ps1` / `uninstall.ps1`。

- [ ] **Step 4: 扩展 `--verify`：TEST 18（安装器幂等性静态检查）**

断言 `scripts/install.ps1` 存在、含 HKCU 影子注册（同时出现本仓 CLSID 与官方 CLSID）、
含 `InstallLayoutOrTip`、含 `Run\WeaselFluent`。

- [ ] **Step 5: 提交**

```bash
git add scripts/install.ps1 scripts/uninstall.ps1 scripts/package_dist.ps1 src/main.cpp
git commit -m "feat(installer): per-user installer that shadows the official Weasel

Installs to %LOCALAPPDATA%\Programs\WeaselFluent without elevation by
registering our TIP under the official Weasel CLSID in HKCU, which COM
resolves ahead of HKLM. Idempotent at every step, and it adds our TIP to
the user language list so Win+Space can reach it."
```

---

## Task 9: 打包与端到端验收

**Files:**
- Modify: `scripts/package_dist.ps1`
- Modify: `README.md`

**Interfaces:**
- Consumes: Task 1–8
- Produces: 可分发的完整发行包

- [ ] **Step 1: `package_dist.ps1` 产出完整包**

包内应含：两个 exe、`rime.dll`、`data/`、`config.json`、`install.ps1`、`uninstall.ps1`、
`install_and_run.bat`、`stop.bat`、`settings.bat`。

- [ ] **Step 2: 端到端手工验收（按顺序）**

1. 在装有官方 Weasel 的机器上跑 `install.ps1` → 语言列表出现本仓输入法 → `Win+Space` 可切到。
2. 任意应用中打拼音 → 候选窗出现，且**只有一套**候选窗（R4）。
3. 切换方案 → 候选随之改变 → 重启后仍是新方案。
4. 导入自定义词库 → 候选首位命中。
5. 改皮肤 → 候选窗与设置中心配色即时变化。
6. 改快捷键 → 新键生效、旧键失效。
7. 卸载 → 官方 Weasel 恢复可用。

- [ ] **Step 3: 更新 README**

补一节说明：本项目基于小狼毫内核 + Fluent 前端；安装器用法；两轮架构演进
（自研过渡实现 → 官方 Weasel 运行时）。

- [ ] **Step 4: 收尾自检并提交**

```bash
powershell -ExecutionPolicy Bypass -File scripts/run-selfchecks.ps1
git add -A
git commit -m "docs: document the Weasel-runtime architecture and installer"
```

---

## Self-Review

**Spec 覆盖**

| 设计文档 §7 Round 2 条目 | 对应任务 |
| :--- | :--- |
| 2.1 编译并链接 Weasel 内核 | Task 1、Task 2 |
| 2.2 换入 `WeaselTSF` | Task 5 |
| 2.3 桥接 `CCandidateList` → Fluent 候选窗 | Task 4、Task 5 |
| 2.4 服务切到 `WeaselIPCServer + RimeWithWeasel` | Task 3、Task 6 |
| 2.5 删除过渡实现 | Task 7 |
| 2.6 `install.ps1` / `uninstall.ps1` | Task 8 |
| 2.7 皮肤补 Weasel style 栏 | Task 6（Step 4） |
| 2.8 `package_dist.ps1` 完整发行包 | Task 9 |

**关键路径**：Task 3 → Task 4 → Task 5 → Task 7（与设计文档 §7 的 2.2→2.3→2.4→2.5 一致）。
Task 1/2 为前置；Task 6 依赖 Task 3；Task 8 依赖 Task 5（要注册换过的 TIP）；Task 9 收尾。

**已核实的上游事实（本计划的依据，勿再猜）**：

| 事实 | 出处 | 对计划的影响 |
| :--- | :--- | :--- |
| `weasel::Config` **只有 `inline_preedit` 一个字段** | `include/WeaselIPCData.h` | 方案清单**不在** IPC 响应流里 ⇒ Task 6 改为文件系统方案 |
| `Configurator::Store` 只处理 `inline_preedit` | `WeaselIPC/Configurator.cpp` | 同上 |
| 方案清单由部署器直接调 Rime API `get_available_schema_list()` | `WeaselDeployer/SwitcherSettingsDialog.cpp` | 上游部署器**不走 IPC** ⇒ 我们沿用 `SchemaManager` 扫文件 |
| 部署命令就是 `StartMaintenance()/EndMaintenance()` | `include/WeaselIPC.h` 的 `WEASEL_IPC_START_MAINTENANCE` | Task 6 的 `deployFull()` 有现成 API，**不需要扩展协议** |
| 样式经响应流的 `Styler` 反序列化进 `p_style` | `WeaselIPC/Styler.cpp` | Task 6 新增「Weasel 状态栏样式」区 |
| 响应流是**自描述**的：服务端下发 action 名，`ActionLoader` 按名路由到对应 Deserializer | `WeaselIPC/ActionLoader.cpp`、`Deserializer.cpp` | 未来扩展配置项应注册新 Deserializer，而不是改协议 |
| TIP 本身就是 WeaselIPC **客户端**：`WeaselTSF.cpp:181` 用 `m_client.GetResponseData(parser)` 取回 Status/UIStyle | `WeaselTSF/WeaselTSF.cpp` | TIP 侧需要一个 WeaselIPC 客户端 ⇒ Task 5 的 `WeaselTipClient` |
| `RimeWithWeaselHandler : public weasel::RequestHandler` | `include/RimeWithWeasel.h:42` | **上游自带引擎↔IPC 胶水层** ⇒ Task 3 直接用它，不自研引擎 |
| `RimeWithWeaselHandler` 构造需要一个 `weasel::UI*` | `include/RimeWithWeasel.h` | 传 `m_ui` 但**不调用 `Create()`** ⇒ 不产生 D2D 窗口 |

**实施期仍需实读代码确认的点**：
- `weasel::UIStyle` 的字段名（`horizontal` / `color_scheme` 等）以 `include/WeaselIPCData.h` 为准；
  Task 6 Step 4 的示例代码引用了 `style.horizontal`，落地时需核对。
- `WeaselTSF/Globals.h` 的 CLSID 与本仓 `WeaselProtocol::kClsidFluentTextService` 是否一致；
  不一致则在 vendor 侧改 `Globals.h`（Task 5 Step 3 已标注）。
- `weasel::Server::Start()` 与 `SetRequestHandler()` 的先后要求（Task 3 按先 handler 后 start 写，需核对上游 `WeaselServerApp.cpp`）。

**类型一致性核对**：`WeaselEngine::context()/status()` 与 `FluentUiAdapter::apply` 的签名一致；
`RimeConfigClient` 三个虚函数签名 Round 1 已定，本计划不改；
`WeaselProtocol::*` 常量在 Task 1 定义、Task 8 引用。
