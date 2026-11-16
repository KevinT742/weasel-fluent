# Weasel 内核合并 + Fluent 菜单现代化 · 设计文档

- 日期：2026-11-16
- 仓库：`KevinT742/weasel-fluent`
- 状态：已批准（用户确认许可证改 GPLv3、方案与两轮划分）
- 依据：`设计.md`（仓库根目录）六项需求 + 官方 Weasel（`rime/weasel`）源码勘察

---

## 1. 目标与非目标

### 1.1 目标

把 `设计.md` 的六项需求全部落地为**真实可用**的功能（而非当前的 UI 外壳），并且：

- 以**官方 Weasel 的输入法实现**为内核底座，不再维护一套平行的精简 TSF 实现；
- 把 Weasel 的老式 Win32 菜单（`WeaselDeployer` 四个对话框、`WeaselTrayIcon` 托盘）替换为 **Qt6 / Fluent-Qt 现代化界面**；
- 保留本项目已有的 Fluent 候选窗、表情面板、扩展面板作为视觉主体。

### 1.2 非目标

- 不重写 librime；不引入新的拼音引擎或词库算法。
- 不实现 TSF 的 `ITfComposition` 内联预编辑（推迟至 Round 2，见 §7）。
- 不引入 Rime 之外的插件体系。

### 1.3 术语

| 术语 | 含义 |
| :--- | :--- |
| Weasel 内核 | 官方 `rime/weasel` 的 `include/`、`WeaselIPC/`、`WeaselIPCServer/`、`RimeWithWeasel/`、`WeaselTSF/` |
| Fluent 前端 | 本仓 `src/` 下的 Qt6/Fluent-Qt 界面层 |
| 服务 | 本仓 `weasel-fluent-cpp-qt.exe` 以服务模式运行的进程，宿主 Weasel 内核 |
| 设置中心 | 本仓 `weasel-fluent-cpp-qt.exe --settings` 独立进程 |

---

## 2. 现状基线（勘察结论，可逐条核对）

### 2.1 现有进程模型

```
weasel-fluent-cpp-qt.exe
  ├ 服务模式（默认）        : RimeEngine + CandidateWindow + IpcServer(命名管道) + QSystemTrayIcon
  └ 设置中心模式(--settings) : 独立进程，改 config.json；候选窗用 QFileSystemWatcher 热加载

weasel-fluent-tsf.dll        : TSF COM TIP（当前为精简实现）
  └ 经 WeaselFluentNamedPipe 与服务通信
```

### 2.2 `设计.md` 六项 vs 现状

| # | 需求 | 现状判定 | 证据 |
| :-- | :--- | :--- | :-- |
| 1 | 去除 rime-ice | **待做**：6 处硬编码默认方案 | `config.h:117`、`rime_engine.h:195`、`settings_window.cpp:1477,1715,1914`、`candidate_window.cpp:1604` |
| 2 | 选择方案并部署 | **假功能**：下拉为 4 条写死文案；`onSchemaChanged` 只写 config 并调 `deploy()`，从不枚举方案、不写 `schema_list`、不调 `selectSchema()` | `settings_window.cpp:1475-1495, 1714-1722` |
| 3 | 添加自定义词库 | **缺失**：仅有"备份词库" | `settings_window.cpp:1497-1519` |
| 4 | 添加自定义皮肤 | **缺失**：仅 4 个硬编码预设 | `settings_window.cpp:1796-1816` |
| 5 | 添加自定义快捷键 | **缺失**：仅固定下拉（Shift/Ctrl/None、4 组翻页键） | `settings_window.cpp:1336-1379` |
| 6 | 自动替换旧的服务 | **缺失**：安装脚本仅 `taskkill` | `scripts/package_dist.ps1:91` |
| 7 | 替换托盘方案，用系统输入法切换 | **部分**：TSF 已就位且钩子已改为 `--hook` 可选，但托盘仍常驻并承担中英切换 | `src/main.cpp:893-954` |

### 2.3 阻塞性缺陷（必须先修）

