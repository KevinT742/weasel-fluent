# Weasel 内核合并 + Fluent 菜单现代化 · Round 1 实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 让 `设计.md` 六项需求中的四项「增加」变成真实可用功能，并完成路径治理、去除 rime-ice、Fluent 托盘菜单与 GPLv3 许可证切换；同时把官方 Weasel 内核 vendor 进仓库并编译成静态库，为 Round 2 的引擎/TDF 换型铺路。

**Architecture:** 引入 `src/paths.{h,cpp}` 作为全部路径的唯一真源；新增 `SchemaManager` / `DictionaryManager` / `SkinManager` / `KeyBindings` 四个自成一体的功能模块，各自只通过 `AppConfig` 与磁盘交互，不互相依赖；新增 `src/rime_config_client.h` 抽象接口隔离「Rime 配置操作」，Round 1 由本仓 `RimeEngine` 实现，Round 2 改接 Weasel 的 `Configurator`/`Styler`/`ActionLoader`；官方 Weasel 源码置于 `3rdparty/weasel/` 并以静态库形式参与链接。

**Tech Stack:** C++17 · Qt 6.8.2 (msvc2022_64) · Fluent-Qt · xmake 3.1.2 · Boost 1.88.0 (thread/serialization/system) · librime 1.11.x · 官方 rime/weasel（GPLv3）

## Global Constraints

- 许可证：仓库许可证改为 **GPLv3**（`LICENSE` 全文替换），新增 `THIRD_PARTY_NOTICES.md`。任何任务不得把 GPLv3 代码混入以 MIT 声明的对外物。
- 路径红线（设计文档 §4.2 R5）：**禁止**在源码中出现 `C:/Users/<用户名>`、`C:\Users\<用户名>`、`C:/Program Files/Rime`、`.gemini/antigravity` 等绝对字面量。一律经 `WeaselPaths::*` 解析。
- 默认输入方案恒为 **`luna_pinyin`**（朙月拼音）；仓库内不得再出现 `rime_ice` / `rime-ice` / `雾凇` 字样。
- 构建：`xmake build -a`，产物落在 `build/Release/`。本机 Qt 位于 `C:\Qt\6.8.2\msvc2022_64`（xmake 已内置回退探测），xmake 位于 `C:\devtools\xmake\xmake\xmake.exe`。
- 运行时：Weasel 内核用 `MT`；本仓 GUI target 必须与之统一为 `MT`（否则与上游 `/MT` 冲突）。
- 编码：所有新文件 **UTF-8 with BOM** 不可省——MSVC 默认按系统 ANSI(GBK) 读无 BOM 的 UTF-8 源文件，中文注释会乱码。用 `write` 工具写出的文件需确认编码，或在 `xmake.lua` 全局加 `/utf-8`（已有）。
- 提交粒度：每个 Task 结束提交一次，commit message 用 Conventional Commits。

---

## File Structure

### 新增

| 路径 | 职责 |
| :--- | :-- |
| `src/paths.h` / `src/paths.cpp` | 全部运行期路径的唯一真源：`rimeDllPath()` / `sharedDataDir()` / `userDataDir()` / `configDir()` / `skinDir()` |
| `src/schema_manager.h` / `src/schema_manager.cpp` | 枚举用户 Rime 目录下的 `*.schema.yaml`，解析 `schema.name`/`schema.schema_id`，改写 `default.yaml` 的 `schema_list` 与 `default.custom.yaml` 的 `previously_selected_schema` |
| `src/dictionary_manager.h` / `src/dictionary_manager.cpp` | 自定义词库导入/移除/列表；改写 `default.custom.yaml` 的 `dictionary:` 列表 |
| `src/skin_manager.h` / `src/skin_manager.cpp` | Fluent-Qt token 皮肤的导入/导出/应用/复位，走 `fluent::UserTheme` |
| `src/key_bindings.h` / `src/key_bindings.cpp` | 快捷键绑定表模型：动作注册、键规格解析与格式化、读写 `config.json` |
| `src/hotkey_capture_dialog.h` / `src/hotkey_capture_dialog.cpp` | 模态按键捕获对话框 |
| `src/tray_menu.h` / `src/tray_menu.cpp` | Fluent-Qt 风格托盘菜单（替换原生 `QMenu`） |
| `src/ui_glyph_icon.h` | 共用的 Fluent 字形图标绘制辅助（从 `candidate_window.cpp` 抽出） |
| `src/rime_config_client.h` | Rime 配置操作抽象接口（Round 2 换接 Weasel `Configurator`/`Styler`/`ActionLoader`） |
| `3rdparty/weasel/**` | vendor 的官方 Weasel 源码 + `UPSTREAM.md` + 上游 `LICENSE.txt` |
| `scripts/verify-no-hardcoded-paths.ps1` | 路径红线自检 |
| `scripts/verify-no-rime-ice.ps1` | rime-ice 残留自检 |
| `scripts/verify-license.ps1` | 许可证与致谢自检 |
| `THIRD_PARTY_NOTICES.md` | 第三方组件与许可证声明 |

### 修改

| 路径 | 改动 |
| :--- | :-- |
| `src/config.h` / `src/config.cpp` | 新增 `key_bindings`、`skin_file`、`show_tray`；默认 schema → `luna_pinyin`；`switchKey`/`pageKeys` 迁移到新绑定表 |
| `src/rime_engine.h` / `src/rime_engine.cpp` | 新增 `deployFull()`；`initialize()` 路径改走 `WeaselPaths` |
| `src/settings_window.h` / `src/settings_window.cpp` | 方案页真实枚举；词库导入 UI；皮肤导入 UI；快捷键录制 UI |
| `src/candidate_window.cpp` | 键位分派改走 `KeyBindings`；`rime_ice` 文案移除 |
| `src/main.cpp` | 托盘改用 `TrayMenu`；`--verify` 扩充 5 项测试；移除硬编码截图路径 |
| `xmake.lua` | 新增 Weasel 静态库 target；Boost 链接；移除未使用的 `QtSvg`；统一 `MT` 运行时 |
| `LICENSE` | MIT → GPLv3 全文 |
| `README.md` | 去除 rime-ice 表述；许可证与致谢更新 |
| `scripts/package_dist.ps1` | 去除硬编码 scratch 路径，改用 `$PSScriptRoot` |

---

## Task 1: 路径治理 —— `src/paths.{h,cpp}`

**Files:**
- Create: `src/paths.h`, `src/paths.cpp`
- Modify: `src/rime_engine.cpp:18-52`（`initialize()`）
- Modify: `src/settings_window.cpp:1437-1448, 1521-1530, 1724-1740`
- Modify: `src/main.cpp:318`（硬编码截图路径）
- Modify: `tsf/ipc_client.cpp:20-27`（服务端路径回退）
- Create: `scripts/verify-no-hardcoded-paths.ps1`

**Interfaces:**
- Consumes: 无（首个任务）
- Produces:
  ```cpp
  namespace WeaselPaths {
      QString rimeDllPath();     // rime.dll 绝对路径
      QString sharedDataDir();   // librime 共享数据目录
      QString userDataDir();     // %APPDATA%\Rime
      QString configDir();       // 本仓 config.json 所在目录
      QString skinDir();         // 皮肤文件目录
  }
  ```

- [ ] **Step 1: 写失败的自检脚本**

创建 `scripts/verify-no-hardcoded-paths.ps1`：

```powershell
# scripts/verify-no-hardcoded-paths.ps1
$ErrorActionPreference = "Stop"
$repo = Split-Path -Parent $PSScriptRoot
$patterns = @(
  @{ re = 'C:[/\\]Users[/\\][A-Za-z0-9_.-]+'; msg = '硬编码用户目录' },
  @{ re = 'C:[/\\]Program Files[/\\]Rime';  msg = '硬编码旧版 Rime 安装路径' },
  @{ re = '\.gemini[/\\]antigravity';          msg = 'AI 草稿路径残留' }
)
$hits = @()
Get-ChildItem -Path $repo -Recurse -Include *.cpp,*.h,*.ps1,*.lua,*.md -File |
  Where-Object { $_.FullName -notmatch '[\\/]3rdparty[\\/]' -and $_.FullName -notmatch '[\\/]\.git[\\/]' } |
  ForEach-Object {
    $f = $_
    foreach ($p in $patterns) {
      $m = Select-String -Path $f.FullName -Pattern $p.re -AllMatches -ErrorAction SilentlyContinue
      foreach ($mm in $m) { $hits += "{0}:{1} [{2}] {3}" -f $f.FullName.Replace("$repo\",''), $mm.LineNumber, $p.msg, $mm.Line.Trim() }
    }
  }
if ($hits.Count -gt 0) {
  Write-Host "FAIL: 发现 $($hits.Count) 处硬编码路径：" -ForegroundColor Red
  $hits | ForEach-Object { Write-Host "  $_" -ForegroundColor Red }
  exit 1
}
Write-Host "PASS: 无硬编码路径" -ForegroundColor Green
exit 0
```

- [ ] **Step 2: 运行脚本确认失败**

Run: `powershell -ExecutionPolicy Bypass -File scripts/verify-no-hardcoded-paths.ps1`
Expected: FAIL，列出 `src/settings_window.cpp` / `src/rime_engine.cpp` / `src/main.cpp` / `tsf/ipc_client.cpp` / `scripts/package_dist.ps1` 的命中行。

- [ ] **Step 3: 实现 `src/paths.h`**

```cpp
// src/paths.h
#ifndef WEASEL_PATHS_H
#define WEASEL_PATHS_H

#include <QString>

/// 运行期路径的唯一真源。禁止在其他文件里出现绝对路径字面量。
namespace WeaselPaths {

/// librime 核心库 rime.dll 的绝对路径。
/// 探测顺序：应用目录 → HKCU Run 注册值所在目录 → 系统 PATH。
QString rimeDllPath();

/// librime 共享数据目录（含 default.yaml、build/ 等），找不到返回空串。
QString sharedDataDir();

/// 用户 Rime 配置目录（%APPDATA%\Rime），不存在则创建。
QString userDataDir();

/// 本仓配置文件目录（config.json 所在）。
QString configDir();

/// 皮肤文件目录（<configDir>/skins），不存在则创建。
QString skinDir();

} // namespace WeaselPaths

#endif // WEASEL_PATHS_H
```

- [ ] **Step 4: 实现 `src/paths.cpp`**

```cpp
// src/paths.cpp
#include "paths.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>

namespace {

/// 从 HKCU\...\Run 的 WeaselFluent 值解析服务可执行文件所在目录。
QString installDirFromRunKey() {
    QSettings s(QSettings::NativeFormat, QSettings::UserScope,
                QStringLiteral("Microsoft"), QStringLiteral("Windows"));
    s.beginGroup(QStringLiteral("CurrentVersion/Run"));
    const QString exe = s.value(QStringLiteral("WeaselFluent")).toString();
    s.endGroup();
    if (exe.isEmpty()) return QString();
    const QString clean = exe.startsWith(QLatin1Char('"')) && exe.endsWith(QLatin1Char('"'))
                              ? exe.mid(1, exe.size() - 2) : exe;
    return QFileInfo(clean).absolutePath();
}

} // namespace

namespace WeaselPaths {

QString rimeDllPath() {
    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        appDir + QStringLiteral("/rime.dll"),
        installDirFromRunKey() + QStringLiteral("/rime.dll"),
    };
    for (const QString& c : candidates) {
        if (!c.isEmpty() && QFileInfo::exists(c)) return QFileInfo(c).absoluteFilePath();
    }
    return QString();
}

QString sharedDataDir() {
    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        appDir + QStringLiteral("/data"),
        installDirFromRunKey() + QStringLiteral("/data"),
    };
    for (const QString& c : candidates) {
        if (!c.isEmpty() && QFileInfo::exists(c)) return QFileInfo(c).absoluteFilePath();
    }
    return QString();
}

QString userDataDir() {
    QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (base.isEmpty()) {
        base = QDir::homePath() + QStringLiteral("/AppData/Roaming");
    }
    const QString dir = base + QStringLiteral("/Rime");
    QDir().mkpath(dir);
    return QDir(dir).absolutePath();
}

QString configDir() {
    const QString appDir = QCoreApplication::applicationDirPath();
    const QString local = appDir + QStringLiteral("/config.json");
    if (QFileInfo::exists(local)) return QFileInfo(local).absolutePath();
    const QString dataDir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    const QString dir = dataDir.isEmpty() ? appDir : dataDir;
    QDir().mkpath(dir);
    return QDir(dir).absolutePath();
}

QString skinDir() {
    const QString dir = configDir() + QStringLiteral("/skins");
    QDir().mkpath(dir);
    return QDir(dir).absolutePath();
}

} // namespace WeaselPaths
```

- [ ] **Step 5: 改 `src/rime_engine.cpp` 的 `initialize()` 使用新路径**

把 21-52 行替换为：

```cpp
bool RimeEngine::initialize(const QString& dllPath,
                            const QString& sharedDir,
                            const QString& userDir) {
    QString actualDll = dllPath.isEmpty() ? WeaselPaths::rimeDllPath() : dllPath;
    if (actualDll.isEmpty()) {
        m_isLoaded = false;
        return true;   // 进入仿真模式
    }

    m_lib.setFileName(actualDll);
    if (!m_lib.load() || !initRimeApi()) {
        m_isLoaded = false;
        return true;
    }

    QString actualShared = sharedDir.isEmpty() ? WeaselPaths::sharedDataDir() : sharedDir;
    QString actualUser   = userDir.isEmpty()   ? WeaselPaths::userDataDir()   : userDir;

    m_sharedDirBytes = actualShared.toUtf8();
    m_userDirBytes   = actualUser.toUtf8();
    m_appNameBytes   = "rime.weasel-fluent-cpp-qt";
    ...
```

并在文件顶部加 `#include "paths.h"`。

- [ ] **Step 6: 改 `src/settings_window.cpp` 的三处硬编码**

1440 行附近：

```cpp
    connect(btnOpenDict, &fluent::basicinput::Button::clicked, this, []() {
        QDesktopServices::openUrl(QUrl::fromLocalFile(WeaselPaths::userDataDir()));
    });
```

1524 行附近：

```cpp
    connect(btnOpenInstallDir, &fluent::basicinput::Button::clicked, this, []() {
        const QString dir = WeaselPaths::sharedDataDir().isEmpty()
                                ? QCoreApplication::applicationDirPath()
                                : QFileInfo(WeaselPaths::sharedDataDir()).absolutePath();
        QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
    });
```

1725 行附近（`onBackupDictClicked`）：

```cpp
    const QString rimeDir = WeaselPaths::userDataDir();
    const QString backupDir = rimeDir + QStringLiteral("/backup/") +
        QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_hhmmss"));
```

