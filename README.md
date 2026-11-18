# Weasel Fluent (小狼毫 Fluent UI 现代输入法前端)

[![Language](https://img.shields.io/badge/C%2B%2B-17-blue.svg)](https://en.cppreference.com/w/cpp/17)
[![GUI](https://img.shields.io/badge/Qt-6.8%2B-green.svg)](https://www.qt.io/)
[![Build](https://img.shields.io/badge/Build-xmake-red.svg)](https://xmake.io/)
[![Platform](https://img.shields.io/badge/Platform-Windows%2011%20x64-0078D7.svg)](https://www.microsoft.com/windows)
[![License](https://img.shields.io/badge/License-GPLv3-brightgreen.svg)](LICENSE)

基于 **C++17** 与 **Qt6 / Fluent-Qt** 深度构建的 Windows 11 **Fluent 2** 风格现代输入法前端与独立服务。

**项目定位：小狼毫（Weasel）输入引擎基座 + Fluent 设计语言的现代化前端。**
输入引擎、TSF 注册、部署逻辑与配置模型继承自官方 Weasel；本项目负责把它们的老式
Win32 界面（`WeaselDeployer` 对话框、`WeaselTrayIcon` 托盘）重写为 Fluent 界面，
并保留 Fluent 候选窗、表情面板与扩展面板作为视觉主体。

深度挂接 Librime 64 位核心引擎（`rime.dll`）与 Windows 11 TSF 系统输入法架构，彻底解决原版小狼毫界面老旧、高分屏模糊、容易假死与无法自适应 Win11 原生亚克力磨砂质感的历史痛点。

---

## 视觉展示 (Showcase)

| 核心界面 | 预览截图 |
| :--- | :--- |
| **经典横排胶囊模式**<br>· 动态高对比度 Accent 强调色药丸<br>· 原生 ClearType 次像素渲染<br>· 翻页箭头与设置快捷入口 | ![横排候选条](assets/screenshots/cand_win.png) |
| **4 行多候选网格大矩阵**<br>· 单击展开按钮或按 `Tab` 平滑展开<br>· 单屏同时呈现多达 20 个候选词<br>· 自动自适应行高，底部居中翻页 | ![多行大矩阵](assets/screenshots/cand_matrix_expanded.png) |
| **经典纵向瀑布流模式**<br>· 纵向无多余展开栏，紧凑清晰<br>· 极简底栏翻页与设置滑块按钮 | ![纵向候选条](assets/screenshots/cand_win_vertical.png) |
| **WinUI Gallery 独立设置中心**<br>· 5 大分类（外观、排版、按键、方案维护、关于）<br>· 平滑弹性导航滑块，配置即时原子落盘 | ![设置中心](assets/screenshots/settings_win.png) |
| **Fluent 质感表情选择器**<br>· Emoji、颜文字（Kaomoji）、特殊符号三合一<br>· 24 键自适应网格，点击秒上屏 | ![表情选择器](assets/screenshots/emoji_picker.png) |
| **扩展面板抽屉**<br>· 文本工具、常用短语、大小写命名转换<br>· 16 位强密码生成器与大写金额转换 | ![扩展工具抽屉](assets/screenshots/cand_extension_panel.png) |

---

## 性能基准压测对比 (Benchmark)

我们在相同硬件环境下，对原生 Rust (Direct2D/DirectWrite) 与本 C++ (Qt6 + Fluent-Qt) 进行了 5 个维度的极限压力测试：

| 测试维度 | Rust (Native Win32 + Direct2D) | C++ (Qt6 + Fluent-Qt) | 性能对比分析 |
| :--- | :--- | :--- | :--- |
| **平均击键响应 (Mean)** | 4.14 ms (4,140 µs) | **2.55 ms (2,549 µs)** | ⚡ **C++ 快 38.4%**（直接调用，方差更稳） |
| **中位数击键延迟 (p50)** | 3.13 ms (3,130 µs) | **2.48 ms (2,475 µs)** | ⚡ **C++ 快 20.8%**（远低于人类可感知 10ms 阈值） |
| **95 分位击键延迟 (p95)**| 10.13 ms (10,130 µs) | **3.59 ms (3,588 µs)** | ⚡ **C++ 延迟更平稳，几乎无毛刺波峰** |
| **单帧渲染耗时 (p50)** | **0.24 ms** (237 µs) | 0.81 ms (808 µs) | Direct2D 纯硬件光栅化略胜，但**均小于 1ms** |
| **理论极限渲染帧率** | 1,938 FPS | 1,150 FPS | 均已突破千帧，Qt6 仅占用 60Hz 帧预算的 5.2% |
| **极限连续吞吐量** | 400 键/秒 (1200键/3.00s) | **446 键/秒 (1200键/2.69s)** | ⚡ **C++ 吞吐量领先 11.5%** |
| **主题热切换耗时** | **0.17 ms** (Direct2D 换笔刷) | 5.27 ms (重刷 Fluent QSS 样式表) | 均在毫秒级无感切换 |
| **物理内存占用 (Working Set)** | **72.19 MB** | 127.47 MB | Rust 无框架底噪更小；Qt6 约 127MB（远优于 Electron 动辄 300MB+） |
| **专用提交内存 (Commit Total)** | 62.70 MB | **57.70 MB** | ⚡ **C++ 专用页文件提交更紧凑** |

---

## 核心特性 (Features)

1. **真正的 Windows TSF 输入法前端**：
   - 直接采用官方 **小狼毫（Weasel）** 的 TSF 实现作为内核基座（`3rdparty/weasel/`，GPLv3），
     组合态、内联预编辑、语言栏、显示属性、Compartment 全部由上游负责，输入链路是
     **系统原生的 TSF 管线**，不是键盘钩子；
   - 候选窗仍然是本仓的 Fluent 实现：上游的 Direct2D 面板被编译掉（vendor 补丁 0001），
     服务端把组合态映射过来渲染，**任何时候只有一套候选窗**；
   - 光标定位由 TIP 的 `ITfContext` 提供，候选窗精准跟随输入位置；
   - 选词与上屏经 TSF 上下文完成，穿透所有 Windows 11 应用。

1b. **语言栏（输入法工具栏）直通本仓程序**：
   - 语言栏不是独立窗口，而是 TIP 的一部分：图标与右键菜单从 TIP DLL 的资源段加载，
     菜单命令经 Weasel IPC 命名管道送到**我们的服务端**执行；
   - 服务端注册了全部 `ID_WEASELTRAY_*` 命令（部署 / 设置 / 词典管理 / 打开用户目录 /
     日志目录 / 同步用户数据 / 重新部署 / 退出…），**点语言栏菜单 = 操作我们的程序**，
     不是官方 WeaselServer；
   - 左键点图标切中/英走同一条管道，端到端已验证：切完后从引擎状态读回
     `status.ascii_mode`（0 → 1 → 0），见 `scripts/test_langbar_ipc.ps1`；
   - 图标是本仓自己的 **Fluent 版**（`assets/langbar/`，由
     `scripts/gen-langbar-icons.py` 生成）：圆角方块 + 纯色底 + 白色粗体字形，
     与托盘图标同一套画法，八种分辨率里最关键的 16px 也清晰可读；
   - 菜单文案是**简体中文**（上游那份是繁体），用词与设置中心 / 候选窗菜单一致；
   - 前提：Windows 11 需在输入法的「选项」里勾选「使用桌面语言栏」，
     否则系统只显示现代输入法指示器（那不是 TSF 语言栏）。

2. **自适应 Windows 11 调色板 (`AccentPalette`)**：
   - 彻底修复了原版获取暗红/猪肝红底色的缺陷；
   - 深色模式下严格提取 Windows 11 `AccentLight2` (`#ED958A`) 高对比度亮色，浅色模式下提取深层对比色，通透温润且对比度达到 7.5:1。

3. **双形态自适应候选矩阵**：
   - **紧凑单行模式**：不干扰视线，占用屏幕面积最小；
   - **4 行网格大矩阵**：点击展开按钮或按 `Tab` 键即可纵向延展为 20 词网格，支持鼠标点选与序号秒选（1~9, 0, a~j）。

4. **独立 WinUI Gallery 标准设置中心**：
   - 独立进程唤起，绝不阻塞打字渲染管线；
   - 弹性滑动选择指示器（Fluid Elastic Indicator），五大分页模块对齐微软官方控件库规范；
   - **方案选择、自定义词库、自定义皮肤、自定义快捷键**四项均为真功能，改动会触发真正的全量重新编译。

5. **免提权安装，一键接管官方 Weasel (`scripts/install.ps1`)**：
   - 每用户安装到 `%LOCALAPPDATA%\Programs\WeaselFluent`，**不需要管理员**；
   - 靠 **HKCU CLSID 影子注册**顶掉机器上已装的官方 Weasel：COM 解析
     `HKCU\Software\Classes\CLSID` 优先于 `HKLM`，语言列表里原有的条目直接生效；
   - 卸载即恢复官方 Weasel（删掉 HKCU 那一份即可）；
   - 发行包内置 64 位 `rime.dll` 与 Rime 基础数据，开箱即用；
   - 托盘只负责「状态 / 部署 / 设置 / 退出」，输入法选择交给系统语言切换（`Win+Space`）。

---

## 快速使用 (Quick Start)

### 方式一：直接运行预编译打包发行包 (推荐)
1. 在 [Releases](https://github.com/bpmstall/weasel-fluent/releases) 下载最新 `weasel-fluent-v1.0.0-windows-x64.zip`；
2. 解压到任意目录（例如 `D:\weasel-fluent\`）；
3. 双击运行 `install_and_run.bat` —— 它会调用 `install.ps1`：
   停掉官方 `WeaselServer.exe`（它占着用户词库的锁）→ 把 payload 复制到
   `%LOCALAPPDATA%\Programs\WeaselFluent` → 注册本仓的 TIP DLL
   （**HKCU 影子注册**，免管理员，顶掉官方 Weasel 的 CLSID）→ 写自启项 → 启动服务；
4. 任务栏右下角托盘区将出现输入法托盘图标，在任意软件中打字即可体验；
5. 按 `Shift` 切换中英，按 `Tab` 展开多候选大矩阵，右键托盘图标可进入“设置中心”。

> 卸载：运行发行包里的 `uninstall.ps1`（或
> `powershell -ExecutionPolicy Bypass -File .\scripts\uninstall.ps1`）。
> 删掉 HKCU 的影子注册后，**官方 Weasel 会立刻恢复**；用户数据 `%APPDATA%\Rime`
> 默认保留，加 `-Purge` 才一并清除。

### 方式二：从源码编译构建

本工程基于轻量高效的现代 C/C++ 构建工具 **[xmake](https://xmake.io)**：

```powershell
# 1. 克隆代码仓库
git clone https://github.com/KevinT742/weasel-fluent.git
cd weasel-fluent

# 2. 准备构建依赖
#    - Qt 6.8.2 (msvc2022_64)：xmake 会自动在 C:\Qt\6.8.2 下探测
#    - librime 运行时与 Rime 基础数据（rime-prelude / rime-luna-pinyin）
powershell -ExecutionPolicy Bypass -File .\scripts\fetch_rime.ps1

# 3. 编译 Release 版本（rime.dll 与 data/ 会自动就位到输出目录）
xmake build

# 4. 运行全部自检（路径红线 / 词库残留 / 许可证 / TIP DLL / 17 项单元集成测试）
powershell -ExecutionPolicy Bypass -File .\scripts\run-selfchecks.ps1

# 5. 运行性能基准测试
.\build\Release\weasel-fluent-cpp-qt.exe --benchmark

# 6. 打包前后端完整发行包
powershell -ExecutionPolicy Bypass -File .\scripts\package_dist.ps1
```

> **关于 Boost**：仓库内置了官方 Weasel 的 IPC / 配置层源码（`3rdparty/weasel/`，GPLv3）。
> 编译这部分需要 Boost 1.88：`powershell -ExecutionPolicy Bypass -File .\scripts\build_boost_libs.ps1`
> （Boost 1.88 的 b2 会静默跳过 `thread` 等组件，所以脚本直接调 `cl.exe` 编）。
> 编译产物还要应用上游补丁：`powershell -ExecutionPolicy Bypass -File .\scripts\apply-weasel-patches.ps1`
> （幂等；补丁清单见 `3rdparty/weasel/UPSTREAM.md`）。
>
> **关于词库**：发行包不内置第三方大词库，默认方案为 librime 自带的**朙月拼音**
> （`luna_pinyin`）。需要更大的词库时，在设置中心「词库与维护 → 自定义词库」里
> 导入 `.dict` / `.dict.yaml` 即可。

---

## 设置中心能力 (Settings Center)

| 页面 | 能力 |
| :--- | :--- |
| 个性化与排版 | 候选窗排版、字体、主题、背景材质、强调色、**自定义皮肤**（Fluent token 覆盖文件导入/导出/复位）、**Weasel 状态栏样式**（引擎侧 `weasel.yaml`：字体 / 字号 / 候选横竖排 / 行内预编辑，走 Weasel IPC 读取与部署） |
| 输入与按键 | 空格上屏、音效、混输空格、**自定义快捷键**（中英切换 / 上屏 / 翻页 / 展开矩阵 / 表情面板 / 扩展面板，逐项录制） |
| 词库与维护 | Rime 引擎状态、用户词库目录、**自定义词库**（导入 / 移除并重新编译）、**输入法方案选择**（枚举真实 `*.schema.yaml` 并触发全量部署）、词库备份 |
| 关于与系统 | 版本、技术栈、恢复默认、配置目录 |

**方案切换是真正的重新编译**：`RimeStartMaintenance(RimeMaintenanceTypeFullDeploy)` +
`JoinMaintenanceThread`，而不是只重载旧产物 —— 改方案或导入词库后会立即生效。

---

## 命令行参数一览

| 参数 | 说明 | 示例 |
| :--- | :--- | :--- |
| `(无参数)` | 以系统后台输入法服务模式运行，驻留托盘并接管打字 | `.\weasel-fluent-cpp-qt.exe` |
| `--settings` / `-s` | 独立打开 WinUI 风格设置中心 | `.\weasel-fluent-cpp-qt.exe --settings` |
| `--demo` | 静态预览模式（固定呈现“你好”候选条供调试） | `.\weasel-fluent-cpp-qt.exe --demo` |
| `--verify` | 运行内置 17 项单元与集成全链路测试套件 | `.\weasel-fluent-cpp-qt.exe --verify` |
| `--benchmark` / `--perf` | 运行平均击键延迟、渲染吞吐与内存分析测试 | `.\weasel-fluent-cpp-qt.exe --benchmark` |
| `--capture` | 自动捕捉候选窗与设置中心全套实物截图 | `.\weasel-fluent-cpp-qt.exe --capture` |
| `--theme <mode>` | 强制设置界面主题：`light`, `dark`, 或 `system` | `.\weasel-fluent-cpp-qt.exe --theme dark` |

---

## 架构一览 (Architecture)

```
   应用进程 (记事本 / Chrome / VS Code …)
            │  TSF 原生管线（ITfContext / ITfKeyEventSink）
            ▼
   ┌──────────────────────────────┐
   │  weasel-fluent-tsf.dll       │  ← 上游 WeaselTSF（vendor 补丁 0001 去掉 D2D 面板）
   │  内联预编辑 / 语言栏 / Compartment │
   └──────────────┬───────────────┘
                  │  Weasel IPC（命名管道 \\.\pipe\<用户>\WeaselFluentNamedPipe）
                  ▼
   ┌──────────────────────────────┐
   │  weasel-fluent-cpp-qt.exe    │  ← 服务进程
   │  ├ WeaselEngine              │    上游 RimeWithWeaselHandler（librime）
   │  ├ FluentUiAdapter           │    weasel::Context/Status → RimeUiState
   │  └ CandidateWindow (Fluent)  │    唯一的可见候选窗
   └──────────────┬───────────────┘
                  │  Weasel IPC
                  ▼
   ┌──────────────────────────────┐
   │  weasel-fluent-cpp-qt.exe    │  ← 设置中心（独立进程）
   │  WeaselConfigClient          │    部署 / 样式走 IPC，方案与词库走文件系统
   └──────────────────────────────┘
```

> 服务端的 `weasel::Context` 由 vendor 补丁 0005 填充 —— 上游的候选列表在 TIP 进程里，
> 而 Fluent 候选窗在服务端进程里，所以服务端必须自己取一次组合态（详见
> `docs/superpowers/specs/2026-11-16-weasel-core-merge-modern-ui-design.md` §4.3）。

---

## 开源致谢 (Acknowledgements)

* **[RIME / 中州韵输入法引擎](https://rime.im/)**: 聪慧的跨平台输入法核心引擎。
* **[Weasel / 小狼毫](https://github.com/rime/weasel)**: 成熟的 Windows TSF 输入法核心实现 —— **本项目的引擎基座**，其 `include/`、`WeaselIPC/`、`WeaselIPCServer/`、`RimeWithWeasel/` 已 vendor 至 `3rdparty/weasel/`（上游 commit 见 `3rdparty/weasel/UPSTREAM.md`）。
* **[rime-prelude](https://github.com/rime/rime-prelude)** / **[rime-luna-pinyin](https://github.com/rime/rime-luna-pinyin)**: librime 自带的基础数据与朙月拼音方案。
* **[Fluent-Qt](https://github.com/CalvinHxx/Fluent-Qt)**: 优雅的 Qt 现代 Fluent 风格控件库。
* **[xmake](https://xmake.io/)**: 现代化极速 C/C++ 构建工具。

---

## 许可证 (License)

本项目基于 [GNU GPL v3](LICENSE) 开源发布 —— 因为内置了官方 Weasel（GPLv3）的输入法内核源码，衍生作品必须沿用 GPLv3。第三方组件清单见 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。