| 缺陷 | 位置 | 后果 |
| :--- | :--- | :-- |
| 硬编码 AI 草稿路径 | `scripts/package_dist.ps1:3`、`tsf/ipc_client.cpp:26` | `C:\Users\zheng\.gemini\antigravity\scratch\...` — 换机即失效 |
| 硬编码开发者用户名 | `src/settings_window.cpp:1440,1447,1525,1725` | `C:/Users/zheng/AppData/Roaming/Rime` |
| 硬编码旧版安装路径 | `src/rime_engine.cpp:27,36` | `C:/Program Files/Rime/weasel-0.17.4/{rime.dll,data}` |
| `deploy()` 不重建 | `src/rime_engine.cpp:344-349` | 仅调 `RimeDeployWorkspace`（重读已编译产物），**不重编译**；`m_fnStartMaintenance`/`m_fnJoinMaintenanceThread` 已在 `rime_engine.h:145-146` 声明但从未用于全量重建 |
| 未使用的 QtSvg 链接 | `xmake.lua:52` | `add_frameworks(..., "QtSvg")` 但全仓无 `QSvg` 引用，易在缺模块环境断链 |

### 2.4 已有资产（可复用，勿重写）

| 资产 | 位置 | 用途 |
| :--- | :--- | :-- |
| `RimeStartMaintenance` / `RimeJoinMaintenanceThread` | `src/rime_engine.h:145-146` | 全量部署所需，已解析符号 |
| `RimeSelectSchema` | `src/rime_engine.h:161` | 切换方案所需，已解析符号 |
| Fluent token 覆盖体系 | `3rdparty/Fluent-Qt/src/components/foundation/UserTheme.h` | `applyOverrides(QJsonObject)` / `filePath()` / `exportTemplate()` — **原生皮肤钩子** |
| HKCU CLSID 影子注册 | `tsf/register.cpp:37-43` | 已用本仓 CLSID 覆盖官方 Weasel CLSID `{A3F4CDED-…}`；COM 解析 HKCU 优先于 HKLM → **免提权替换旧服务**的现成机制 |
| 托盘图标绘制 | `src/main.cpp:906-923` | 现成绘制逻辑，可换成 Fluent 菜单 |

---

## 3. 许可证与合规

- 官方 Weasel 为 **GPLv3**（`LICENSE.txt`）。本仓原为 **MIT**。
- **决策（用户已确认）**：本仓改为 **GPLv3**，保留 Weasel / librime / Fluent-Qt 署名。
- 落地动作：
  - `LICENSE` 替换为 GPLv3 全文；
  - 新增 `THIRD_PARTY_NOTICES.md`，列明 Weasel（GPLv3）、librime（GPLv3）、Fluent-Qt（MIT）、Boost（BSL-1.0）、Qt（LGPLv3）；
  - `README.md` 与 `settings_window.cpp` 的"开源组件"卡片同步更新；
  - vendor 目录保留上游 `LICENSE.txt` 与 commit 号（见 §5.1）。

---

## 4. 目标架构

### 4.1 模块图

```
┌─ Windows 应用进程 ────────────────────────────┐
│  WeaselTSF (vendored, Round 2 换入)           │
│    CandidateList / Composition / EditSession  │
│    DisplayAttributeProvider / LanguageBar     │
│    Compartment (KEYBOARD_OPENCLOSE)           │
│         │ WeaselIPC (Boost.Serialization)      │
└─────────┼──────────────────────────────────────┘
          │
┌─────────▼──────────────────────────────────────┐
│  本仓 Qt 服务进程                                │
│    WeaselIPCServer  (vendored)  ← 服务端        │
│    RimeWithWeasel   (vendored)  ← librime 封装  │
│    CandidateWindow / EmojiPicker / Extension   │  ← Fluent 保留
│    TrayMenu (Fluent-Qt menus_toolbars)        │  ← 替换 WeaselTrayIcon
└─────────┬──────────────────────────────────────┘
          │ WeaselIPC
┌─────────▼──────────────────────────────────────┐
│  设置中心进程 (--settings)                       │
│    WeaselIPC client                             │
│    Fluent 设置中心 (替换 WeaselDeployer)         │
│      方案与部署 / 自定义词库 / 自定义皮肤 /        │
│      自定义快捷键                                │
└────────────────────────────────────────────────┘
```