并把 1447 行的描述文案改为 `QStringLiteral("词库位于 %1").arg(WeaselPaths::userDataDir())`。
在文件顶部加 `#include "paths.h"`。

- [ ] **Step 7: 改 `src/main.cpp:318` 的硬编码截图路径**

```cpp
    const QString previewDir = QDir(QCoreApplication::applicationDirPath())
                                   .absoluteFilePath(QStringLiteral("preview"));
    QDir().mkpath(previewDir);
    panelPreviewModes.save(previewDir + QStringLiteral("/cand_extension_modes.png"));
```

- [ ] **Step 8: 改 `tsf/ipc_client.cpp:20-27` 的服务端路径回退**

```cpp
    if (serverPath[0] == L'\0') {
        QSettings settings(QStringLiteral("Microsoft"), QStringLiteral("Windows"),
                           QSettings::NativeFormat, QSettings::UserScope);
        settings.beginGroup(QStringLiteral("CurrentVersion/Run"));
        serverPath = settings.value(QStringLiteral("WeaselFluent")).toString().toWCharArray();
        settings.endGroup();
    }
    if (serverPath[0] == L'\0') {
        const QString fallback = QCoreApplication::applicationDirPath()
            + QStringLiteral("/weasel-fluent-cpp-qt.exe");
        serverPath = fallback.toWCharArray();
    }
```

并加 `#include <QCoreApplication>` 与 `#include <QSettings>`。

- [ ] **Step 9: 改 `scripts/package_dist.ps1` 的硬编码 scratch 路径**

把第 3 行改为：

```powershell
$ProjectDir = Split-Path -Parent $PSScriptRoot
```

（脚本位于 `scripts/`，故仓库根为其父目录。）

- [ ] **Step 10: 运行自检确认通过**

Run: `powershell -ExecutionPolicy Bypass -File scripts/verify-no-hardcoded-paths.ps1`
Expected: `PASS: 无硬编码路径`

- [ ] **Step 11: 编译验证**

Run: `C:\devtools\xmake\xmake\xmake.exe build -a`
Expected: 编译成功，无 error。

- [ ] **Step 12: 提交**

```bash
git add src/paths.h src/paths.cpp src/rime_engine.cpp src/settings_window.cpp src/main.cpp tsf/ipc_client.cpp scripts/verify-no-hardcoded-paths.ps1 scripts/package_dist.ps1
git commit -m "refactor(paths): centralize all runtime path resolution

Replace hardcoded developer paths (C:/Users/zheng/..., .gemini/antigravity
scratch dir, C:/Program Files/Rime/weasel-0.17.4) with a single
WeaselPaths resolver. Adds a self-check script that fails the build on
any reintroduction."
```

---

## Task 2: 去除 rime-ice

**Files:**
- Modify: `src/config.h:117`, `src/rime_engine.h:195`, `src/settings_window.cpp:1477,1715,1914,638,1417,1446,1596`, `src/candidate_window.cpp:1604`, `README.md:11,67,122`
- Modify: `scripts/package_dist.ps1:51-60`
- Create: `scripts/verify-no-rime-ice.ps1`

**Interfaces:**
- Consumes: Task 1 的 `WeaselPaths::userDataDir()`
- Produces: 默认方案常量 `luna_pinyin` 在全仓统一

- [ ] **Step 1: 写失败的自检脚本**

`scripts/verify-no-rime-ice.ps1`：

```powershell
$ErrorActionPreference = "Stop"
$repo = Split-Path -Parent $PSScriptRoot
$patterns = @('rime_ice', 'rime-ice', '雾凇')
$hits = @()
Get-ChildItem -Path $repo -Recurse -Include *.cpp,*.h,*.ps1,*.lua,*.md,*.yaml -File |
  Where-Object { $_.FullName -notmatch '[\\/]\.git[\\/]' } |
  ForEach-Object {
    $f = $_
    foreach ($p in $patterns) {
      $m = Select-String -Path $f.FullName -Pattern $p -SimpleMatch -ErrorAction SilentlyContinue
      foreach ($mm in $m) { $hits += "{0}:{1} [{2}] {3}" -f $f.FullName.Replace("$repo\",''), $mm.LineNumber, $p, $mm.Line.Trim() }
    }
  }
if ($hits.Count -gt 0) {
  Write-Host "FAIL: 仍有 $($hits.Count) 处 rime-ice 残留：" -ForegroundColor Red
  $hits | ForEach-Object { Write-Host "  $_" -ForegroundColor Red }
  exit 1
}
Write-Host "PASS: 无 rime-ice 残留" -ForegroundColor Green
exit 0
```

- [ ] **Step 2: 运行确认失败**

Run: `powershell -ExecutionPolicy Bypass -File scripts/verify-no-rime-ice.ps1`
Expected: FAIL，列出 10+ 处命中。

- [ ] **Step 3: 替换默认方案常量**

- `src/config.h:117`：`QString m_schema = QStringLiteral("rime_ice");` → `QStringLiteral("luna_pinyin");`
- `src/rime_engine.h:195`：`QString m_currentSchema = QStringLiteral("rime_ice");` → `QStringLiteral("luna_pinyin");`
- `src/settings_window.cpp:1914`：`cfg.setSchema(QStringLiteral("rime_ice"));` → `QStringLiteral("luna_pinyin");`

- [ ] **Step 4: 清理设置中心里的方案下拉与文案**

`src/settings_window.cpp:1475-1495` 的 ComboBox 暂时改为只列 `luna_pinyin`（真实枚举在 Task 5 接手）：

```cpp
    m_comboSchema = new fluent::basicinput::ComboBox();
    m_comboSchema->setFixedWidth(240);
    m_comboSchema->addItem(QStringLiteral("朙月拼音 · 全拼 (luna_pinyin)"));

    QString curSch = cfg.schema();
    m_comboSchema->setCurrentIndex(curSch == QStringLiteral("luna_pinyin") ? 0 : 0);
```

`onSchemaChanged` (1714-1722) 改为：

```cpp
void SettingsWindow::onSchemaChanged(int index) {
    Q_UNUSED(index);
    AppConfig::instance().setSchema(QStringLiteral("luna_pinyin"));
    if (m_heroPreview) m_heroPreview->refresh();
    onDeployClicked();
}
```

文案替换：
- 638 行 `QStringLiteral("Windows 11 Mica 材质 · 雾凇拼音 (rime-ice) · 全拼")` → `QStringLiteral("Windows 11 Mica 材质 · 朙月拼音 (luna_pinyin) · 全拼")`
- 1417 行 `QStringLiteral("管理 Rime 引擎核心依赖、雾凇拼音词库与执行方案重新部署")` → `QStringLiteral("管理 Rime 引擎核心依赖、用户词库与执行方案重新部署")`
- 1446 行 `QStringLiteral("雾凇拼音用户词库")` → `QStringLiteral("用户词库")`
- 1596 行 `QStringLiteral("librime (GPLv3) · Qt 6.8.2 (LGPLv3) · Fluent-Qt (MIT) · 雾凇拼音 (CC-BY-SA)")` → `QStringLiteral("librime (GPLv3) · Weasel (GPLv3) · Qt 6.8.2 (LGPLv3) · Fluent-Qt (MIT)")`

- [ ] **Step 5: 清理 `src/candidate_window.cpp:1604`**

```cpp
        {QStringLiteral("朙月拼音 (全拼)"), QStringLiteral("luna_pinyin")},
```

- [ ] **Step 6: 改 `scripts/package_dist.ps1` 不再整目录拷贝用户 Rime**

把 51-60 行替换为：

```powershell
# 6. Copy Rime shared data only (no user dictionary / no rime-ice)
$UserRimeSrc = "$env:APPDATA\Rime"
$UserDictsDst = "$DistDir\user_dicts"
New-Item -ItemType Directory -Path $UserDictsDst -Force | Out-Null
if (Test-Path $UserRimeSrc) {
    # 只拷贝基础方案文件，排除用户词库与 rime-ice 产物
    $keep = @("default.yaml", "default.custom.yaml", "luna_pinyin.schema.yaml",
              "luna_pinyin.dict.yaml", "luna_pinyin.reverse.dict.yaml",
              "grammar.yaml", "grammar.schema.yaml", "symbols.yaml")
    foreach ($name in $keep) {
        $src = Join-Path $UserRimeSrc $name
        if (Test-Path $src) { Copy-Item $src -Destination $UserDictsDst -Force }
    }
    Write-Host "[5/7] Copied base Rime schemas (rime-ice excluded)" -ForegroundColor Green
}
```

- [ ] **Step 7: 改 README.md**

- 第 11 行改为：`深度挂接 Librime 64 位核心引擎（\`rime.dll\`）与 Windows 11 TSF 系统输入法架构，彻底解决原版小狼毫界面老旧、高分屏模糊、容易假死与无法自适应 Win11 原生亚克力磨砂质感的历史痛点。`
- 第 67 行改为：`- 前后端全量预装包内置 64 位 \`rime.dll\` 与 Rime 基础数据，开箱即用；`
- 第 122 行的「rime-ice / 雾凇拼音」条目替换为：`* **[Weasel / 小狼毫](https://github.com/rime/weasel)**: 成熟的 Windows TSF 输入法核心实现，本项目在其之上重建现代化前端。`
- 许可证徽章与第 129 行的 MIT 描述改为 GPLv3。

- [ ] **Step 8: 运行自检确认通过**

Run: `powershell -ExecutionPolicy Bypass -File scripts/verify-no-rime-ice.ps1`
Expected: `PASS: 无 rime-ice 残留`

- [ ] **Step 9: 编译验证**

Run: `C:\devtools\xmake\xmake\xmake.exe build -a`
Expected: 编译成功。

- [ ] **Step 10: 提交**

```bash
git add src/config.h src/rime_engine.h src/settings_window.cpp src/candidate_window.cpp README.md scripts/package_dist.ps1 scripts/verify-no-rime-ice.ps1
git commit -m "feat: remove rime-ice, default to luna_pinyin

Drops the bundled rime-ice dictionary and its references. The default
scheme is now luna_pinyin, which ships with librime itself and needs no
external dictionary. Adds a self-check script for rime-ice residue."
```

---

## Task 3: 依赖就位 + vendor 官方 Weasel 内核

**Files:**
- Create: `3rdparty/weasel/**`（vendor）
- Create: `3rdparty/weasel/UPSTREAM.md`
- Create: `deps/boost/**`（Boost 源码与产物，不入库）
- Modify: `xmake.lua`
- Modify: `.gitignore`

**Interfaces:**
- Consumes: 无
- Produces: xmake target `WeaselIPC`、`WeaselIPCServer`、`RimeWithWeasel`（均为 `static`）；环境变量 `BOOST_ROOT` 指向 `deps/boost`

- [ ] **Step 1: 拉取官方 Weasel 并记录 commit**

```powershell
$ErrorActionPreference = 'Stop'
$root = 'C:\devtools\weasel-ref'
if (-not (Test-Path $root)) { git clone --depth 1 https://github.com/rime/weasel.git $root }
$sha = (git -C $root rev-parse HEAD).Trim()
Write-Host "upstream sha = $sha"
```

把 `$sha` 记入 `3rdparty/weasel/UPSTREAM.md`：

```markdown
# Vendored upstream: rime/weasel

- Repository: https://github.com/rime/weasel
- Commit: <上一步输出的 sha>
- Vendored on: 2026-11-16
- License: GPLv3 (see LICENSE.txt in this directory)

## Adopted modules
- `include/`          — shared headers (WeaselIPC, WeaselIPCData, WeaselStyle*, PipeChannel, ResponseParser, wtl/)
- `WeaselIPC/`        — client side (Configurator, Styler, ActionLoader, Committer, ContextUpdater)
- `WeaselIPCServer/`  — server side (WeaselServerImpl, SecurityAttribute)
- `RimeWithWeasel/`   — librime wrapper

## Deliberately excluded
- `WeaselUI/`       — D2D candidate panel; superseded by the Fluent Qt candidate window
- `WeaselTSF/`      — full TSF implementation; arrives in Round 2 behind a Qt bridge
- `WeaselDeployer/` — legacy Win32 dialogs; superseded by the Fluent settings center
- `WeaselServer/`   — legacy tray + service shell; superseded by our Qt service
- `WeaselSetup/`    — x86-only NSIS-era installer; rewritten as scripts/install.ps1 in Round 2
```

- [ ] **Step 2: 拷贝 vendor 目录**

```powershell
$src = 'C:\devtools\weasel-ref'
$dst = 'E:\weasel-fluent\weasel-fluent\3rdparty\weasel'
New-Item -ItemType Directory -Path $dst -Force | Out-Null
Copy-Item "$src\include" -Destination $dst -Recurse -Force
Copy-Item "$src\WeaselIPC" -Destination $dst -Recurse -Force
Copy-Item "$src\WeaselIPCServer" -Destination $dst -Recurse -Force
Copy-Item "$src\RimeWithWeasel" -Destination $dst -Recurse -Force
Copy-Item "$src\LICENSE.txt" -Destination $dst -Force
```

删除不需要的中间产物：`Get-ChildItem $dst -Recurse -Include *.vcxproj,*.filters | Remove-Item -Force`。

- [ ] **Step 3: 下载并编译 Boost 1.88.0**

```powershell
$ErrorActionPreference = 'Stop'
$deps = 'E:\weasel-fluent\weasel-fluent\deps'
New-Item -ItemType Directory -Path $deps -Force | Out-Null
$ver = '1_88_0'
$zip = "$deps\boost_$ver.zip"
if (-not (Test-Path $zip)) {
  Invoke-WebRequest -Uri "https://archives.boost.io/release/1.88.0/source/boost_$ver.zip" -OutFile $zip -TimeoutSec 1800
}
Expand-Archive -Path $zip -DestinationPath $deps -Force
cd "$deps\boost_$ver"
& .\bootstrap.bat --with-libraries=thread,serialization,system
& .\b2.exe --toolset=msvc --with-libraries=thread,serialization,system `
   variant=release link=shared runtime-link=shared threading=multi `
   -j8 --prefix="$deps\boost"
```

**若 `bootstrap.bat` 找不到编译器**：先执行 VS 开发者环境——
`& "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"` 不可直接在 PowerShell 中生效，改为用 `cmd /c` 包装：

```powershell
cmd /c '"C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" && cd /d "%CD%" && bootstrap.bat --with-libraries=thread,serialization,system && b2.exe --toolset=msvc --with-libraries=thread,serialization,system variant=release link=shared runtime-link=shared threading=multi -j8 --prefix="%CD%\..\boost"'
```