### 4.2 模块边界规则（红线）

- **R1** Weasel 内核**不得**依赖 Qt；任何 Qt 依赖只允许存在于 `src/`。
- **R2** Fluent 侧**不得**直接调用 librime C API；一切经 Weasel IPC 客户端。
- **R3** 设置中心**不得**直接写 `%APPDATA%\Rime`；配置变更经 Weasel `Configurator`/`Styler`/`ActionLoader`。
- **R4** 候选窗渲染**不得**同时存在两套实现；同一时刻只有 Weasel D2D 面板或 Fluent Qt 窗口之一。
- **R5** 所有路径必须经统一解析函数；**禁止**出现 `C:/Users/<用户名>` 或绝对盘符字面量（可用 `scripts/verify-no-hardcoded-paths.ps1` 自检）。

### 4.3 与 R2/R4 的张力及裁决

Weasel 的 `CCandidateList` 内部持有 `weasel::UI`（D2D 面板）。本项目要保留 Fluent Qt 候选窗，故：

- Round 1：保留现有精简 TIP 作桥接，不引入 `WeaselUI`，不违反 R4。
- Round 2：换入 `WeaselTSF` 时，**必须**同时提供 `CCandidateList` → Fluent Qt 窗口的桥接实现，删除 `WeaselUI` 依赖。否则 R4 冲突。

---

## 5. Vendor 清单与依赖

### 5.1 Vendor 目录

置于 `3rdparty/weasel/`，保留上游目录名与 `LICENSE.txt`，并在 `3rdparty/weasel/UPSTREAM.md` 记录：仓库 URL、clone 日期、commit SHA、vendored 模块列表、被排除的模块及原因。

| 上游模块 | 采纳 | 说明 |
| :--- | :--- | :-- |
| `include/` | ✅ 全量 | `WeaselIPC.h`、`WeaselIPCData.h`、`WeaselStyleColor.h`、`WeaselStyleKeys.h`、`PipeChannel.h`、`ResponseParser.h`、`WeaselConstants.h`、`WeaselUtility.h`、`wtl/` |
| `WeaselIPC/` | ✅ 全量 | 客户端：`Configurator`、`Styler`、`ActionLoader`、`Committer`、`ContextUpdater`、`ResponseParser`、`PipeChannel` |
| `WeaselIPCServer/` | ✅ 全量 | 服务端：`WeaselServerImpl`、`SecurityAttribute` |
| `RimeWithWeasel/` | ✅ 全量 | librime 封装 + `WeaselUtility` |
| `WeaselTSF/` | 🔶 Round 2 | 完整 TSF；接入前须完成 §4.3 桥接 |
| `WeaselUI/` | ❌ | D2D 面板，被 Fluent Qt 候选窗取代（R4） |
| `WeaselDeployer/` | ❌ | 老 Win32 对话框，被 Fluent 设置中心取代 |
| `WeaselServer/` | ❌ | 老托盘 + 老服务外壳，被本仓服务取代（仅参考 `WeaselTrayIcon` 行为） |
| `WeaselSetup/` | ❌ | Round 2 重写为 `scripts/install.ps1`（§6.6） |

### 5.2 新增构建依赖

| 依赖 | 版本 | 获取方式 | 需要的产物 |
| :--- | :--- | :--- | :-- |
| Boost | 1.84.0 | `archives.boost.io` 源码包 + `b2` | `boost_thread`、`boost_serialization`、`boost_system`（+ 头文件） |
| librime | 1.11.x | `get-rime.ps1`（拉 GitHub release） | `rime.dll` + `data/` |
| ATL | VS 自带 | — | `atls.lib` |
| WTL | 上游内置 | `include/wtl/` | 头文件 |

### 5.3 构建系统调整（`xmake.lua`）