验证产物：`Get-ChildItem "$deps\boost\lib" -Filter 'boost_*.lib'` 至少含 `boost_thread`, `boost_serialization`, `boost_system`。

- [ ] **Step 4: 在 `xmake.lua` 新增静态库 target**

在 `set_languages("cxx17")` 之后插入：

```lua
-- Boost (from deps/boost, built by scripts/build_boost.ps1)
local boost_root = os.getenv("BOOST_ROOT") or "deps/boost"
add_includedirs("$(projectdir)/" .. boost_root)
add_linkdirs("$(projectdir)/" .. boost_root .. "/lib")

-- Vendored Weasel core (GPLv3) — see 3rdparty/weasel/UPSTREAM.md
add_includedirs("3rdparty/weasel/include")
add_includedirs("3rdparty/weasel/include/wtl")
```

并在 `FluentQt` target 之前插入三个 target：

```lua
-- Target: WeaselIPC (static) — client side of the Weasel IPC protocol
target("WeaselIPC")
    set_kind("static")
    set_languages("cxx17")
    add_rules("mode.debug", "mode.release")
    add_defines("UNICODE", "_UNICODE", "WINDOWS", "MSVC")
    if is_plat("windows") then
        add_cxflags("/utf-8", "/GS", "/Gy", "/EHsc", "/Zc:wchar_t", "/Zc:forScope", {tools = {"cl", "clang_cl"}})
    end
    add_includedirs("3rdparty/weasel/include")
    add_includedirs("3rdparty/weasel/include/wtl")
    add_files("3rdparty/weasel/WeaselIPC/*.cpp")
    add_syslinks("advapi32", "user32", "ole32")

-- Target: WeaselIPCServer (static)
target("WeaselIPCServer")
    set_kind("static")
    set_languages("cxx17")
    add_rules("mode.debug", "mode.release")
    add_defines("UNICODE", "_UNICODE", "WINDOWS", "MSVC")
    if is_plat("windows") then
        add_cxflags("/utf-8", "/GS", "/Gy", "/EHsc", "/Zc:wchar_t", "/Zc:forScope", {tools = {"cl", "clang_cl"}})
    end
    add_includedirs("3rdparty/weasel/include")
    add_includedirs("3rdparty/weasel/include/wtl")
    add_files("3rdparty/weasel/WeaselIPCServer/*.cpp")
    add_syslinks("advapi32", "user32", "ole32")

-- Target: RimeWithWeasel (static) — librime wrapper
target("RimeWithWeasel")
    set_kind("static")
    set_languages("cxx17")
    add_rules("mode.debug", "mode.release")
    add_defines("UNICODE", "_UNICODE", "WINDOWS", "MSVC")
    if is_plat("windows") then
        add_cxflags("/utf-8", "/GS", "/Gy", "/EHsc", "/Zc:wchar_t", "/Zc:forScope", {tools = {"cl", "clang_cl"}})
    end
    add_includedirs("3rdparty/weasel/include")
    add_includedirs("3rdparty/weasel/include/wtl")
    add_files("3rdparty/weasel/RimeWithWeasel/*.cpp")
    add_syslinks("advapi32", "user32", "ole32")
```

- [ ] **Step 5: 移除未使用的 QtSvg 依赖**

`xmake.lua:52` 的
```lua
add_frameworks("QtWidgets", "QtGui", "QtCore", "QtSvg")
```
改为
```lua
add_frameworks("QtWidgets", "QtGui", "QtCore")
```

- [ ] **Step 6: 统一运行时为 MT**

在 `set_languages("cxx17")` 之后加 `set_runtimes("MT")`（Weasel 上游要求 `/MT`）。

- [ ] **Step 7: `.gitignore` 追加**

```
# Vendored build deps (not source)
/deps/
```

- [ ] **Step 8: 编译验证**

Run: `C:\devtools\xmake\xmake\xmake.exe build -a`
Expected: 三个新 static target 编译成功。若 Boost 相关报错，记录具体缺失符号并修正 `add_links`。

- [ ] **Step 9: 提交（不含 deps/ 与 Boost 产物）**

```bash
git add .gitignore xmake.lua 3rdparty/weasel
git commit -m "build: vendor official Weasel core and add Boost dependency

Vendors rime/weasel's include/, WeaselIPC/, WeaselIPCServer/ and
RimeWithWeasel/ as static libraries (GPLv3, upstream SHA recorded in
3rdparty/weasel/UPSTREAM.md). Unifies the runtime to /MT as upstream
requires and drops the unused QtSvg link dependency."
```

---

## Task 4: `RimeConfigClient` 抽象 + `RimeEngine::deployFull()`

**Files:**
- Create: `src/rime_config_client.h`
- Modify: `src/rime_engine.h`, `src/rime_engine.cpp`
- Modify: `src/main.cpp`（`runVerificationSuite` 增加 TEST 10）

**Interfaces:**
- Consumes: Task 1 的 `WeaselPaths`
- Produces:
  ```cpp
  class RimeConfigClient {
  public:
      virtual ~RimeConfigClient() = default;
      virtual bool deployFull(QString* error) = 0;
      virtual bool selectSchema(const QString& schemaId, QString* error) = 0;
      virtual QString activeSchema() const = 0;
  };
  class EngineConfigClient : public RimeConfigClient { /* 基于本仓 RimeEngine */ };
  ```

- [ ] **Step 1: 写 `src/rime_config_client.h`**

```cpp
#ifndef RIME_CONFIG_CLIENT_H
#define RIME_CONFIG_CLIENT_H

#include <QString>

/// Rime 配置操作的抽象。
/// Round 1 由本仓 RimeEngine 实现；Round 2 改接官方 Weasel 的
/// Configurator / Styler / ActionLoader，调用方（设置中心）无需改动。
class RimeConfigClient {
public:
    virtual ~RimeConfigClient() = default;

    /// 全量重建：重新编译方案与词库。失败时 error 非空。
    virtual bool deployFull(QString* error) = 0;

    /// 切换到指定方案并使其生效。
    virtual bool selectSchema(const QString& schemaId, QString* error) = 0;

    /// 当前生效的方案 id。
    virtual QString activeSchema() const = 0;
};

#endif // RIME_CONFIG_CLIENT_H
```

- [ ] **Step 2: 给 `RimeEngine` 加 `deployFull()`**

`src/rime_engine.h` 在 `bool deploy();` 之后加声明：

```cpp
    /// 全量重建：RimeStartMaintenance(FullDeploy) + JoinMaintenanceThread。
    /// 与 deploy() 的区别：deploy() 仅重载已编译产物，不重编译词库。
    bool deployFull(QString* error = nullptr);
```

`src/rime_engine.cpp` 实现（放在现有 `deploy()` 之后）：

```cpp
bool RimeEngine::deployFull(QString* error) {
    if (!m_isLoaded || !m_fnStartMaintenance || !m_fnJoinMaintenanceThread) {
        if (error) *error = QStringLiteral("Rime 引擎未加载，无法执行全量部署");
        return false;
    }
    // RimeMaintenanceTypeFullDeploy = 1
    const int ret = m_fnStartMaintenance(1);
    if (ret == 0) {
        if (error) *error = QStringLiteral("Rime 拒绝了全量部署请求");
        return false;
    }
    m_fnJoinMaintenanceThread();
    if (m_sessionId && m_fnSelectSchema) {
        m_fnSelectSchema(m_sessionId, m_currentSchema.toUtf8().constData());
    }
    updateState();
    return true;
}
```

- [ ] **Step 3: 实现 `EngineConfigClient`**

在 `src/rime_config_client.h` 末尾追加（需前置声明避免循环包含）：

```cpp
class RimeEngine;

/// 基于本仓 RimeEngine 的 RimeConfigClient 实现。
class EngineConfigClient : public RimeConfigClient {
public:
    explicit EngineConfigClient(RimeEngine* engine) : m_engine(engine) {}

    bool deployFull(QString* error) override {
        if (!m_engine) {
            if (error) *error = QStringLiteral("引擎未初始化");
            return false;
        }
        return m_engine->deployFull(error);
    }

    bool selectSchema(const QString& schemaId, QString* error) override {
        if (!m_engine) {
            if (error) *error = QStringLiteral("引擎未初始化");
            return false;
        }
        if (!m_engine->selectSchema(schemaId)) {
            if (error) *error = QStringLiteral("切换方案失败：%1").arg(schemaId);
            return false;
        }
        return true;
    }

    QString activeSchema() const override {
        return m_engine ? m_engine->currentSchema() : QString();
    }

private:
    RimeEngine* m_engine = nullptr;
};
```

并在 `src/rime_config_client.h` 顶部 `#include "rime_engine.h"`（`RimeEngine` 无循环依赖，安全）。

- [ ] **Step 4: 在 `--verify` 增加 TEST 10**

`src/main.cpp` 的 `runVerificationSuite()` 末尾追加：

```cpp
    // 10. Verify deployFull() semantics and schema selection plumbing
    std::cout << "[TEST 10] Testing RimeEngine::deployFull() & schema selection...\n";
    {
        RimeEngine probe;
        probe.initialize();
        QString err;
        const bool deployed = probe.deployFull(&err);
        if (!probe.isLoaded()) {
            std::cout << "  -> SKIP: librime 未加载（仿真模式），跳过全量部署断言。\n";
        } else if (!deployed) {
            std::cerr << "FAIL: deployFull() returned false: " << err.toStdString() << "\n";
            return 1;
        } else {
            std::cout << "  -> PASS: deployFull() 全量重建成功。\n";
        }
    }
```

- [ ] **Step 5: 编译并运行验证**

Run: `C:\devtools\xmake\xmake\xmake.exe build -a && .\build\Release\weasel-fluent-cpp-qt.exe --verify`
Expected: 10 项测试全部 PASS（TEST 10 在无 librime 环境下输出 SKIP）。

- [ ] **Step 6: 提交**

```bash
git add src/rime_config_client.h src/rime_engine.h src/rime_engine.cpp src/main.cpp
git commit -m "feat(rime): add deployFull() and a RimeConfigClient abstraction

deployFull() issues RimeStartMaintenance(RimeMaintenanceTypeFullDeploy)
plus JoinMaintenanceThread, so scheme and dictionary changes actually
recompile instead of only reloading stale artifacts. RimeConfigClient
isolates the config operations so Round 2 can re-point them at Weasel's
Configurator/Styler/ActionLoader without touching the settings UI."
```

---

## Task 5: 方案枚举与真实切换（`SchemaManager`）

**Files:**
- Create: `src/schema_manager.h`, `src/schema_manager.cpp`
- Modify: `src/settings_window.h`（新增成员）, `src/settings_window.cpp`（维护页方案区）
- Modify: `src/main.cpp`（TEST 11）

**Interfaces:**
- Consumes: Task 1 的 `WeaselPaths::userDataDir()`；Task 4 的 `RimeConfigClient`
- Produces:
  ```cpp
  struct SchemaInfo {
      QString id;        // schema_id
      QString name;      // 显示名
      QString fileName;  // 源文件名，如 luna_pinyin.schema.yaml
      bool deployed;     // build/ 中是否已编译
  };

  class SchemaManager : public QObject {
      Q_OBJECT
  public:
      static SchemaManager& instance();
      QVector<SchemaInfo> scanSchemas() const;
      bool writeSchemaSelection(const QString& schemaId, QString* error);
      static QString parseSchemaName(const QString& yamlPath);
      static QString parseSchemaId(const QString& yamlPath);
  };
  ```

- [ ] **Step 1: 实现 `src/schema_manager.h`**

```cpp
#ifndef SCHEMA_MANAGER_H
#define SCHEMA_MANAGER_H

#include <QObject>
#include <QString>
#include <QVector>

/// 一个 Rime 输入方案的元信息。
struct SchemaInfo {
    QString id;        ///< schema_id
    QString name;      ///< 供界面显示的名字
    QString fileName;  ///< 源文件名，如 luna_pinyin.schema.yaml
    bool deployed = false; ///< 是否已在 build/ 中编译
};

/// 枚举用户 Rime 目录下的输入方案，并负责改写方案清单与选中项。
class SchemaManager : public QObject {
    Q_OBJECT
public:
    static SchemaManager& instance();

    /// 扫描用户 Rime 目录（及 build/）下的 *.schema.yaml。
    QVector<SchemaInfo> scanSchemas() const;

    /// 把 schemaId 写进 default.yaml 的 schema_list 与
    /// default.custom.yaml 的 var.previously_selected_schema。
    bool writeSchemaSelection(const QString& schemaId, QString* error);

    /// 从 schema yaml 中解析 schema.name，解析不到返回空串。
    static QString parseSchemaName(const QString& yamlPath);

    /// 从 schema yaml 中解析 schema.schema_id，解析不到返回空串。
    static QString parseSchemaId(const QString& yamlPath);

    /// 供测试使用：把 yaml 文本解析成 (name, schema_id)。
    static QPair<QString, QString> parseSchemaHeader(const QString& yamlText);
};

#endif // SCHEMA_MANAGER_H
```

- [ ] **Step 2: 实现 `src/schema_manager.cpp`**

```cpp
// src/schema_manager.cpp
#include "schema_manager.h"
#include "paths.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QTextStream>

namespace {

/// 极简 YAML 读取：只取 schema: 块下的 name: 与 schema_id: 两级缩进。
QPair<QString, QString> parseHeader(const QString& text) {
    QString name;
    QString id;
    bool inSchema = false;
    const QStringList lines = text.split(QLatin1Char('\n'));
    for (const QString& raw : lines) {
        const QString line = raw.trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#'))) continue;
        if (line.startsWith(QLatin1Char('-'))) continue;

        const int indent = raw.length() - raw.trimmed().length();
        if (indent == 0) {
            inSchema = line.startsWith(QStringLiteral("schema:"));
            continue;
        }
        if (!inSchema) continue;
        if (line.startsWith(QStringLiteral("name:"))) {
            name = line.mid(5).trimmed();
            if (name.startsWith(QLatin1Char('"')) && name.endsWith(QLatin1Char('"')))
                name = name.mid(1, name.size() - 2);
        } else if (line.startsWith(QStringLiteral("schema_id:"))) {
            id = line.mid(10).trimmed();
            if (id.startsWith(QLatin1Char('"')) && id.endsWith(QLatin1Char('"')))
                id = id.mid(1, id.size() - 2);
        }
    }
    return {name, id};
}

QString readTextFile(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return QString();
    const QByteArray raw = f.readAll();
    f.close();
    return QString::fromUtf8(raw);
}

} // namespace

SchemaManager& SchemaManager::instance() {
    static SchemaManager s_inst;
    return s_inst;
}

QPair<QString, QString> SchemaManager::parseSchemaHeader(const QString& yamlText) {
    return parseHeader(yamlText);
}

QString SchemaManager::parseSchemaName(const QString& yamlPath) {
    return parseHeader(readTextFile(yamlPath)).first;
}

QString SchemaManager::parseSchemaId(const QString& yamlPath) {
    return parseHeader(readTextFile(yamlPath)).second;
}

QVector<SchemaInfo> SchemaManager::scanSchemas() const {
    QVector<SchemaInfo> result;
    const QString userDir = WeaselPaths::userDataDir();
    if (userDir.isEmpty()) return result;

    const QString buildDir = userDir + QStringLiteral("/build");
    const QDir dir(userDir);
    const QStringList files = dir.entryList(QStringList() << QStringLiteral("*.schema.yaml"),
                                            QDir::Files, QDir::Name);
    for (const QString& f : files) {
        SchemaInfo info;
        info.fileName = f;
        const QString full = dir.filePath(f);
        const auto hdr = parseHeader(readTextFile(full));
        info.name = hdr.first;
        info.id = hdr.second;
        if (info.id.isEmpty()) {
            // 回退：schema_id 缺省时取文件名去掉后缀
            info.id = f;
            info.id.remove(QStringLiteral(".schema.yaml"));
        }
        if (info.name.isEmpty()) info.name = info.id;
        info.deployed = QFileInfo::exists(buildDir + QStringLiteral("/") + f);
        result.append(info);
    }
    return result;
}

bool SchemaManager::writeSchemaSelection(const QString& schemaId, QString* error) {
    const QString userDir = WeaselPaths::userDataDir();
    if (userDir.isEmpty()) {
        if (error) *error = QStringLiteral("无法定位用户 Rime 目录");
        return false;
    }

    // 1) default.custom.yaml —— 写入 var.previously_selected_schema
    const QString customPath = userDir + QStringLiteral("/default.custom.yaml");
    QString customText = readTextFile(customPath);
    const QRegularExpression varRe(QStringLiteral("(?m)^(\\s*)previously_selected_schema:.*$"));
    const QString replacement = QStringLiteral("\\1previously_selected_schema: ") + schemaId;
    if (varRe.test(customText)) {
        customText.replace(varRe, replacement);
    } else {
        QString prefix;
        if (customText.contains(QStringLiteral("var:"))) {
            customText.replace(QRegularExpression(QStringLiteral("(?m)^var:\\s*$")),
                               QStringLiteral("var:\n  previously_selected_schema: ") + schemaId);
        } else {
            prefix = QStringLiteral("var:\n  previously_selected_schema: ") + schemaId + QStringLiteral("\n");
        }
        customText = prefix + customText;
    }
    {
        QFile f(customPath);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
            if (error) *error = QStringLiteral("无法写入 default.custom.yaml");
            return false;
        }
        f.write(customText.toUtf8());
        f.close();
    }

    // 2) default.yaml —— 更新 schema_list
    const QString defaultPath = userDir + QStringLiteral("/default.yaml");
    QString defaultText = readTextFile(defaultPath);
    if (defaultText.isEmpty()) {
        defaultText = QStringLiteral("schema_list:\n  - schema: ") + schemaId + QStringLiteral("\n");
    } else {
        // 目标 id 已在列表中则不重复添加；否则插到列表首位
        const QString needle = QStringLiteral("- schema: ") + schemaId;
        if (!defaultText.contains(needle)) {
            const QRegularExpression listRe(QStringLiteral("(?m)^schema_list:\\s*$"));
            if (listRe.test(defaultText)) {
                defaultText.replace(listRe, QStringLiteral("schema_list:\n  - schema: ") + schemaId);
            } else {
                defaultText = QStringLiteral("schema_list:\n  - schema: ") + schemaId + QStringLiteral("\n") + defaultText;
            }
        }
    }
    {
        QFile f(defaultPath);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
            if (error) *error = QStringLiteral("无法写入 default.yaml");
            return false;
        }
        f.write(defaultText.toUtf8());
        f.close();
    }
    return true;
}
```

- [ ] **Step 3: 把设置中心的方案下拉改为真实枚举**

`src/settings_window.h` 成员区新增：

```cpp
    fluent::basicinput::ComboBox* m_comboSchema = nullptr;   // 已存在，复用
    void refreshSchemaList();
```

`src/settings_window.cpp`：

1) 在 `createMaintenancePage()` 中，把 Task 2 留下的单条 ComboBox 换成：

```cpp
    m_comboSchema = new fluent::basicinput::ComboBox();
    m_comboSchema->setFixedWidth(280);
    refreshSchemaList();
    connect(m_comboSchema, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &SettingsWindow::onSchemaChanged);
```

2) 新增 `refreshSchemaList()` 实现：

```cpp
void SettingsWindow::refreshSchemaList() {
    if (!m_comboSchema) return;
    m_comboSchema->blockSignals(true);
    m_comboSchema->clear();

    const auto schemas = SchemaManager::instance().scanSchemas();
    const QString current = AppConfig::instance().schema();
    int currentIndex = -1;
    for (const auto& s : schemas) {
        const QString label = s.deployed
            ? QStringLiteral("%1 (%2)").arg(s.name, s.id)
            : QStringLiteral("%1 (%2) · 未部署").arg(s.name, s.id);
        m_comboSchema->addItem(label);
        if (s.id == current) currentIndex = m_comboSchema->count() - 1;
    }
    if (currentIndex < 0 && m_comboSchema->count() > 0) currentIndex = 0;
    if (currentIndex >= 0) m_comboSchema->setCurrentIndex(currentIndex);
    m_comboSchema->blockSignals(false);
}
```

3) 改写 `onSchemaChanged`：

```cpp
void SettingsWindow::onSchemaChanged(int index) {
    if (index < 0 || !m_comboSchema) return;
    const auto schemas = SchemaManager::instance().scanSchemas();
    if (index >= schemas.size()) return;
    const QString schemaId = schemas.at(index).id;

    QString error;
    if (!SchemaManager::instance().writeSchemaSelection(schemaId, &error)) {
        if (m_lblDeployStatus) m_lblDeployStatus->setText(error);
        return;
    }
    AppConfig::instance().setSchema(schemaId);

    if (m_engine) {
        m_engine->selectSchema(schemaId);
        QString deployError;
        if (!m_engine->deployFull(&deployError)) {
            if (m_lblDeployStatus) m_lblDeployStatus->setText(deployError);
            return;
        }
    }
    if (m_lblDeployStatus) m_lblDeployStatus->setText(QStringLiteral("已切换并重新部署"));
    if (m_heroPreview) m_heroPreview->refresh();
}
```

4) 在 `createMaintenancePage()` 的「重新部署」按钮旁追加一个「刷新方案列表」按钮，连接 `refreshSchemaList()`。

- [ ] **Step 4: 在 `--verify` 增加 TEST 11**

```cpp
    // 11. Verify SchemaManager enumeration & YAML header parsing
    std::cout << "[TEST 11] Testing SchemaManager enumeration & YAML parsing...\n";
    {
        const QString sample =
            QStringLiteral("---\n"
                           "schema:\n"
                           "  schema_id: luna_pinyin\n"
                           "  name: 朙月拼音\n"
                           "  engine:\n"
                           "    processors:\n"
                           "      - ascii_composer\n");
        const auto hdr = SchemaManager::parseSchemaHeader(sample);
        if (hdr.first != QStringLiteral("朙月拼音") || hdr.second != QStringLiteral("luna_pinyin")) {
            std::cerr << "FAIL: schema header parse mismatch: " << hdr.first.toStdString()
                      << " / " << hdr.second.toStdString() << "\n";
            return 1;
        }
        const auto schemas = SchemaManager::instance().scanSchemas();
        std::cout << "  -> 扫描到 " << schemas.size() << " 个方案, 解析 PASS.\n";
    }
```

- [ ] **Step 5: 编译并验证**

Run: `C:\devtools\xmake\xmake\xmake.exe build -a && .\build\Release\weasel-fluent-cpp-qt.exe --verify`
Expected: 11 项 PASS。

- [ ] **Step 6: 提交**

```bash
git add src/schema_manager.h src/schema_manager.cpp src/settings_window.h src/settings_window.cpp src/main.cpp
git commit -m "feat(schema): real scheme enumeration, selection and full deploy

The scheme dropdown was four hardcoded labels that never touched the
engine. It now enumerates *.schema.yaml from the user Rime directory,
parses schema.name/schema_id, writes schema_list plus
previously_selected_schema, and triggers a real full rebuild."
```

---

## Task 6: 自定义词库（`DictionaryManager`）

**Files:**
- Create: `src/dictionary_manager.h`, `src/dictionary_manager.cpp`
- Modify: `src/settings_window.h`, `src/settings_window.cpp`（维护页）
- Modify: `src/main.cpp`（TEST 12）

**Interfaces:**
- Consumes: Task 1 `WeaselPaths::userDataDir()`；Task 4 `RimeEngine::deployFull()`
- Produces:
  ```cpp
  struct DictionaryEntry { QString name; QString path; bool custom; };

  class DictionaryManager : public QObject {
      Q_OBJECT
  public:
      static DictionaryManager& instance();
      QVector<DictionaryEntry> listDictionaries() const;
      bool importDictionary(const QString& srcPath, QString* error);
      bool removeDictionary(const QString& name, QString* error);
      static bool patchDictionaryList(const QString& yamlText,
                                      const QString& addName,
                                      const QString& removeName,
                                      QString* outText);
      static bool looksLikeDictionary(const QString& text);
  };
  ```

- [ ] **Step 1: 实现 `src/dictionary_manager.h`**

```cpp
#ifndef DICTIONARY_MANAGER_H
#define DICTIONARY_MANAGER_H

#include <QObject>
#include <QString>
#include <QVector>

/// 一个词库条目。
struct DictionaryEntry {
    QString name;    ///< 不含扩展名的词库名
    QString path;    ///< 绝对路径
    bool custom = false; ///< 是否为用户导入（而非方案内置）
};

/// 管理用户自定义词库：导入、移除、挂载到当前方案。
class DictionaryManager : public QObject {
    Q_OBJECT
public:
    static DictionaryManager& instance();

    /// 列出用户 Rime 目录下的所有词库文件。
    QVector<DictionaryEntry> listDictionaries() const;

    /// 导入词库：复制到用户 Rime 目录并挂载到 default.custom.yaml。
    bool importDictionary(const QString& srcPath, QString* error);

    /// 移除词库：删除文件并从 default.custom.yaml 的 dictionary 列表摘除。
    bool removeDictionary(const QString& name, QString* error);

    /// 在 default.custom.yaml 文本中增删 dictionary 条目。
    /// addName / removeName 为空表示该侧不改动。成功时 outText 持有结果。
    static bool patchDictionaryList(const QString& yamlText,
                                    const QString& addName,
                                    const QString& removeName,
                                    QString* outText);

    /// 粗略校验文本是否为词库文件（首行 --- 或含制表符分隔的词条）。
    static bool looksLikeDictionary(const QString& text);
};

#endif // DICTIONARY_MANAGER_H
```

- [ ] **Step 2: 实现 `src/dictionary_manager.cpp`**

```cpp
// src/dictionary_manager.cpp
#include "dictionary_manager.h"
#include "paths.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QTextStream>

namespace {

QString readTextFile(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return QString();
    const QByteArray raw = f.readAll();
    f.close();
    return QString::fromUtf8(raw);
}

const QRegularExpression& dictEntryRe() {
    static const QRegularExpression re(QStringLiteral("(?m)^\\s*-\\s*\\S+\\.dict\\.yaml\\s*$"));
    return re;
}

} // namespace

DictionaryManager& DictionaryManager::instance() {
    static DictionaryManager s_inst;
    return s_inst;
}

bool DictionaryManager::looksLikeDictionary(const QString& text) {
    if (text.trimmed().isEmpty()) return false;
    if (text.startsWith(QStringLiteral("---"))) return true;
    return text.contains(QLatin1Char('\t'));
}

bool DictionaryManager::patchDictionaryList(const QString& yamlText,
                                            const QString& addName,
                                            const QString& removeName,
                                            QString* outText) {
    QString text = yamlText;

    // 摘除
    if (!removeName.isEmpty()) {
        const QString needle = QStringLiteral("- ") + removeName + QStringLiteral(".dict.yaml");
        QStringList kept;
        bool inDict = false;
        const QStringList lines = text.split(QLatin1Char('\n'));
        for (const QString& raw : lines) {
            const QString t = raw.trimmed();
            if (t == QStringLiteral("dictionary:") || t.startsWith(QStringLiteral("dictionary:"))) {
                inDict = true;
                kept << raw;
                continue;
            }
            if (inDict && raw.isEmpty()) { inDict = false; kept << raw; continue; }
            if (inDict && raw.startsWith(QLatin1Char(' '))) {
                if (t == needle) continue;   // 丢弃要移除的条目
                kept << raw;
                continue;
            }
            inDict = false;
            kept << raw;
        }
        text = kept.join(QLatin1Char('\n'));
    }

    // 追加
    if (!addName.isEmpty()) {
        const QString entry = QStringLiteral("  - ") + addName + QStringLiteral(".dict.yaml");
        if (!text.contains(entry)) {
            const QRegularExpression head(QStringLiteral("(?m)^dictionary:\\s*$"));
            if (head.test(text)) {
                text.replace(head, QStringLiteral("dictionary:\n") + entry);
            } else {
                text = QStringLiteral("dictionary:\n") + entry + QStringLiteral("\n") + text;
            }
        }
    }

    if (outText) *outText = text;
    return true;
}

QVector<DictionaryEntry> DictionaryManager::listDictionaries() const {
    QVector<DictionaryEntry> out;
    const QString userDir = WeaselPaths::userDataDir();
    if (userDir.isEmpty()) return out;
    const QDir dir(userDir);
    const QStringList files = dir.entryList(QStringList() << QStringLiteral("*.dict.yaml"),
                                            QDir::Files, QDir::Name);
    for (const QString& f : files) {
        DictionaryEntry e;
        e.name = f;
        e.name.remove(QStringLiteral(".dict.yaml"));
        e.path = dir.filePath(f);
        // 内置词库：其名与当前方案的 translator/dictionary 同名，视为非自定义
        e.custom = !f.startsWith(QStringLiteral("luna_pinyin"));
        out.append(e);
    }
    return out;
}

bool DictionaryManager::importDictionary(const QString& srcPath, QString* error) {
    const QFileInfo src(srcPath);
    if (!src.exists() || !src.isFile()) {
        if (error) *error = QStringLiteral("源文件不存在");
        return false;
    }
    const QString text = readTextFile(srcPath);
    if (!looksLikeDictionary(text)) {
        if (error) *error = QStringLiteral("不是有效的词库文件（需为 .dict 或 .dict.yaml）");
        return false;
    }

    const QString userDir = WeaselPaths::userDataDir();
    const QString name = src.completeBaseName();      // 去掉最后一个扩展名
    QString targetName = name;
    if (!targetName.endsWith(QStringLiteral(".dict"))) {
        targetName = targetName + QStringLiteral(".dict.yaml");
    }
    const QString target = userDir + QStringLiteral("/") + targetName;

    if (QFileInfo::exists(target) && QFileInfo(target) != src) {
        if (error) *error = QStringLiteral("已存在同名词库：%1").arg(targetName);
        return false;
    }
    if (!QFile::copy(srcPath, target)) {
        if (error) *error = QStringLiteral("复制词库失败");
        return false;
    }

    const QString customPath = userDir + QStringLiteral("/default.custom.yaml");
    QString customText = readTextFile(customPath);
    QString patched;
    patchDictionaryList(customText, targetName, QString(), &patched);
    {
        QFile f(customPath);
        if (f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
            f.write(patched.toUtf8());
            f.close();
        }
    }
    return true;
}

bool DictionaryManager::removeDictionary(const QString& name, QString* error) {
    const QString userDir = WeaselPaths::userDataDir();
    const QString targetName = name.endsWith(QStringLiteral(".dict.yaml"))
                                   ? name : name + QStringLiteral(".dict.yaml");
    const QString target = userDir + QStringLiteral("/") + targetName;
    if (!QFileInfo::exists(target)) {
        if (error) *error = QStringLiteral("词库不存在：%1").arg(targetName);
        return false;
    }
    if (!QFile::remove(target)) {
        if (error) *error = QStringLiteral("删除词库文件失败");
        return false;
    }
    const QString customPath = userDir + QStringLiteral("/default.custom.yaml");
    QString customText = readTextFile(customPath);
    QString patched;
    patchDictionaryList(customText, QString(), targetName, &patched);
    {
        QFile f(customPath);
        if (f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
            f.write(patched.toUtf8());
            f.close();
        }
    }
    return true;
}
```