- 新增 target `WeaselIPC` / `WeaselIPCServer` / `RimeWithWeasel`（`static`），源文件取自 `3rdparty/weasel/`。
- 新增全局 `add_includedirs("3rdparty/weasel/include")`；`add_includedirs("3rdparty/weasel/include/wtl")`。
- 新增 `add_links("boost_thread", "boost_serialization", "boost_system")` 与对应 `add_linkdirs`（指向 `deps/boost`）。
- **移除** `add_frameworks("QtSvg")`（§2.3，无引用）。
- 现有 `FluentQt` / `weasel-fluent-cpp-qt` / `weasel-fluent-tsf` target 保持结构不变，Round 2 再调整 TSF target。
- **运行时一致性红线**：Weasel 内核用 `set_runtimes("MT")`（上游要求，避免与 `/MD` 冲突）；本仓 GUI target 需与之对齐或显式隔离，二者不得混链。

---

## 6. `设计.md` 六项需求的逐项设计

### 6.1 删减：去除 rime-ice

- 默认方案 `rime_ice` → **`luna_pinyin`**（朙月拼音，librime 自带，零外部依赖）。
- 改动点：`config.h:117`、`rime_engine.h:195`、`settings_window.cpp:1477,1715,1914`、`candidate_window.cpp:1604`。
- 打包：`package_dist.ps1` **不再**从 `%APPDATA%\Rime` 整目录拷贝（那正是 rime-ice 的所在地），改为只拷贝明确列出的基础文件。
- 文档：`README.md:11,67,122` 与 `settings_window.cpp:638,1417,1446,1596` 中"雾凇拼音/rime-ice"表述全部移除。
- **断言**：全仓 `grep -ri "rime.ice\|雾凇"` 命中数为 0（`scripts/verify-no-rime-ice.ps1` 自检）。

### 6.2 增加：选择方案并部署（真功能）

**方案枚举**（新增 `src/schema_manager.{h,cpp}`）：
- 扫描 `%APPDATA%\Rime\*.schema.yaml` 与 `%APPDATA%\Rime\build\*.schema.yaml`。
- 解析每个文件的 `schema:` 节点取 `name:` 作显示名、`schema_id:` 作 ID。
- 解析采用**最小 YAML 子集读取器**（只认 `schema:` 下的 `name:`/`schema_id:` 两级缩进），不引入 YAML 库。
- 区分"已部署"(build/ 内) 与"仅源文件"，UI 上分别标注。

**切换动作**（按序）：
1. 写 `%APPDATA%\Rime\default.custom.yaml`：
   ```yaml
   var:
     previously_selected_schema: <schema_id>
   ```
2. 写/更新 `%APPDATA%\Rime\default.yaml` 的 `schema_list`（保持既有项顺序，替换目标项）。
3. **全量重建**：`RimeStartMaintenance(RimeMaintenanceTypeFullDeploy = 1)` → `RimeJoinMaintenanceThread()`。
4. `RimeSelectSchema(session, schema_id)`（经 Weasel `Configurator`）。
5. 更新 `config.json` 的 `schema` 字段。

**`RimeEngine::deploy()` 升级**为两级语义（现仅 `RimeDeployWorkspace`）：
- `deployLight()` — 仅重载已编译产物（`RimeDeployWorkspace`），用于配置热更新。
- `deployFull()` — `RimeStartMaintenance(1)` + `Join`，用于方案切换/词库变更。

**验收断言**：切换到非默认方案后，`m_currentSchema` 与 `default.custom.yaml` 中的 `previously_selected_schema` 一致；`build/<schema_id>.schema.yaml` 的 mtime 发生变化。

### 6.3 增加：自定义词库

- 入口：设置中心「词库与维护」页 → 导入按钮（文件对话框，`*.dict` / `*.dict.yaml`）。
- 落地：复制到 `%APPDATA%\Rime/<name>.dict.yaml`。
- 挂载：把 `<name>` 追加进当前方案 `default.custom.yaml` 的 `dictionary:` 列表（保留既有项，去重）。
- 触发：`deployFull()`。
- 管理：列表展示已挂载的自定义词库，可移除（同样改写 `dictionary:` 后重建）。
- **红线**：不覆盖同名文件，除非用户显式确认；导入前校验首行是否为 `---` / 词条含制表符分隔，否则报错拒绝。