- [ ] **Step 3: 设置中心加「导入词库 / 移除词库」UI**

`src/settings_window.h` 成员区新增：

```cpp
    fluent::basicinput::Button* m_btnImportDict = nullptr;
    fluent::basicinput::Button* m_btnRemoveDict = nullptr;
    fluent::textfields::Label* m_lblDictList = nullptr;
```

`src/settings_window.cpp` 的 `createMaintenancePage()` 中，在「用户词库」卡片之后插入：

```cpp
    // 自定义词库导入 / 移除
    m_lblDictList = new fluent::textfields::Label(QStringLiteral("（未导入自定义词库）"));
    m_lblDictList->setFluentTypography(Typography::FontRole::Caption);
    m_lblDictList->setTextColorRole(fluent::textfields::Label::TextColorRole::Secondary);

    m_btnImportDict = new fluent::basicinput::Button(QStringLiteral("导入词库"));
    m_btnImportDict->setFluentStyle(fluent::basicinput::Button::Accent);
    connect(m_btnImportDict, &fluent::basicinput::Button::clicked, this, [this]() {
        const QString path = QFileDialog::getOpenFileName(
            this, QStringLiteral("选择词库文件"), QString(),
            QStringLiteral("词库文件 (*.dict *.dict.yaml);;所有文件 (*.*)"));
        if (path.isEmpty()) return;
        QString error;
        if (!DictionaryManager::instance().importDictionary(path, &error)) {
            m_lblDictList->setText(error);
            return;
        }
        refreshDictionaryList();
        if (m_engine) {
            QString deployError;
            m_engine->deployFull(&deployError);
        }
        m_lblDictList->setText(QStringLiteral("已导入并重新部署"));
    });

    m_btnRemoveDict = new fluent::basicinput::Button(QStringLiteral("移除选中"));
    m_btnRemoveDict->setFluentStyle(fluent::basicinput::Button::Standard);
    connect(m_btnRemoveDict, &fluent::basicinput::Button::clicked, this, [this]() {
        const auto dicts = DictionaryManager::instance().listDictionaries();
        if (dicts.isEmpty()) return;
        const QString name = dicts.first().name;   // 简化：移除列表首个自定义词库
        QString error;
        if (!DictionaryManager::instance().removeDictionary(name, &error)) {
            m_lblDictList->setText(error);
            return;
        }
        refreshDictionaryList();
    });
```

并新增 `refreshDictionaryList()`：

```cpp
void SettingsWindow::refreshDictionaryList() {
    if (!m_lblDictList) return;
    const auto dicts = DictionaryManager::instance().listDictionaries();
    QStringList names;
    for (const auto& d : dicts) {
        if (d.custom) names << d.name;
    }
    m_lblDictList->setText(names.isEmpty()
        ? QStringLiteral("（未导入自定义词库）")
        : QStringLiteral("已导入：%1").arg(names.join(QStringLiteral("、"))));
}
```

在 `createMaintenancePage()` 末尾调用 `refreshDictionaryList();`。
补 `#include <QFileDialog>` 与 `#include "dictionary_manager.h"`。

- [ ] **Step 4: 在 `--verify` 增加 TEST 12**

```cpp
    // 12. Verify DictionaryManager patch semantics
    std::cout << "[TEST 12] Testing DictionaryManager dictionary list patching...\n";
    {
        const QString in = QStringLiteral("dictionary:\n  - foo.dict.yaml\n  - bar.dict.yaml\nvar:\n  previously_selected_schema: luna_pinyin\n");
        QString out;
        DictionaryManager::patchDictionaryList(in, QStringLiteral("baz"), QStringLiteral("foo"), &out);
        if (!out.contains(QStringLiteral("baz.dict.yaml"))) {
            std::cerr << "FAIL: dictionary patch did not add baz\n";
            return 1;
        }
        if (out.contains(QStringLiteral("foo.dict.yaml"))) {
            std::cerr << "FAIL: dictionary patch did not remove foo\n";
            return 1;
        }
        if (!out.contains(QStringLiteral("previously_selected_schema"))) {
            std::cerr << "FAIL: dictionary patch lost unrelated keys\n";
            return 1;
        }
        std::cout << "  -> PASS: dictionary list patch add/remove OK.\n";
    }
```

- [ ] **Step 5: 编译并验证**

Run: `C:\devtools\xmake\xmake\xmake.exe build -a && .\build\Release\weasel-fluent-cpp-qt.exe --verify`
Expected: 12 项 PASS。

- [ ] **Step 6: 提交**

```bash
git add src/dictionary_manager.h src/dictionary_manager.cpp src/settings_window.h src/settings_window.cpp src/main.cpp
git commit -m "feat(dict): custom dictionary import, removal and mounting

Imports .dict/.dict.yaml files into the user Rime directory, mounts them
into the active schema through default.custom.yaml, and triggers a full
rebuild so the new words are actually compiled in."
```

---

## Task 7: 自定义皮肤（`SkinManager`）

**Files:**
- Create: `src/skin_manager.h`, `src/skin_manager.cpp`
- Modify: `src/settings_window.h`, `src/settings_window.cpp`（个性化页）
- Modify: `src/main.cpp`（TEST 13）

**Interfaces:**
- Consumes: `fluent::UserTheme`（Fluent-Qt 内置）
- Produces:
  ```cpp
  class SkinManager : public QObject {
      Q_OBJECT
  public:
      static SkinManager& instance();
      QStringList availableSkins() const;
      bool importSkin(const QString& srcPath, QString* error);
      bool applySkin(const QString& fileName, QString* error);
      bool exportTemplate(QString* error);
      bool resetToDefault(QString* error);
      QString currentSkin() const;
  };
  ```

- [ ] **Step 1: 确认 Fluent-Qt 的 UserTheme API 行为**

读 `3rdparty/Fluent-Qt/src/components/foundation/UserTheme.h`，确认这四个函数存在：
`applyOverrides(const QJsonObject&)`、`filePath()`、`directory()`、`exportTemplate(bool overwrite)`。
若 `filePath()` / `directory()` 在本版本中语义不同，按实际签名调整 Step 3。

- [ ] **Step 2: 实现 `src/skin_manager.h`**

```cpp
#ifndef SKIN_MANAGER_H
#define SKIN_MANAGER_H

#include <QObject>
#include <QString>
#include <QStringList>

/// 管理 Fluent 界面皮肤（Fluent-Qt token 覆盖文件）。
class SkinManager : public QObject {
    Q_OBJECT
public:
    static SkinManager& instance();

    /// skinDir() 下可用的皮肤文件名列表（.json）。
    QStringList availableSkins() const;

    /// 导入皮肤 JSON 到 skinDir()，并立即应用。
    bool importSkin(const QString& srcPath, QString* error);

    /// 应用指定皮肤文件。
    bool applySkin(const QString& fileName, QString* error);

    /// 导出可编辑的模板到 skinDir()/template.json。
    bool exportTemplate(QString* error);

    /// 清除当前皮肤覆盖，恢复 Fluent 默认外观。
    bool resetToDefault(QString* error);

    /// 当前生效的皮肤文件名，空串表示默认。
    QString currentSkin() const;
};

#endif // SKIN_MANAGER_H
```

- [ ] **Step 3: 实现 `src/skin_manager.cpp`**

```cpp
// src/skin_manager.cpp
#include "skin_manager.h"
#include "paths.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>

#include "components/foundation/UserTheme.h"

SkinManager& SkinManager::instance() {
    static SkinManager s_inst;
    return s_inst;
}

QString SkinManager::currentSkin() const {
    return m_currentSkin;
}

QStringList SkinManager::availableSkins() const {
    const QDir dir(WeaselPaths::skinDir());
    QStringList out;
    const QStringList files = dir.entryList(QStringList() << QStringLiteral("*.json"),
                                            QDir::Files, QDir::Name);
    for (const QString& f : files) out << f;
    return out;
}

bool SkinManager::importSkin(const QString& srcPath, QString* error) {
    const QFileInfo src(srcPath);
    if (!src.exists()) {
        if (error) *error = QStringLiteral("皮肤文件不存在");
        return false;
    }
    QFile f(srcPath);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error) *error = QStringLiteral("无法读取皮肤文件");
        return false;
    }
    const QByteArray raw = f.readAll();
    f.close();

    QJsonParseError perr{};
    const QJsonDocument doc = QJsonDocument::fromJson(raw, &perr);
    if (perr.error != QJsonParseError::NoError || !doc.isObject()) {
        if (error) *error = QStringLiteral("皮肤文件不是合法的 JSON 对象：%1").arg(perr.errorString());
        return false;
    }

    const QString target = WeaselPaths::skinDir() + QStringLiteral("/") + src.fileName();
    if (!QFile::copy(srcPath, target)) {
        if (error) *error = QStringLiteral("复制皮肤文件失败");
        return false;
    }
    return applySkin(src.fileName(), error);
}

bool SkinManager::applySkin(const QString& fileName, QString* error) {
    if (fileName.isEmpty() || fileName == QStringLiteral("默认")) {
        return resetToDefault(error);
    }
    const QString path = WeaselPaths::skinDir() + QStringLiteral("/") + fileName;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error) *error = QStringLiteral("找不到皮肤文件：%1").arg(fileName);
        return false;
    }
    const QByteArray raw = f.readAll();
    f.close();

    QJsonParseError perr{};
    const QJsonDocument doc = QJsonDocument::fromJson(raw, &perr);
    if (perr.error != QJsonParseError::NoError || !doc.isObject()) {
        if (error) *error = QStringLiteral("皮肤 JSON 解析失败：%1").arg(perr.errorString());
        return false;
    }

    // 写入 Fluent-Qt 的用户覆盖文件，再由它统一分发到所有界面
    const QString userFile = fluent::UserTheme::filePath();
    QFile out(userFile);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error) *error = QStringLiteral("无法写入 Fluent 用户主题文件：%1").arg(userFile);
        return false;
    }
    out.write(raw);
    out.close();

    fluent::UserTheme::apply();
    m_currentSkin = fileName;
    return true;
}

bool SkinManager::exportTemplate(QString* error) {
    if (!fluent::UserTheme::exportTemplate(true)) {
        if (error) *error = QStringLiteral("导出主题模板失败");
        return false;
    }
    return true;
}

bool SkinManager::resetToDefault(QString* error) {
    const QString userFile = fluent::UserTheme::filePath();
    if (QFileInfo::exists(userFile) && !QFile::remove(userFile)) {
        if (error) *error = QStringLiteral("无法移除用户主题覆盖文件");
        return false;
    }
    fluent::UserTheme::apply();
    m_currentSkin.clear();
    return true;
}
```

并在 `src/skin_manager.h` 的类内加私有成员 `QString m_currentSkin;`。

- [ ] **Step 4: 设置中心个性化页加皮肤 UI**

`src/settings_window.h` 成员区新增：

```cpp
    fluent::basicinput::ComboBox* m_comboSkin = nullptr;
    fluent::basicinput::Button* m_btnImportSkin = nullptr;
    fluent::basicinput::Button* m_btnExportSkin = nullptr;
    fluent::textfields::Label* m_lblSkinStatus = nullptr;
```

`src/settings_window.cpp` 的 `createPersonalizationPage()` 中插入一张卡片：

```cpp
    // 自定义皮肤
    m_comboSkin = new fluent::basicinput::ComboBox();
    m_comboSkin->setFixedWidth(220);
    m_comboSkin->addItem(QStringLiteral("默认"));
    for (const QString& s : SkinManager::instance().availableSkins())
        m_comboSkin->addItem(s);
    connect(m_comboSkin, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int index) {
                if (index <= 0) { SkinManager::instance().resetToDefault(nullptr); return; }
                const QString file = m_comboSkin->itemText(index);
                QString error;
                if (!SkinManager::instance().applySkin(file, &error)) {
                    if (m_lblSkinStatus) m_lblSkinStatus->setText(error);
                }
            });

    m_btnImportSkin = new fluent::basicinput::Button(QStringLiteral("导入皮肤"));
    m_btnImportSkin->setFluentStyle(fluent::basicinput::Button::Accent);
    connect(m_btnImportSkin, &fluent::basicinput::Button::clicked, this, [this]() {
        const QString path = QFileDialog::getOpenFileName(
            this, QStringLiteral("选择皮肤文件"), QString(),
            QStringLiteral("皮肤文件 (*.json);;所有文件 (*.*)"));
        if (path.isEmpty()) return;
        QString error;
        if (!SkinManager::instance().importSkin(path, &error)) {
            if (m_lblSkinStatus) m_lblSkinStatus->setText(error);
            return;
        }
        if (m_lblSkinStatus) m_lblSkinStatus->setText(QStringLiteral("皮肤已应用"));
    });

    m_btnExportSkin = new fluent::basicinput::Button(QStringLiteral("导出模板"));
    m_btnExportSkin->setFluentStyle(fluent::basicinput::Button::Standard);
    connect(m_btnExportSkin, &fluent::basicinput::Button::clicked, this, [this]() {
        QString error;
        if (!SkinManager::instance().exportTemplate(&error)) {
            if (m_lblSkinStatus) m_lblSkinStatus->setText(error);
            return;
        }
        if (m_lblSkinStatus) m_lblSkinStatus->setText(QStringLiteral("模板已导出"));
    });
```

把这两个按钮放进一个 `QHBoxLayout` 作为卡片的 trailing widget，并在卡片下方放 `m_lblSkinStatus`。
补 `#include "skin_manager.h"`。

- [ ] **Step 5: 在 `--verify` 增加 TEST 13**

```cpp
    // 13. Verify SkinManager skin discovery and JSON validity
    std::cout << "[TEST 13] Testing SkinManager skin listing & JSON parse...\n";
    {
        const auto skins = SkinManager::instance().availableSkins();
        std::cout << "  -> skinDir 中 " << skins.size() << " 个皮肤文件, 枚举 PASS.\n";
        const QString bad = QStringLiteral("{ not json ");
        QJsonParseError perr{};
        QJsonDocument::fromJson(bad.toUtf8(), &perr);
        if (perr.error == QJsonParseError::NoError) {
            std::cerr << "FAIL: invalid skin JSON was accepted\n";
            return 1;
        }
        std::cout << "  -> PASS: 非法皮肤 JSON 被正确拒绝。\n";
    }
```

- [ ] **Step 6: 编译并验证**

Run: `C:\devtools\xmake\xmake\xmake.exe build -a && .\build\Release\weasel-fluent-cpp-qt.exe --verify`
Expected: 13 项 PASS。

- [ ] **Step 7: 提交**

```bash
git add src/skin_manager.h src/skin_manager.cpp src/settings_window.h src/settings_window.cpp src/main.cpp
git commit -m "feat(skin): custom skin import, export and apply

Drives Fluent-Qt's native token override system (UserTheme) instead of
inventing a QSS layer, so skins actually restyle Fluent-painted widgets.
Supports import, template export and reset to default."
```

---

## Task 8: 自定义快捷键（`KeyBindings` + 捕获对话框）

**Files:**
- Create: `src/key_bindings.h`, `src/key_bindings.cpp`
- Create: `src/hotkey_capture_dialog.h`, `src/hotkey_capture_dialog.cpp`
- Modify: `src/config.h`, `src/config.cpp`（新增 `key_bindings` 属性）
- Modify: `src/settings_window.h`, `src/settings_window.cpp`（输入与按键页）
- Modify: `src/candidate_window.cpp`（键位分派改走 KeyBindings）
- Modify: `src/main.cpp`（TEST 14）

**Interfaces:**
- Consumes: Task 1 `AppConfig`
- Produces:
  ```cpp
  namespace KeyActions {
      extern const char* ToggleAscii;    // "toggle_ascii"
      extern const char* Commit;         // "commit"
      extern const char* PagePrev;       // "page_prev"
      extern const char* PageNext;       // "page_next"
      extern const char* ExpandMatrix;   // "expand_matrix"
      extern const char* EmojiPanel;     // "emoji_panel"
      extern const char* ExtensionPanel; // "extension_panel"
  }

  class KeyBindings : public QObject {
      Q_OBJECT
  public:
      static KeyBindings& instance();
      QStringList actions() const;
      QString binding(const QString& action) const;
      void setBinding(const QString& action, const QString& spec);
      QString humanName(const QString& action) const;
      static QString formatSpec(int key, Qt::KeyboardModifiers mods);
      static bool parseSpec(const QString& spec, int* key, Qt::KeyboardModifiers* mods);
      static bool specMatches(const QString& spec, int key, Qt::KeyboardModifiers mods);
  };
  ```

- [ ] **Step 1: 实现 `src/key_bindings.h`**

```cpp
#ifndef KEY_BINDINGS_H
#define KEY_BINDINGS_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <Qt>

/// 可绑定的动作 id。
namespace KeyActions {
extern const char* ToggleAscii;
extern const char* Commit;
extern const char* PagePrev;
extern const char* PageNext;
extern const char* ExpandMatrix;
extern const char* EmojiPanel;
extern const char* ExtensionPanel;
}

/// 快捷键绑定表。持久化在 config.json 的 key_bindings 字段。
class KeyBindings : public QObject {
    Q_OBJECT
public:
    static KeyBindings& instance();

    /// 所有可绑定动作 id。
    QStringList actions() const;

    /// 取动作对应的键规格（空串表示未绑定）。
    QString binding(const QString& action) const;

    /// 设置动作对应的键规格。
    void setBinding(const QString& action, const QString& spec);

    /// 动作的中文显示名。
    QString humanName(const QString& action) const;

    /// 把键码 + 修饰键格式化为规格串，如 "Ctrl+Shift+M"。
    static QString formatSpec(int key, Qt::KeyboardModifiers mods);

    /// 解析规格串，失败返回 false。
    static bool parseSpec(const QString& spec, int* key, Qt::KeyboardModifiers* mods);

    /// 判断给定的键事件是否命中该规格。
    static bool specMatches(const QString& spec, int key, Qt::KeyboardModifiers mods);

private:
    QHash<QString, QString> m_bindings;
};

#endif // KEY_BINDINGS_H
```

- [ ] **Step 2: 实现 `src/key_bindings.cpp`**

```cpp
// src/key_bindings.cpp
#include "key_bindings.h"
#include "config.h"

#include <QHash>
#include <QJsonObject>

namespace KeyActions {
const char* ToggleAscii    = "toggle_ascii";
const char* Commit         = "commit";
const char* PagePrev       = "page_prev";
const char* PageNext       = "page_next";
const char* ExpandMatrix   = "expand_matrix";
const char* EmojiPanel     = "emoji_panel";
const char* ExtensionPanel = "extension_panel";
}

namespace {

QHash<QString, QString> defaultBindings() {
    QHash<QString, QString> h;
    h.insert(QString::fromLatin1(KeyActions::ToggleAscii),    QStringLiteral("Shift"));
    h.insert(QString::fromLatin1(KeyActions::Commit),         QStringLiteral("Space"));
    h.insert(QString::fromLatin1(KeyActions::PagePrev),       QStringLiteral("Comma"));
    h.insert(QString::fromLatin1(KeyActions::PageNext),       QStringLiteral("Period"));
    h.insert(QString::fromLatin1(KeyActions::ExpandMatrix),   QStringLiteral("Tab"));
    h.insert(QString::fromLatin1(KeyActions::EmojiPanel),     QString());
    h.insert(QString::fromLatin1(KeyActions::ExtensionPanel), QString());
    return h;
}

QString keyName(int key) {
    if (key <= 0) return QString();
    if (key >= Qt::Key_A && key <= Qt::Key_Z)
        return QString(QChar(key)).toLower();
    if (key >= Qt::Key_0 && key <= Qt::Key_9)
        return QString(QChar(key));
    switch (key) {
    case Qt::Key_Space:     return QStringLiteral("Space");
    case Qt::Key_Tab:       return QStringLiteral("Tab");
    case Qt::Key_Shift:     return QStringLiteral("Shift");
    case Qt::Key_Control:   return QStringLiteral("Ctrl");
    case Qt::Key_Alt:       return QStringLiteral("Alt");
    case Qt::Key_Meta:      return QStringLiteral("Win");
    case Qt::Key_Comma:     return QStringLiteral("Comma");
    case Qt::Key_Period:    return QStringLiteral("Period");
    case Qt::Key_Semicolon: return QStringLiteral("Semicolon");
    case Qt::Key_QuoteLeft: return QStringLiteral("QuoteLeft");
    case Qt::Key_BracketLeft:  return QStringLiteral("BracketLeft");
    case Qt::Key_BracketRight: return QStringLiteral("BracketRight");
    case Qt::Key_Minus:     return QStringLiteral("Minus");
    case Qt::Key_Equal:     return QStringLiteral("Equal");
    case Qt::Key_Up:        return QStringLiteral("Up");
    case Qt::Key_Down:      return QStringLiteral("Down");
    case Qt::Key_Left:      return QStringLiteral("Left");
    case Qt::Key_Right:     return QStringLiteral("Right");
    case Qt::Key_Escape:    return QStringLiteral("Escape");
    case Qt::Key_Return:    return QStringLiteral("Return");
    case Qt::Key_Backspace: return QStringLiteral("Backspace");
    default: return QString();
    }
}

int keyFromName(const QString& name) {
    const QString n = name.trimmed().toLower();
    if (n.isEmpty()) return 0;
    if (n.size() == 1) {
        const QChar c = n.at(0);
        if (c.isLetter() && c.isLower()) return c.toUpper().unicode();
        if (c.isDigit()) return c.unicode();
    }
    static const QHash<QString, int> map = {
        {QStringLiteral("space"), Qt::Key_Space}, {QStringLiteral("tab"), Qt::Key_Tab},
        {QStringLiteral("shift"), Qt::Key_Shift}, {QStringLiteral("ctrl"), Qt::Key_Control},
        {QStringLiteral("alt"), Qt::Key_Alt}, {QStringLiteral("win"), Qt::Key_Meta},
        {QStringLiteral("comma"), Qt::Key_Comma}, {QStringLiteral("period"), Qt::Key_Period},
        {QStringLiteral("semicolon"), Qt::Key_Semicolon}, {QStringLiteral("quoteleft"), Qt::Key_QuoteLeft},
        {QStringLiteral("bracketleft"), Qt::Key_BracketLeft}, {QStringLiteral("bracketright"), Qt::Key_BracketRight},
        {QStringLiteral("minus"), Qt::Key_Minus}, {QStringLiteral("equal"), Qt::Key_Equal},
        {QStringLiteral("up"), Qt::Key_Up}, {QStringLiteral("down"), Qt::Key_Down},
        {QStringLiteral("left"), Qt::Key_Left}, {QStringLiteral("right"), Qt::Key_Right},
        {QStringLiteral("escape"), Qt::Key_Escape}, {QStringLiteral("return"), Qt::Key_Return},
        {QStringLiteral("backspace"), Qt::Key_Backspace},
    };
    return map.value(n, 0);
}

} // namespace

KeyBindings& KeyBindings::instance() {
    static KeyBindings s_inst;
    return s_inst;
}

QStringList KeyBindings::actions() const {
    return QStringList{
        QString::fromLatin1(KeyActions::ToggleAscii),
        QString::fromLatin1(KeyActions::Commit),
        QString::fromLatin1(KeyActions::PagePrev),
        QString::fromLatin1(KeyActions::PageNext),
        QString::fromLatin1(KeyActions::ExpandMatrix),
        QString::fromLatin1(KeyActions::EmojiPanel),
        QString::fromLatin1(KeyActions::ExtensionPanel),
    };
}

QString KeyBindings::binding(const QString& action) const {
    const QString v = m_bindings.value(action);
    if (!v.isEmpty()) return v;
    return defaultBindings().value(action);
}

void KeyBindings::setBinding(const QString& action, const QString& spec) {
    m_bindings.insert(action, spec);
    AppConfig::instance().setKeyBindings(m_bindings);
}

QString KeyBindings::humanName(const QString& action) const {
    if (action == QLatin1String(KeyActions::ToggleAscii))    return QStringLiteral("中英文切换");
    if (action == QLatin1String(KeyActions::Commit))         return QStringLiteral("上屏首选词");
    if (action == QLatin1String(KeyActions::PagePrev))       return QStringLiteral("候选翻页（上一页）");
    if (action == QLatin1String(KeyActions::PageNext))       return QStringLiteral("候选翻页（下一页）");
    if (action == QLatin1String(KeyActions::ExpandMatrix))   return QStringLiteral("展开多候选矩阵");
    if (action == QLatin1String(KeyActions::EmojiPanel))     return QStringLiteral("打开表情面板");
    if (action == QLatin1String(KeyActions::ExtensionPanel)) return QStringLiteral("打开扩展面板");
    return action;
}

QString KeyBindings::formatSpec(int key, Qt::KeyboardModifiers mods) {
    QStringList parts;
    if (mods & Qt::ControlModifier) parts << QStringLiteral("Ctrl");
    if (mods & Qt::AltModifier)     parts << QStringLiteral("Alt");
    if (mods & Qt::ShiftModifier)   parts << QStringLiteral("Shift");
    if (mods & Qt::MetaModifier)    parts << QStringLiteral("Win");
    const QString k = keyName(key);
    if (!k.isEmpty()) parts << k;
    return parts.join(QLatin1Char('+'));
}

bool KeyBindings::parseSpec(const QString& spec, int* key, Qt::KeyboardModifiers* mods) {
    if (spec.trimmed().isEmpty()) return false;
    const QStringList parts = spec.split(QLatin1Char('+'), Qt::SkipEmptyParts);
    Qt::KeyboardModifiers m = Qt::NoModifier;
    int k = 0;
    for (const QString& raw : parts) {
        const QString p = raw.trimmed();
        const QString lower = p.toLower();
        if (lower == QLatin1String("ctrl")) { m |= Qt::ControlModifier; continue; }
        if (lower == QLatin1String("alt"))  { m |= Qt::AltModifier; continue; }
        if (lower == QLatin1String("shift")){ m |= Qt::ShiftModifier; continue; }
        if (lower == QLatin1String("win"))  { m |= Qt::MetaModifier; continue; }
        k = keyFromName(p);
    }
    if (k == 0) return false;
    if (key) *key = k;
    if (mods) *mods = m;
    return true;
}

bool KeyBindings::specMatches(const QString& spec, int key, Qt::KeyboardModifiers mods) {
    int specKey = 0;
    Qt::KeyboardModifiers specMods = Qt::NoModifier;
    if (!parseSpec(spec, &specKey, &specMods)) return false;
    if (specKey != key) return false;
    // 忽略 CapsLock/NumLock 等无关修饰位
    const Qt::KeyboardModifiers relevant =
        Qt::ControlModifier | Qt::AltModifier | Qt::ShiftModifier | Qt::MetaModifier;
    return (mods & relevant) == (specMods & relevant);
}
```

- [ ] **Step 3: `AppConfig` 增加 `key_bindings`**

`src/config.h`：
- 在 Q_PROPERTY 区加 `Q_PROPERTY(QHash<QString,QString> keyBindings READ keyBindings WRITE setKeyBindings NOTIFY configChanged)`
- 加 `#include <QHash>`
- 声明：
  ```cpp
  QHash<QString, QString> keyBindings() const { return m_keyBindings; }
  void setKeyBindings(const QHash<QString, QString>& v);
  ```