### 6.4 增加：自定义皮肤

**双皮肤体系并存，职责不重叠**：

| 层 | 皮肤对象 | 机制 |
| :--- | :--- | :--- |
| Fluent 界面层（候选窗/设置中心/托盘） | Fluent token | `fluent::UserTheme::applyOverrides(QJsonObject)` + `UserTheme::filePath()` |
| Weasel 内核层（若 Round 2 启用 D2D 面板） | Weasel style | `WeaselIPC/Styler` + `WeaselStyleColor.h` / `WeaselStyleKeys.h` |

- Round 1 实现 Fluent token 层：导入 skin JSON → 写入 `UserTheme::filePath()` → `UserTheme::apply()`；提供"导出模板"（`UserTheme::exportTemplate(true)`）与"恢复默认"。
- 皮肤 JSON 结构遵循 Fluent-Qt 自带 schema（含 `schema` 版本号），非法字段整体拒绝（`applyOverrides` 语义：无效则拒绝整次更新）。
- 追加 `config.json` 的 `skin_file` 字段，支持多皮肤文件切换。

### 6.5 增加：自定义快捷键

- **绑定表**：`config.json` 新增 `key_bindings`，形如 `{ "<action>": "<key_spec>" }`。
- **动作集**（可绑定）：
  | action | 默认 | 生效点 |
  | :--- | :--- | :--- |
  | `toggle_ascii` | `Shift` | 中英切换（原 `switchKey`） |
  | `commit` | `Space` | 上屏首选词 |
  | `page_prev` | `,` | 候选翻页（前） |
  | `page_next` | `.` | 候选翻页（后） |
  | `expand_matrix` | `Tab` | 展开 4 行矩阵 |
  | `emoji_panel` | — | 打开表情面板 |
  | `extension_panel` | — | 打开扩展面板 |
- **迁移**：旧的 `switchKey` / `pageKeys` 预设值在加载时映射为新表的初值，读取一次后写回新格式（旧键保留 1 个版本周期）。
- **UI**：每个动作一行 + 「录制」按钮 → 弹出模态捕获对话框（`QKeyEvent` 捕获组合键），显示为人类可读串（`Ctrl+Shift+4`）。
- **生效**：键位分派集中在 `CandidateWindow` / `InputHook` / `IpcServer` 的按键入口，禁止散落硬编码判断。

### 6.6 优化：自动替换旧的服务（Round 2 落地）

`scripts/install.ps1`（每用户，免管理员，目标 `%LOCALAPPDATA%\Programs\WeaselFluent`）：

1. 停旧进程：`WeaselServer.exe`、旧 `weasel-fluent-cpp-qt.exe`。
2. 反注册官方 Weasel TIP（若存在，`regsvr32 /u`），清 `HKLM\SOFTWARE\Microsoft\CTF\TIP\{A3F4CDED-…}`（若可写）。
3. 复制 payload：`weasel-fluent-cpp-qt.exe`、`weasel-fluent-tsf.dll`、`rime.dll`、`data/`。
4. `regsvr32` 注册本仓 TSF DLL（HKCU，免提权）— 依赖 `tsf/register.cpp` 现有的 **HKCU CLSID 影子注册**（覆盖官方 Weasel CLSID）。
5. 把 `0804:{D7B1C618-9F3E-4E7B-B96E-5C13D1A5B4C2}{E8934A01-C349-4F8A-8512-14AE0293BD01}` 加入用户语言列表（`InstallLayoutOrTip`）。
6. 写 `HKCU\...\Run\WeaselFluent` 自启。
7. **幂等**：可重复执行，每步先检测后动作。

`scripts/uninstall.ps1`：泛化现有 `scripts/restore_official_weasel.ps1`（去除硬编码路径），补齐语言列表还原。

### 6.7 优化：替换托盘方案，使用系统输入法切换