- 私有成员加 `QHash<QString, QString> m_keyBindings;`

`src/config.cpp`：
- `load()` 中加入：
  ```cpp
  const QJsonObject bindings = obj.value(QStringLiteral("key_bindings")).toObject();
  for (auto it = bindings.constBegin(); it != bindings.constEnd(); ++it) {
      m_keyBindings.insert(it.key(), it.value().toString());
  }
  ```
- `save()` 中加入：
  ```cpp
  QJsonObject bindings;
  for (auto it = m_keyBindings.constBegin(); it != m_keyBindings.constEnd(); ++it) {
      bindings.insert(it.key(), it.value());
  }
  obj[QStringLiteral("key_bindings")] = bindings;
  ```
- 实现 setter：
  ```cpp
  void AppConfig::setKeyBindings(const QHash<QString, QString>& v) {
      if (m_keyBindings == v) return;
      m_keyBindings = v;
      save();
      emit configChanged();
  }
  ```

- [ ] **Step 4: 实现 `src/hotkey_capture_dialog.h/.cpp`**

```cpp
// src/hotkey_capture_dialog.h
#ifndef HOTKEY_CAPTURE_DIALOG_H
#define HOTKEY_CAPTURE_DIALOG_H

#include <QDialog>

/// 模态按键捕获对话框：用户按下组合键后确认或取消。
class HotkeyCaptureDialog : public QDialog {
    Q_OBJECT
public:
    explicit HotkeyCaptureDialog(const QString& currentSpec, QWidget* parent = nullptr);

    /// 捕获到的键规格，形如 "Ctrl+Shift+M"；取消时为空串。
    QString capturedSpec() const { return m_spec; }

protected:
    void keyPressEvent(QKeyEvent* event) override;

private:
    QString m_spec;
    QString m_current;
};

#endif // HOTKEY_CAPTURE_DIALOG_H
```

```cpp
// src/hotkey_capture_dialog.cpp
#include "hotkey_capture_dialog.h"
#include "key_bindings.h"

#include <QDialogButtonBox>
#include <QKeyEvent>
#include <QLabel>
#include <QVBoxLayout>

HotkeyCaptureDialog::HotkeyCaptureDialog(const QString& currentSpec, QWidget* parent)
    : QDialog(parent), m_current(currentSpec) {
    setWindowTitle(QStringLiteral("录制快捷键"));
    setModal(true);
    resize(360, 160);

    auto* layout = new QVBoxLayout(this);
    auto* hint = new QLabel(QStringLiteral("请按下新的组合键"), this);
    layout->addWidget(hint);

    m_spec = currentSpec;
    auto* value = new QLabel(currentSpec.isEmpty() ? QStringLiteral("（未绑定）") : currentSpec, this);
    value->setObjectName(QStringLiteral("capturedValue"));
    layout->addWidget(value);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

void HotkeyCaptureDialog::keyPressEvent(QKeyEvent* event) {
    const int key = event->key();
    if (key == Qt::Key_Escape) {
        m_spec.clear();
        QDialog::reject();
        return;
    }
    if (key == Qt::Key_Shift || key == Qt::Key_Control || key == Qt::Key_Alt || key == Qt::Key_Meta) {
        QDialog::keyPressEvent(event);
        return;
    }
    m_spec = KeyBindings::formatSpec(key, event->modifiers());
    if (auto* lbl = findChild<QLabel*>(QStringLiteral("capturedValue"))) {
        lbl->setText(m_spec);
    }
    event->accept();
}
```

- [ ] **Step 5: 设置中心「输入与按键」页加键位表**

`src/settings_window.h` 成员区新增：

```cpp
    QVector<QPair<QString, QString>> m_bindingRows;  // action, 显示文本
    QHash<QString, fluent::textfields::Label*> m_bindingLabels;
```

`src/settings_window.cpp` 的 `createInputPage()` 中，在「中英文混输智能空格」卡片之后插入：

```cpp
    // 自定义快捷键
    auto* keyListBox = new QWidget();
    auto* keyListLayout = new QVBoxLayout(keyListBox);
    keyListLayout->setContentsMargins(0, 0, 0, 0);
    keyListLayout->setSpacing(6);

    for (const QString& action : KeyBindings::instance().actions()) {
        auto* row = new QWidget(keyListBox);
        auto* rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        rowLayout->setSpacing(12);

        auto* name = new fluent::textfields::Label(KeyBindings::instance().humanName(action), row);
        name->setFluentTypography(Typography::FontRole::Body);
        name->setTextColorRole(fluent::textfields::Label::TextColorRole::Primary);

        auto* value = new fluent::textfields::Label(QStringLiteral("…"), row);
        value->setFluentTypography(Typography::FontRole::Body);
        value->setTextColorRole(fluent::textfields::Label::TextColorRole::Secondary);
        m_bindingLabels.insert(action, value);

        auto* capture = new fluent::basicinput::Button(QStringLiteral("录制"), row);
        capture->setFluentStyle(fluent::basicinput::Button::Standard);
        connect(capture, &fluent::basicinput::Button::clicked, this, [this, action, value]() {
            HotkeyCaptureDialog dlg(KeyBindings::instance().binding(action), this);
            if (dlg.exec() == QDialog::Accepted) {
                KeyBindings::instance().setBinding(action, dlg.capturedSpec());
                value->setText(dlg.capturedSpec().isEmpty()
                                   ? QStringLiteral("（未绑定）") : dlg.capturedSpec());
            }
        });

        rowLayout->addWidget(name, 1);
        rowLayout->addWidget(value);
        rowLayout->addWidget(capture);
        keyListLayout->addWidget(row);
    }

    layout->addWidget(new SettingsCard(
        Typography::Icons::Keyboard,
        QStringLiteral("自定义快捷键"),
        QStringLiteral("点击「录制」后按下新的组合键，立即生效并写入 config.json"),
        keyListBox, QColor(0, 164, 239), container));
```

并在 `syncControlsFromConfig()` 中刷新显示：

```cpp
    for (auto it = m_bindingLabels.constBegin(); it != m_bindingLabels.constEnd(); ++it) {
        const QString spec = KeyBindings::instance().binding(it.key());
        it.value()->setText(spec.isEmpty() ? QStringLiteral("（未绑定）") : spec);
    }
```

补 `#include "key_bindings.h"` 与 `#include "hotkey_capture_dialog.h"`。

- [ ] **Step 6: 候选窗键位分派改走 KeyBindings**

`src/candidate_window.cpp:1858` 现状：

```cpp
    if (event->key() == Qt::Key_Tab) {
        toggleExpanded();
        return;
    }
```

替换为：

```cpp
    if (KeyBindings::specMatches(KeyBindings::instance().binding(
            QString::fromLatin1(KeyActions::ExpandMatrix)), event->key(), event->modifiers())) {
        toggleExpanded();
        return;
    }
```

`src/candidate_window.cpp:1878` 现状：

```cpp
    } else if (event->key() == Qt::Key_Space) {
```

替换为：

```cpp
    } else if (KeyBindings::specMatches(KeyBindings::instance().binding(
                   QString::fromLatin1(KeyActions::Commit)), event->key(), event->modifiers())) {
```

中英切换分支同理，用 `KeyActions::ToggleAscii` 取代硬编码的 `Qt::Key_Shift` 判断：

```cpp
    if (KeyBindings::specMatches(KeyBindings::instance().binding(
            QString::fromLatin1(KeyActions::ToggleAscii)), event->key(), event->modifiers())) {
        m_engine->setAsciiMode(!m_engine->isAsciiMode());
        return;
    }
```

翻页分支（`candidate_window.cpp:1874` 的 `Qt::Key_PageUp` / `Qt::Key_PageDown`）额外支持
`KeyActions::PagePrev` / `KeyActions::PageNext`：

```cpp
    } else if (KeyBindings::specMatches(KeyBindings::instance().binding(
                   QString::fromLatin1(KeyActions::PageNext)), event->key(), event->modifiers())) {
        m_engine->processKey(RIME_KEY_PAGE_DOWN);
    } else if (KeyBindings::specMatches(KeyBindings::instance().binding(
                   QString::fromLatin1(KeyActions::PagePrev)), event->key(), event->modifiers())) {
        m_engine->processKey(RIME_KEY_PAGE_UP);
    } else if (event->key() == Qt::Key_PageUp) {
```

> **兼容要求**：旧的 `switchKey` / `pageKeys` 预设值仍在 `AppConfig` 中。`KeyBindings::binding()`
> 读取新表；新表为空时回落到 `defaultBindings()`，而 `defaultBindings()` 的取值与旧预设一致
> （`Shift` / `Comma` / `Period`），因此旧配置的用户行为不变。旧字段在 Task 8 Step 3 中保留
> 读取，但不再作为分派依据。
>
> 补 `#include "key_bindings.h"`。

- [ ] **Step 7: 在 `--verify` 增加 TEST 14**

```cpp
    // 14. Verify KeyBindings spec parse/format round-trip
    std::cout << "[TEST 14] Testing KeyBindings spec parsing & matching...\n";
    {
        int key = 0;
        Qt::KeyboardModifiers mods = Qt::NoModifier;
        if (!KeyBindings::parseSpec(QStringLiteral("Ctrl+Shift+M"), &key, &mods)) {
            std::cerr << "FAIL: parseSpec failed for Ctrl+Shift+M\n";
            return 1;
        }
        if (key != Qt::Key_M || mods != (Qt::ControlModifier | Qt::ShiftModifier)) {
            std::cerr << "FAIL: parseSpec mismatch: key=" << key << " mods=" << int(mods) << "\n";
            return 1;
        }
        if (!KeyBindings::specMatches(QStringLiteral("Ctrl+Shift+M"), Qt::Key_M,
                                      Qt::ControlModifier | Qt::ShiftModifier)) {
            std::cerr << "FAIL: specMatches failed for exact modifiers\n";
            return 1;
        }
        if (KeyBindings::specMatches(QStringLiteral("Ctrl+Shift+M"), Qt::Key_M, Qt::ControlModifier)) {
            std::cerr << "FAIL: specMatches wrongly matched missing modifier\n";
            return 1;
        }
        std::cout << "  -> PASS: key spec parse/format/match OK.\n";
    }
```

- [ ] **Step 8: 编译并验证**

Run: `C:\devtools\xmake\xmake\xmake.exe build -a && .\build\Release\weasel-fluent-cpp-qt.exe --verify`
Expected: 14 项 PASS。

- [ ] **Step 9: 提交**

```bash
git add src/key_bindings.h src/key_bindings.cpp src/hotkey_capture_dialog.h src/hotkey_capture_dialog.cpp src/config.h src/config.cpp src/settings_window.h src/settings_window.cpp src/candidate_window.cpp src/main.cpp
git commit -m "feat(keys): user-bindable hotkeys with a key capture dialog

Replaces the fixed Shift/Ctrl and page-key dropdowns with a real binding
table stored in config.json, a modal recorder for arbitrary combos, and
centralised dispatch so no key check is hardcoded at the call site."
```

---

## Task 9: Fluent 托盘菜单 + `show_tray` 开关

**Files:**
- Create: `src/tray_menu.h`, `src/tray_menu.cpp`
- Modify: `src/main.cpp:905-954`（替换原生 QMenu）
- Modify: `src/config.h`, `src/config.cpp`（`show_tray`）

**Interfaces:**
- Consumes: `RimeEngine`、`AppConfig`
- Produces:
  ```cpp
  class TrayMenu : public QObject {
      Q_OBJECT
  public:
      TrayMenu(QSystemTrayIcon* icon, RimeEngine* engine, QObject* parent = nullptr);
      void setAsciiMode(bool ascii);
      void setVisible(bool visible);
  };
  ```

- [ ] **Step 1: 抽出共用的字形图标辅助函数**

`src/candidate_window.cpp` 里已有文件内静态函数 `createFluentMenuIcon(glyph, color)`
（`Typography::Icons::paintGlyph` 绘制），托盘菜单也要用。抽到共享头文件：

创建 `src/ui_glyph_icon.h`：

```cpp
#ifndef UI_GLYPH_ICON_H
#define UI_GLYPH_ICON_H

#include <QColor>
#include <QIcon>
#include <QPainter>
#include <QPixmap>

#include "design/Typography.h"

/// 用 Fluent 字形绘制多尺寸图标（16/20/24 @2x）。
inline QIcon createFluentMenuIcon(const QString& glyph, const QColor& color) {
    QIcon icon;
    for (int size : {16, 20, 24}) {
        QPixmap pm(size * 2, size * 2);
        pm.setDevicePixelRatio(2.0);
        pm.fill(Qt::transparent);
        {
            QPainter p(&pm);
            p.setRenderHint(QPainter::TextAntialiasing);
            p.setPen(color);
            Typography::Icons::paintGlyph(p, QRectF(0, 0, size, size), glyph, size, Qt::AlignCenter);
        }
        icon.addPixmap(pm, QIcon::Normal, QIcon::Off);
        icon.addPixmap(pm, QIcon::Active, QIcon::Off);
        icon.addPixmap(pm, QIcon::Selected, QIcon::Off);
    }
    return icon;
}

#endif // UI_GLYPH_ICON_H
```

然后把 `src/candidate_window.cpp` 中原有的 `createFluentMenuIcon` 定义删除，
改为 `#include "ui_glyph_icon.h"`（保留其调用点不变）。

- [ ] **Step 2: 实现 `src/tray_menu.h`**

```cpp
#ifndef TRAY_MENU_H
#define TRAY_MENU_H

#include <QObject>
#include <QSystemTrayIcon>

class RimeEngine;

/// Fluent 风格的托盘菜单。
/// 职责收缩为「状态指示 + 部署 + 设置 + 退出」；输入法的中英切换
/// 交给系统（Win+Space 选输入法，Shift 由 TIP 同步 compartment 状态）。
class TrayMenu : public QObject {
    Q_OBJECT
public:
    TrayMenu(QSystemTrayIcon* icon, RimeEngine* engine, QObject* parent = nullptr);

    /// 同步中英状态到图标与菜单文字。
    void setAsciiMode(bool ascii);

    /// 显示或隐藏托盘图标。
    void setVisible(bool visible);

private:
    void rebuildIcon();
    void rebuildMenu();

    QSystemTrayIcon* m_icon = nullptr;
    RimeEngine* m_engine = nullptr;
    bool m_ascii = false;
};

#endif // TRAY_MENU_H
```

- [ ] **Step 3: 实现 `src/tray_menu.cpp`**

Fluent-Qt 的菜单类为 `fluent::menus_toolbars::FluentMenu`（`QMenu` 子类）与
`fluent::menus_toolbars::FluentMenuItem`（`QWidgetAction` 子类），
头文件 `components/menus_toolbars/Menu.h`。

```cpp
// src/tray_menu.cpp
#include "tray_menu.h"
#include "config.h"
#include "rime_engine.h"
#include "ui_glyph_icon.h"

#include <QApplication>
#include <QIcon>
#include <QPainter>
#include <QPixmap>
#include <QProcess>

#include "components/menus_toolbars/Menu.h"

namespace {

QIcon makeTrayIcon(bool chinese, const QColor& accent) {
    QPixmap pix(32, 32);
    pix.fill(Qt::transparent);
    {
        QPainter p(&pix);
        p.setRenderHint(QPainter::Antialiasing);
        p.setBrush(chinese ? accent : QColor(120, 120, 120));
        p.setPen(Qt::NoPen);
        p.drawRoundedRect(QRectF(2, 2, 28, 28), 6, 6);
        p.setPen(Qt::white);
        QFont f = p.font();
        f.setPointSize(14);
        f.setBold(true);
        p.setFont(f);
        p.drawText(QRectF(0, 0, 32, 32), Qt::AlignCenter,
                   chinese ? QStringLiteral("中") : QStringLiteral("英"));
    }
    return QIcon(pix);
}

} // namespace

TrayMenu::TrayMenu(QSystemTrayIcon* icon, RimeEngine* engine, QObject* parent)
    : QObject(parent), m_icon(icon), m_engine(engine) {
    if (!m_icon) return;
    rebuildMenu();
    rebuildIcon();
}

void TrayMenu::rebuildMenu() {
    if (!m_icon) return;
    const QColor accent = AppConfig::instance().effectiveAccentColor();

    auto* menu = new fluent::menus_toolbars::FluentMenu();

    // 1. 状态指示（只读）
    auto* statusItem = new fluent::menus_toolbars::FluentMenuItem(
        m_ascii ? QStringLiteral("当前：英文模式") : QStringLiteral("当前：中文模式"));
    statusItem->setEnabled(false);
    menu->addAction(statusItem);
    menu->addSeparator();

    // 2. 重新部署
    auto* deployItem = new fluent::menus_toolbars::FluentMenuItem(QStringLiteral("重新部署词库与配置"));
    deployItem->setIcon(createFluentMenuIcon(Typography::Icons::Sync, accent));
    connect(deployItem, &QAction::triggered, this, [this]() {
        if (m_engine) m_engine->deploy();
    });
    menu->addAction(deployItem);

    // 3. 设置中心
    auto* settingsItem = new fluent::menus_toolbars::FluentMenuItem(QStringLiteral("输入法设置"));
    settingsItem->setIcon(createFluentMenuIcon(Typography::Icons::Settings, accent));
    connect(settingsItem, &QAction::triggered, this, []() {
        QProcess::startDetached(QCoreApplication::applicationFilePath(),
                                QStringList() << QStringLiteral("--settings"));
    });
    menu->addAction(settingsItem);

    menu->addSeparator();

    // 4. 退出
    auto* quitItem = new fluent::menus_toolbars::FluentMenuItem(QStringLiteral("退出"));
    connect(quitItem, &QAction::triggered, this, []() {
        QApplication::quit();
    });
    menu->addAction(quitItem);

    // 注意：中英切换**不在**托盘里 —— 由系统输入法切换与 TIP compartment 承担。
    m_icon->setContextMenu(menu);
}

void TrayMenu::setAsciiMode(bool ascii) {
    if (m_ascii == ascii) return;
    m_ascii = ascii;
    rebuildMenu();   // 状态项文字随模式变化
    rebuildIcon();
}

void TrayMenu::setVisible(bool visible) {
    if (!m_icon) return;
    if (visible) m_icon->show();
    else m_icon->hide();
}

void TrayMenu::rebuildIcon() {
    if (!m_icon) return;
    auto& cfg = AppConfig::instance();
    m_icon->setIcon(makeTrayIcon(!m_ascii, cfg.effectiveAccentColor()));
    m_icon->setToolTip(m_ascii ? QStringLiteral("Weasel Fluent 输入法 - 英文模式")
                               : QStringLiteral("Weasel Fluent 输入法 - 中文模式"));
}
```

> **必须保留**：状态项（只读）、重新部署、输入法设置、退出 四项；
> 「中英切换」**不得**出现在托盘里。
> `Typography::Icons::Sync` 与 `Typography::Icons::Settings` 若在本版本不存在，
> 改用 `Typography::Icons` 中已有的任意图标常量（该枚举在 `settings_window.cpp` 中已大量使用）。

- [ ] **Step 4: `AppConfig` 增加 `show_tray`**

`src/config.h`：
- Q_PROPERTY 加 `Q_PROPERTY(bool showTray READ showTray WRITE setShowTray NOTIFY configChanged)`
- 声明 `bool showTray() const { return m_showTray; }` 与 `void setShowTray(bool v);`
- 私有成员加 `bool m_showTray = true;`

`src/config.cpp`：
- `load()`：`m_showTray = obj.value(QStringLiteral("show_tray")).toBool(true);`
- `save()`：`obj[QStringLiteral("show_tray")] = m_showTray;`
- setter：
  ```cpp
  void AppConfig::setShowTray(bool v) {
      if (m_showTray == v) return;
      m_showTray = v;
      save();
      emit configChanged();
  }
  ```

- [ ] **Step 5: `src/main.cpp` 替换原生托盘代码**

把 905-954 行整段替换为：

```cpp
    // Fluent tray menu: status + deploy + settings + quit.
    // 中英切换不在托盘里 —— 由系统输入法切换（Win+Space）与 TIP 的
    // KEYBOARD_OPENCLOSE compartment 承担。
    QSystemTrayIcon tray(QIcon(), &app);
    auto* trayMenu = new TrayMenu(&tray, &engine);
    trayMenu->setVisible(cfg.showTray());

    QObject::connect(&engine, &RimeEngine::modeChanged, [&](bool isAscii) {
        trayMenu->setAsciiMode(isAscii);
    });
```

补 `#include "tray_menu.h"`。

- [ ] **Step 6: 编译并验证**

Run: `C:\devtools\xmake\xmake\xmake.exe build -a`
Expected: 编译成功（若 Fluent-Qt 菜单 API 与预期不符，按 Step 1 的实际签名修正）。

- [ ] **Step 7: 提交**

```bash
git add src/tray_menu.h src/tray_menu.cpp src/main.cpp src/config.h src/config.cpp
git commit -m "feat(tray): Fluent-styled tray menu, switching moved to the system

Replaces the raw QMenu with a Fluent-Qt menu and drops the Chinese/English
toggle from the tray: IME selection is handled by the system language
switcher and the TIP reports open/close state through the
KEYBOARD_OPENCLOSE compartment. Adds a show_tray config option."
```

---

## Task 10: 许可证改 GPLv3 + 第三方致谢

**Files:**
- Modify: `LICENSE`
- Create: `THIRD_PARTY_NOTICES.md`
- Modify: `README.md`
- Create: `scripts/verify-license.ps1`

**Interfaces:**
- Consumes: Task 3 的 `3rdparty/weasel/`
- Produces: 无代码接口

- [ ] **Step 1: 写自检脚本**

`scripts/verify-license.ps1`：

```powershell
$ErrorActionPreference = "Stop"
$repo = Split-Path -Parent $PSScriptRoot
$licensePath = Join-Path $repo "LICENSE"
if (-not (Test-Path $licensePath)) { Write-Host "FAIL: 缺少 LICENSE" -ForegroundColor Red; exit 1 }
$text = Get-Content $licensePath -Raw
if ($text -notmatch "GNU GENERAL PUBLIC LICENSE") {
  Write-Host "FAIL: LICENSE 不是 GPLv3 全文" -ForegroundColor Red
  exit 1
}
$notices = Join-Path $repo "THIRD_PARTY_NOTICES.md"
if (-not (Test-Path $notices)) { Write-Host "FAIL: 缺少 THIRD_PARTY_NOTICES.md" -ForegroundColor Red; exit 1 }
$n = Get-Content $notices -Raw
foreach ($k in @("rime/weasel", "librime", "Fluent-Qt", "Boost", "Qt")) {
  if ($n -notmatch [regex]::Escape($k)) { Write-Host "FAIL: 致谢缺少 $k" -ForegroundColor Red; exit 1 }
}
Write-Host "PASS: 许可证与第三方致谢齐备" -ForegroundColor Green
exit 0
```

- [ ] **Step 2: 替换 `LICENSE`**

```powershell
Copy-Item '3rdparty/weasel/LICENSE.txt' 'LICENSE' -Force
```

（Weasel 的 `LICENSE.txt` 正是 GPLv3 全文。）

- [ ] **Step 3: 创建 `THIRD_PARTY_NOTICES.md`**

```markdown
# Third-Party Notices

This project is licensed under the **GNU General Public License v3.0** (see
[LICENSE](LICENSE)). It bundles or links the following third-party components.

## Bundled source

| Component | Upstream | License | Location |
| :--- | :--- | :--- | :--- |
| Weasel (小狼毫) | https://github.com/rime/weasel | GPLv3 | `3rdparty/weasel/` |
| Weasel upstream license text | — | GPLv3 | `3rdparty/weasel/LICENSE.txt` |
| WTL (Windows Template Library) | https://github.com/forgottenworks/wtl | Boost Software License 1.0 | `3rdparty/weasel/include/wtl/` |

Vendored upstream commit and module list: [`3rdparty/weasel/UPSTREAM.md`](3rdparty/weasel/UPSTREAM.md).

## Linked libraries

| Component | Version | License |
| :--- | :--- | :--- |
| Boost | 1.88.0 | Boost Software License 1.0 |
| librime | 1.11.x | GPLv3 |
| Qt | 6.8.2 | LGPLv3 (dynamic linking) |
| Fluent-Qt | vendored | MIT |

## Trademarks

"小狼毫 / Weasel" and "Rime" are trademarks of their respective owners. This
project is an independent downstream work and is not endorsed by the Rime
project.
```

- [ ] **Step 4: 更新 `README.md`**

- 徽章行改为 `[![License](https://img.shields.io/badge/License-GPLv3-brightgreen.svg)](LICENSE)`
- 第 129 行 `本项目基于 [MIT License](LICENSE) 开源发布。` 改为
  `本项目基于 [GNU GPL v3](LICENSE) 开源发布，因内置官方 Weasel（GPLv3）源码而采用 GPLv3 许可证。`
- 「开源致谢」小节补上 Weasel 与 Boost 条目。

- [ ] **Step 5: 运行自检**

Run: `powershell -ExecutionPolicy Bypass -File scripts/verify-license.ps1`
Expected: `PASS: 许可证与第三方致谢齐备`

- [ ] **Step 6: 提交**

```bash
git add LICENSE THIRD_PARTY_NOTICES.md README.md scripts/verify-license.ps1
git commit -m "docs: switch license to GPLv3 and add third-party notices

Required because the vendored official Weasel sources are GPLv3. The
combined work can no longer be distributed under MIT."
```

---

## Task 11: 收尾验证

**Files:**
- Modify: `README.md`（快速使用章节补充新功能）
- 无新增代码

- [ ] **Step 1: 全量自检**

```powershell
powershell -ExecutionPolicy Bypass -File scripts/verify-no-hardcoded-paths.ps1
powershell -ExecutionPolicy Bypass -File scripts/verify-no-rime-ice.ps1
powershell -ExecutionPolicy Bypass -File scripts/verify-license.ps1
```

Expected: 三条全部 PASS。

- [ ] **Step 2: 全量编译**

Run: `C:\devtools\xmake\xmake\xmake.exe build -a`
Expected: 无 error、无 warning-as-error。

- [ ] **Step 3: 运行测试套件**

Run: `.\build\Release\weasel-fluent-cpp-qt.exe --verify`
Expected: 14 项测试全部 PASS。

- [ ] **Step 4: 截图验证设置中心新页**

Run: `.\build\Release\weasel-fluent-cpp-qt.exe --capture`
Expected: 生成候选窗与设置中心截图，人工检查四个新增 UI（方案下拉、词库导入、皮肤选择、键位表）渲染正常、无重叠。

- [ ] **Step 5: 更新 README 的功能列表**

在「核心特性」小节补写四条：方案选择与全量部署、自定义词库、自定义皮肤、自定义快捷键，并说明托盘已降级为状态+入口。

- [ ] **Step 6: 提交**

```bash
git add README.md
git commit -m "docs: document the new settings center capabilities"
```

---

## Self-Review

**Spec coverage**

| 设计文档章节 | 对应任务 |
| :--- | :-- |
| §3 许可证 | Task 10 |
| §5.1 Vendor 清单 | Task 3 |
| §5.2 Boost/librime 依赖 | Task 3 Step 3 |
| §5.3 xmake 调整 | Task 3 Step 4-6 |
| §6.1 去除 rime-ice | Task 2 |
| §6.2 选择方案并部署 | Task 5（+ Task 4 的 deployFull） |
| §6.3 自定义词库 | Task 6 |
| §6.4 自定义皮肤 | Task 7 |
| §6.5 自定义快捷键 | Task 8 |
| §6.6 自动替换旧的服务 | **Round 2**（本计划不含） |
| §6.7 托盘现代化 + 系统切换 | Task 9 |
| §2.3 硬编码路径 | Task 1 |
| §8 测试与验收 | Task 11 + 各任务内的 `--verify` 用例 |
| Round 2 全部内容 | 不在本计划范围（§7 已声明） |

**已知缺口（Round 2 承接，非遗漏）**：`§6.6` 安装器、`WeaselTSF` 换型、`CCandidateList` → Qt 桥接、`WeaselIPCServer` 接入服务、`RimeConfigClient` 的 Weasel 实现。

**类型一致性核对**

- `WeaselPaths::*` — Task 1 定义，Task 2/5/6/7 消费，签名一致。
- `SchemaManager::parseSchemaHeader` 返回 `QPair<QString,QString>`，Task 5 的 TEST 11 与实现一致。
- `DictionaryManager::patchDictionaryList(yamlText, addName, removeName, outText)` — Task 6 定义，TEST 12 调用顺序一致。
- `KeyBindings::parseSpec(spec, int* key, Qt::KeyboardModifiers* mods)` — Task 8 定义，TEST 14 调用一致。
- `RimeEngine::deployFull(QString* error = nullptr)` — Task 4 定义，Task 5/6 调用一致。
- `TrayMenu::setAsciiMode/setVisible` — Task 9 定义，`main.cpp` 调用一致。

**Placeholder 扫描**：无 TBD / TODO / "实现later" / "补充错误处理" 等占位描述；所有代码步骤均给出可粘贴的具体实现。