- **托盘职责收缩**：不再是切换入口，只保留「当前模式状态指示 / 重新部署 / 设置中心 / 关于 / 退出」。
- **切换归属系统**：
  - 输入法选择 → Windows 自带 `Win+Space`（本仓 TSF 已注册进语言列表，§6.6 步骤 5）。
  - 中↔英 → `Shift`，状态经 TIP 的 `GUID_COMPARTMENT_KEYBOARD_OPENCLOSE` 同步给系统语言栏（机制已存在于 `tsf/weasel_tsf.cpp:191-205`），Windows 显示正确的模式指示。
- **菜单现代化**：用 Fluent-Qt `menus_toolbars` 组件重写（当前 `src/main.cpp:926-941` 是原生 `QMenu`），保持 Fluent 视觉语言与键盘可达性。
- **可选隐藏**：`config.json` 增加 `show_tray`（默认 `true`），允许纯系统切换的用户彻底隐藏托盘。

---

## 7. 轮次划分

### Round 1 — 功能先行（本次交付）

| 序 | 内容 | 依赖 | 验收 |
| :-- | :--- | :--- | :-- |
| 1.1 | 工具链与依赖就位：Boost 1.84 编译（3 库）、`get-rime.ps1` 拉 librime | 网络 | `boost_thread.lib` + `rime.dll` 存在 |
| 1.2 | vendor `include/`、`WeaselIPC/`、`WeaselIPCServer/`、`RimeWithWeasel/`；xmake 新增 3 个 static target | 1.1 | `xmake build` 通过 |
| 1.3 | 路径治理：新增 `src/paths.{h,cpp}` 单一真源；清除全部硬编码路径 | — | `scripts/verify-no-hardcoded-paths.ps1` 通过 |
| 1.4 | 去除 rime-ice（§6.1） | — | `scripts/verify-no-rime-ice.ps1` 通过 |
| 1.5 | `RimeEngine::deployFull()`（§6.2） | 1.2 | 全量重建后 `build/` mtime 变化 |
| 1.6 | 设置中心：方案选择&部署 / 自定义词库 / 自定义皮肤 / 自定义快捷键（§6.2–6.5） | 1.5 | 4 项功能可操作且落盘生效 |
| 1.7 | Fluent 托盘菜单 + `show_tray` 开关（§6.7） | — | 托盘菜单渲染正常 |
| 1.8 | 许可证与致谢改 GPLv3（§3） | — | `LICENSE` 为 GPLv3 全文 |

### Round 2 — 内核与发行

| 序 | 内容 | 验收 |
| :-- | :--- | :-- |
| 2.1 | 桥接 `CCandidateList` → Fluent Qt 候选窗（§4.3） | 候选窗不再重复出现（R4） |
| 2.2 | 换入 `WeaselTSF`（内联预编辑 / 语言栏 / 显示属性） | 系统语言栏显示正确模式 |
| 2.3 | `scripts/install.ps1` / `uninstall.ps1`（§6.6） | 在装有官方 Weasel 的机器上一键替换成功 |
| 2.4 | `package_dist.ps1` 去硬编码、产出完整发行包 | 换机可跑 |

---

## 8. 测试与验收

### 8.1 编译期

- `xmake build -a` 全量通过（含 3 个新 static target）。
- `xmake build -a mode=debug` 通过。

### 8.2 运行时自检（沿用现有 CLI）

| 命令 | 用途 | 扩展 |
| :--- | :--- | :-- |
| `--verify` | 现有 9 项单元/集成测试 | **新增**：路径解析、方案枚举、词库挂载、皮肤覆盖、键位解析共 5 项 |
| `--demo` | 静态候选条预览 | 新增皮肤切换演示 |
| `--capture` | 截图 | 新增设置中心四页截图 |

### 8.3 脚本级自检（新增 `scripts/verify-*.ps1`）

| 脚本 | 断言 |
| :--- | :-- |
| `verify-no-hardcoded-paths.ps1` | 无 `C:/Users/<name>`、无 `C:/Program Files/Rime` 字面量（R5） |
| `verify-no-rime-ice.ps1` | 无 `rime_ice` / `rime-ice` / `雾凇`（§6.1） |
| `verify-license.ps1` | `LICENSE` 为 GPLv3 全文，`THIRD_PARTY_NOTICES.md` 存在且含 Weasel 条目 |

### 8.4 手动验收清单

1. 全新机器（无 Rime）→ 运行 `install_and_run.bat` → 服务起来 → 任意应用可打拼音。
2. 设置中心切方案 → 候选随之改变 → 重启后仍是新方案。
3. 导入自定义词库 → 输入词库内词 → 候选首位命中。
4. 导入皮肤 → 候选窗与设置中心配色即时变化 → 重启仍生效。
5. 把"展开矩阵"改绑到 `Ctrl+M` → 按 `Ctrl+M` 展开矩阵 → 原 `Tab` 不再展开。

---

## 9. 风险登记

| # | 风险 | 影响 | 缓解 |
| :-- | :--- | :-- | :-- |
| 1 | Boost 1.84 编译耗时长 / 可能与 MSVC 14.51 不兼容 | Round 1.1 阻塞 | 备选：Boost 1.87+ 或改用 header-only 子集；必要时先以 `boost::interprocess`+`serialization` 最小集编译 |
| 2 | 上游 `rime/weasel` 为持续开发的主分支，无 release tag | vendor 内容不可复现 | 记录 commit SHA 到 `3rdparty/weasel/UPSTREAM.md`，锁定版本 |
| 3 | Weasel `WeaselIPC` 协议与本仓现有 `IpcKeyRequest/IpcKeyResponse` 不兼容 | 现有 TIP 与服务需同步换 | Round 1 保留两套协议共存（服务同时监听两个管道名），Round 2 统一 |
| 4 | `RimeStartMaintenance` 在服务进程内运行会阻塞 UI（同步 Join） | 候选窗卡顿 | 部署放到独立线程 + 状态回报；设置中心侧显示进度 |
| 5 | 去掉 `rime-ice` 后开箱词量锐减 | 用户体感下降 | 明确 README 指引用「自定义词库」导入；默认 `luna_pinyin` 至少保证可用 |
| 6 | Weasel 内核 `/MT` 与 GUI `/MD` 运行时不一致 | 链接错误 | 统一为 `/MT`（Weasel 上游要求），或把内核编成独立 DLL 隔离 |
| 7 | 合并后 GPLv3 与既有 MIT 发布物不一致 | 下游合规 | 已在 §3 决策；README 与致谢同步 |

---

## 10. 附录：勘察命令与事实来源

```powershell
# 本仓现状
git log --oneline -20
Select-String -Path src/*.cpp,src/*.h -Pattern 'rime-ice|雾凇'

# 官方 Weasel
git clone --depth 1 https://github.com/rime/weasel.git
Get-ChildItem weasel -Directory        # WeaselTSF / WeaselIPC / WeaselIPCServer / WeaselUI / WeaselDeployer / WeaselServer / WeaselSetup / RimeWithWeasel
Get-Content weasel/LICENSE.txt -TotalCount 6        # GPLv3
Select-String -Path weasel/WeaselUI/WeaselPanel.cpp -Pattern 'WS_POPUP'   # 候选面板为独立弹出窗口
Select-String -Path weasel/WeaselIPC/*.cpp -Pattern 'boost/'              # Boost 依赖
```

**已核实的事实**（本文所有断言的唯一来源）：

- Weasel 的候选面板是独立 `WS_POPUP` 顶层窗口，非应用内原生 UI 元素 → §4.3 可行。
- WeaselTSF 依赖 WeaselIPC + WeaselUI（`weasel/WeaselTSF/xmake.lua`）。
- WeaselIPC 依赖 Boost.Interprocess / Boost.Serialization / Boost.Thread（`include/PipeChannel.h`、`include/WeaselIPCData.h`）。
- 上游 `lib64/` 仅含 `WinSparkle.lib`，librime 需另行获取（`get-rime.ps1`）。
- 上游 `WeaselSetup` 仅在 x86 构建（根 `xmake.lua` 的 `is_arch("x86")` 分支）→ Round 2 必须重写为 x64 可用的 PowerShell 安装器（§6.6）。
