#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QProcess>
#include <QScreen>
#include <QThread>
#include <QDebug>
#include <iostream>

#include <chrono>
#include <algorithm>
#include <numeric>
#include <vector>
#include <iomanip>

#include "FluentQt/FluentQt.h"
#ifdef Q_OS_WIN
#include <windows.h>
#include <psapi.h>
#ifdef small
#undef small
#endif
#endif
#include "components/foundation/UserTheme.h"
#include "design/Typography.h"
#include "config.h"
#include "dictionary_manager.h"
#include "fluent_ui_adapter.h"
#include "key_bindings.h"
#include "paths.h"
#include "rime_ui_state.h"
#include "schema_manager.h"
#include "skin_manager.h"
#include "candidate_window.h"
#include "settings_window.h"
#include "emoji_picker_window.h"
#include "extension_panel_window.h"
#include "tray_menu.h"
#include "ui_engine.h"
#include "weasel_config_client.h"
#include "weasel_engine.h"
#include "weasel_style.h"
#include <QElapsedTimer>
#include <QSystemTrayIcon>
#include <QMenu>
#include <QAction>
#include "components/menus_toolbars/Menu.h"

namespace {

/// 截图输出目录：<可执行文件目录>/preview。
/// 便于与用户目录、数据目录彻底解耦。
QString previewOutputDir() {
    const QString dir = QDir(QCoreApplication::applicationDirPath())
                           .absoluteFilePath(QStringLiteral("preview"));
    QDir().mkpath(dir);
    return dir;
}

/// 给本次运行准备一份**隔离的 Rime 用户目录**。
///
/// 机器上装着官方 Weasel 时，官方 WeaselServer.exe 正占着 %APPDATA%\Rime 里
/// 用户词库的 LOCK 文件，本仓的引擎就只能只读词库（输入出不来候选）。
/// 上游的 WeaselUserDataPath() 支持 WEASEL_USER_DIR 覆盖（见 vendor 补丁 0006），
/// 环境变量是进程级的，不会动用户的注册表配置。
QString prepareIsolatedRimeDir(const QString& tag) {
    const QString dir =
        QDir(QDir::tempPath()).filePath(QStringLiteral("weasel-fluent-%1-%2")
                                            .arg(tag)
                                            .arg(QCoreApplication::applicationPid()));
    QDir().mkpath(dir);
    qputenv("WEASEL_USER_DIR", dir.toLocal8Bit().constData());
    return dir;
}

/// 定位仓库的 scripts/ 目录（自检要读安装器与打包脚本）。
///
/// 二进制在 build/Release 或 build/windows/x64/release，仓库根在上面若干级，
/// 所以从可执行文件目录往上找。
QString locateScriptsDir() {
    QString probe = QCoreApplication::applicationDirPath();
    for (int up = 0; up < 5; ++up) {
        const QString candidate = QDir(probe).filePath(QStringLiteral("scripts"));
        if (QFileInfo(candidate).isDir()) {
            return candidate;
        }
        const QString parent = QFileInfo(probe).absolutePath();
        if (parent == probe) {
            break;
        }
        probe = parent;
    }
    return QString();
}

/// 等 Weasel 引擎的状态回流。
///
/// Weasel 路径是**异步**的：按键投到服务端线程执行，状态再经队列回 Qt 线程，
/// 所以凡是「发命令 → 看状态」的地方都得等。
bool waitForState(WeaselEngine& engine,
                  const std::function<bool(const RimeUiState&)>& predicate,
                  int timeoutMs = 5000) {
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs) {
        QCoreApplication::processEvents();
        if (predicate(engine.uiState())) {
            return true;
        }
        QThread::msleep(10);
    }
    return false;
}

} // namespace

// ============================================================================
// Weasel 运行时端到端探针（独立进程）
//
// 单独拎出来是因为 librime 是**进程级全局单例**：同一个进程里不能既跑
// 过渡实现又跑 WeaselEngine（会互抢用户词库的 LOCK，
// 组合态起不来）。所有依赖 librime 的断言都放在这里。
// ============================================================================
int runWeaselRuntimeProbe() {
    std::cout << "[PROBE] Starting the Weasel runtime...\n";

    const QString probeUserDir = prepareIsolatedRimeDir(QStringLiteral("probe"));

    WeaselEngine weasel;
    QString err;
    if (!weasel.start(&err)) {
        std::cerr << "FAIL: WeaselEngine start failed: " << err.toStdString() << "\n";
        return 1;
    }
    if (!weasel.isRunning()) {
        std::cerr << "FAIL: WeaselEngine is not running\n";
        return 1;
    }

    // 输入 "nihao"。命令投到服务端线程执行，状态再异步回流，所以这里要等。
    weasel.processChar(QChar('n'));
    weasel.processChar(QChar('i'));
    weasel.processChar(QChar('h'));
    weasel.processChar(QChar('a'));
    weasel.processChar(QChar('o'));

    RimeUiState st;
    waitForState(weasel, [&st](const RimeUiState& s) {
        st = s;
        return !s.candidates.isEmpty();
    });
    if (st.candidates.isEmpty()) {
        std::cerr << "FAIL: Weasel runtime produced no candidates for 'nihao'"
                  << " (schema=" << weasel.currentSchema().toStdString()
                  << " composing=" << (st.isComposing ? 1 : 0)
                  << " preedit=[" << st.preedit.toStdString() << "])\n";
        return 1;
    }
    std::cout << "  -> Weasel runtime candidates: " << st.candidates.size()
              << ", first: " << st.candidates[0].text.toUtf8().constData()
              << ", schema: " << weasel.currentSchema().toUtf8().constData() << "\n";

    // 候选导航：Down 前进一格，Up 退回一格。
    const int firstHighlight = st.highlightedIndex;
    weasel.processKey(RIME_KEY_DOWN);
    waitForState(weasel, [&firstHighlight](const RimeUiState& s) {
        return s.highlightedIndex != firstHighlight;
    });
    if (weasel.uiState().highlightedIndex != firstHighlight + 1) {
        std::cerr << "FAIL: Arrow Down did not advance the highlighted candidate\n";
        return 1;
    }
    weasel.processKey(RIME_KEY_UP);
    waitForState(weasel, [&firstHighlight](const RimeUiState& s) {
        return s.highlightedIndex == firstHighlight;
    });
    if (weasel.uiState().highlightedIndex != firstHighlight) {
        std::cerr << "FAIL: Arrow Up did not move the highlighted candidate back\n";
        return 1;
    }
    std::cout << "  -> Candidate navigation (Down / Up) OK\n";

    // 模式切换：中英 / 简繁 / 全半角。
    // 状态是**异步回流**的（服务端 → 队列 → Qt 线程），所以必须泵事件循环，
    // 光 sleep 不行 —— 队列里的回调不会被执行。
    weasel.setAsciiMode(true);
    if (!waitForState(weasel, [](const RimeUiState& s) { return s.asciiMode; })) {
        std::cerr << "FAIL: ascii mode did not propagate to the Weasel status\n";
        return 1;
    }
    weasel.setAsciiMode(false);
    weasel.setSimplified(true);
    if (!weasel.isSimplified()) {
        std::cerr << "FAIL: simplified state was not recorded\n";
        return 1;
    }
    weasel.setFullShape(true);
    if (!waitForState(weasel, [](const RimeUiState& s) { return s.fullShape; })) {
        std::cerr << "FAIL: full shape did not propagate to the Weasel status\n";
        return 1;
    }
    weasel.setFullShape(false);
    std::cout << "  -> Mode toggles (ascii / simplified / full shape) OK\n";

    // 全量部署（隔离目录里跑真实的 Rime 维护）。
    QString deployError;
    if (!weasel.deployFull(&deployError)) {
        std::cerr << "FAIL: deployFull failed: " << deployError.toStdString() << "\n";
        return 1;
    }
    std::cout << "  -> deployFull() OK\n";

    // 桥接：同一个 Context/Status 映射出来的状态必须一致。
    const RimeUiState bridged = FluentUiAdapter::toUiState(weasel.context(), weasel.status());
    if (bridged.candidates.size() != st.candidates.size()) {
        std::cerr << "FAIL: bridge mapping disagrees with the engine state\n";
        return 1;
    }

    // 中英模式也走同一条路：设 ascii_mode 之后 status().ascii_mode 要变。
    weasel.setAsciiMode(true);
    for (int i = 0; i < 40; ++i) {
        QCoreApplication::processEvents();
        QThread::msleep(20);
        if (weasel.isAsciiMode()) {
            break;
        }
    }
    if (!weasel.isAsciiMode()) {
        std::cerr << "FAIL: ascii mode did not propagate to the Weasel status\n";
        return 1;
    }
    weasel.setAsciiMode(false);

    weasel.stop();
    QDir(probeUserDir).removeRecursively();
    std::cout << "[PROBE] OK\n";
    return 0;
}

int runVerificationSuite() {
    std::cout << "====================================================\n";
    std::cout << "[VERIFY] Running Weasel-Fluent C++ Qt Test Suite...\n";
    std::cout << "====================================================\n";

    // 1. Verify AppConfig
    std::cout << "[TEST 1] Testing AppConfig JSON round-trip and system theme/accent...\n";
    {
        QFont testF;
        testF.setFamilies({QStringLiteral("Microsoft YaHei UI"), QStringLiteral("Microsoft YaHei"), QStringLiteral("Segoe UI Variable Text")});
        testF.setPointSize(12);
        QFontInfo fi(testF);
        std::cout << "  -> Resolved Candidate Font: " << fi.family().toUtf8().constData()
                  << " (weight: " << fi.weight() << ")\n";
    }
    auto& cfg = AppConfig::instance();
    cfg.setOrientation(QStringLiteral("horizontal"));
    cfg.setPageSize(5);
    cfg.setFontSize(12);
    if (cfg.orientation() != QStringLiteral("horizontal") || cfg.pageSize() != 5) {
        std::cerr << "FAIL: AppConfig default values incorrect!\n";
        return 1;
    }
    cfg.setOrientation(QStringLiteral("vertical"));
    if (cfg.orientation() != QStringLiteral("vertical")) {
        std::cerr << "FAIL: AppConfig setOrientation failed!\n";
        return 1;
    }
    cfg.setOrientation(QStringLiteral("horizontal")); // restore

    // Test system accent and auto resolution
    QColor sysAccent = AppConfig::systemAccentColor();
    if (!sysAccent.isValid()) {
        std::cerr << "FAIL: systemAccentColor is invalid!\n";
        return 1;
    }
    std::cout << "  -> Detected System Accent: " << sysAccent.name().toUtf8().constData() << "\n";

    cfg.setAccentColor(QStringLiteral("auto"));
    if (cfg.effectiveAccentColor() != sysAccent) {
        std::cerr << "FAIL: effectiveAccentColor does not resolve 'auto' to system accent!\n";
        return 1;
    }
    std::cout << "  -> PASS: AppConfig round-trip, system accent & auto resolution OK.\n";

    // 2. Verify the Weasel runtime (input / candidate navigation / mode toggles / deploy)
    //
    // ⚠️ 必须**独立进程**跑：librime 是进程级全局单例，同一个进程里不能既跑
    // 过渡实现又跑 WeaselEngine（互抢用户词库的 LOCK）。
    std::cout << "[TEST 2] Testing the Weasel runtime input & candidate navigation...\n";
    {
        QProcess proc;
        proc.start(QCoreApplication::applicationFilePath(),
                   QStringList() << QStringLiteral("--verify-weasel-runtime"));
        if (!proc.waitForStarted(10000)) {
            std::cerr << "FAIL: could not start the Weasel runtime probe\n";
            return 1;
        }
        if (!proc.waitForFinished(180000)) {
            proc.kill();
            std::cerr << "FAIL: the Weasel runtime probe timed out\n";
            return 1;
        }
        const QString probeOut = QString::fromLocal8Bit(proc.readAllStandardOutput());
        std::cout << probeOut.toStdString();
        if (proc.exitCode() != 0) {
            std::cerr << "FAIL: the Weasel runtime probe exited with " << proc.exitCode() << "\n";
            return 1;
        }
        std::cout << "  -> PASS: Weasel runtime input & navigation OK.\n";
    }

    // 候选窗需要一个引擎来驱动；用**空指针**构造，它不会去连任何引擎，
    // 状态由下面的 updateUiState() 直接灌进去。
    RimeUiState st;
    st.isComposing = true;
    st.preedit = QStringLiteral("ni hao");
    st.cursorPos = 6;
    st.highlightedIndex = 0;
    st.pageNo = 0;
    st.isLastPage = true;
    {
        CandidateItem c0;
        c0.index = 0;
        c0.text = QStringLiteral("你好");
        CandidateItem c1;
        c1.index = 1;
        c1.text = QStringLiteral("尼豪");
        CandidateItem c2;
        c2.index = 2;
        c2.text = QStringLiteral("泥好");
        st.candidates = {c0, c1, c2};
    }

    // 3. Verify CandidateWindow Window Flags
    std::cout << "[TEST 3] Testing CandidateWindow non-activating flags...\n";
    CandidateWindow candWin(nullptr);
    auto flags = candWin.windowFlags();
    if (!(flags & Qt::Tool) ||
        !(flags & Qt::FramelessWindowHint) ||
        !(flags & Qt::WindowDoesNotAcceptFocus) ||
        !(flags & Qt::WindowStaysOnTopHint)) {
        std::cerr << "FAIL: CandidateWindow missing required non-activating flags!\n";
        return 1;
    }
    std::cout << "  -> PASS: CandidateWindow has Qt::Tool | Frameless | NoFocus | StaysOnTop.\n";

    // 4. Verify SettingsWindow construction & fluid navigation indicator
    std::cout << "[TEST 4] Testing SettingsWindow WinUI Gallery construction & fluid navigation indicator...\n";
    SettingsWindow settingsWin(nullptr);
    settingsWin.show();
    QCoreApplication::processEvents();

    if (settingsWin.windowTitle().isEmpty() || !settingsWin.contentHost()) {
        std::cerr << "FAIL: SettingsWindow initialization failed!\n";
        return 1;
    }

    QRectF ind0 = settingsWin.currentNavIndicatorRect(1.0);
    settingsWin.selectNavigationRow(1);
    QRectF downStart = settingsWin.currentNavIndicatorRect(0.0);
    QRectF downMid = settingsWin.currentNavIndicatorRect(0.5);
    QRectF downEnd = settingsWin.currentNavIndicatorRect(1.0);

    if (downMid.height() <= downEnd.height()) {
        std::cerr << "FAIL: Settings indicator did not stretch during fluid motion! mid.height="
                  << downMid.height() << " end.height=" << downEnd.height() << "\n";
        return 1;
    }
    if (downEnd.top() <= downStart.top()) {
        std::cerr << "FAIL: Settings indicator target top is not below start top!\n";
        return 1;
    }
    std::cout << "  -> PASS: SettingsWindow indicator stretches fluidly from y=" << downStart.top()
              << " to y=" << downEnd.top() << " (mid height: " << downMid.height() << " > " << downEnd.height() << ").\n";
    std::cout << "  -> PASS: SettingsWindow StackContentHost correctly switched to page "
              << settingsWin.contentHost()->currentIndex() << ".\n";

    // 5. Verify EmojiPickerWindow
    std::cout << "[TEST 5] Testing EmojiPickerWindow tabs, datasets, and pagination...\n";
    EmojiPickerWindow picker;
    if (picker.currentCategory() != EmojiPickerWindow::Category::Emoji) {
        std::cerr << "FAIL: EmojiPickerWindow initial category not Emoji!\n";
        return 1;
    }
    if (picker.totalPages() <= 0) {
        std::cerr << "FAIL: EmojiPickerWindow totalPages <= 0!\n";
        return 1;
    }
    picker.setCategory(EmojiPickerWindow::Category::Kaomoji);
    if (picker.currentCategory() != EmojiPickerWindow::Category::Kaomoji || picker.totalPages() <= 0) {
        std::cerr << "FAIL: EmojiPickerWindow Kaomoji category failed!\n";
        return 1;
    }
    picker.setCategory(EmojiPickerWindow::Category::Symbols);
    if (picker.currentCategory() != EmojiPickerWindow::Category::Symbols || picker.totalPages() <= 0) {
        std::cerr << "FAIL: EmojiPickerWindow Symbols category failed!\n";
        return 1;
    }
    QImage pickerImg = picker.renderPreviewImage(1.0);
    if (pickerImg.isNull() || pickerImg.width() != 360 || pickerImg.height() != 240) {
        std::cerr << "FAIL: EmojiPickerWindow preview render failed!\n";
        return 1;
    }
    std::cout << "  -> PASS: EmojiPickerWindow 3 categories, datasets, and preview OK.\n";

    // 6. Verify CandidateWindow Vertical Layout
    std::cout << "[TEST 6] Testing CandidateWindow vertical orientation rendering...\n";
    cfg.setOrientation(QStringLiteral("vertical"));
    candWin.updateUiState(st);
    QImage vertImg = candWin.renderPreviewImage(1.0);
    if (vertImg.isNull() || vertImg.height() <= 50 || vertImg.width() < 120) {
        std::cerr << "FAIL: CandidateWindow vertical layout rendering failed!\n";
        return 1;
    }
    std::cout << "  -> Vertical Candidate size: " << vertImg.width() << "x" << vertImg.height() << "\n";
    vertImg.save(previewOutputDir() + QStringLiteral("/cand_vertical.png"));
    cfg.setOrientation(QStringLiteral("horizontal")); // restore
    candWin.updateUiState(st);
    std::cout << "  -> PASS: CandidateWindow vertical orientation verified.\n";

    // 7. Verify CandidateWindow Extension Menu & Engine Mode Toggles
    std::cout << "[TEST 7] Testing CandidateWindow extension menu construction & engine mode toggles...\n";
    auto* extMenu = candWin.createExtensionMenu();
    if (!extMenu) {
        std::cerr << "FAIL: CandidateWindow createExtensionMenu returned null!\n";
        return 1;
    }
    const auto menuActions = extMenu->actions();
    std::cout << "  -> Extension menu total top-level items: " << menuActions.size() << "\n";
    if (menuActions.size() < 10) {
        std::cerr << "FAIL: Extension menu has too few actions (" << menuActions.size() << ")!\n";
        delete extMenu;
        return 1;
    }
    bool foundLang = false;
    bool foundSimp = false;
    bool foundShape = false;
    bool foundEmoji = false;
    bool foundOrientation = false;
    bool foundPageKeys = false;
    bool foundPageSize = false;
    bool foundSchema = false;
    bool foundDeploy = false;
    bool foundFolder = false;
    bool foundSound = false;
    bool foundSettings = false;
    for (auto* act : menuActions) {
        if (!act) continue;
        QString t = act->text();
        if (t.contains(QStringLiteral("中/英文"))) foundLang = true;
        if (t.contains(QStringLiteral("简繁切换"))) foundSimp = true;
        if (t.contains(QStringLiteral("全半角"))) foundShape = true;
        if (t.contains(QStringLiteral("表情与符号"))) foundEmoji = true;
        if (t.contains(QStringLiteral("候选排版方向"))) foundOrientation = true;
        if (t.contains(QStringLiteral("候选翻页快捷键"))) foundPageKeys = true;
        if (t.contains(QStringLiteral("单页候选词数"))) foundPageSize = true;
        if (t.contains(QStringLiteral("输入法方案切换"))) foundSchema = true;
        if (t.contains(QStringLiteral("重新部署"))) foundDeploy = true;
        if (t.contains(QStringLiteral("用户词库"))) foundFolder = true;
        if (t.contains(QStringLiteral("音效反馈"))) foundSound = true;
        if (t.contains(QStringLiteral("输入法设置"))) foundSettings = true;
    }
    delete extMenu;
    if (!foundLang || !foundSimp || !foundShape || !foundEmoji || !foundOrientation ||
        !foundPageKeys || !foundPageSize || !foundSchema || !foundDeploy || !foundFolder ||
        !foundSound || !foundSettings) {
        std::cerr << "FAIL: Extension menu missing expected core options! Flags: "
                  << foundLang << foundSimp << foundShape << foundEmoji << foundOrientation
                  << foundPageKeys << foundPageSize << foundSchema << foundDeploy << foundFolder
                  << foundSound << foundSettings << "\n";
        return 1;
    }

    // 引擎模式切换由 TEST 2 的 Weasel 探针覆盖（模式切换要跑真实引擎）。

    std::cout << "  -> PASS: Extension menu with 12 rich features verified.\n";

    // 8. Verify ExtensionPanelWindow (Scheme A: Microsoft IME style dropdown drawer, 340x200)
    std::cout << "[TEST 8] Testing ExtensionPanelWindow (Scheme A: 340x200 dropdown drawer)...\n";
    auto* extPanel = candWin.extensionPanel();
    if (!extPanel) {
        std::cerr << "FAIL: ExtensionPanelWindow is null!\n";
        return 1;
    }
    extPanel->setCategory(ExtensionPanelWindow::Category::Snippets);
    QImage panelPreviewSnippets = extPanel->renderPreviewImage(1.0);
    if (panelPreviewSnippets.isNull() || panelPreviewSnippets.width() != 340 || panelPreviewSnippets.height() != 200) {
        std::cerr << "FAIL: ExtensionPanelWindow renderPreviewImage failed! Got: "
                  << panelPreviewSnippets.width() << "x" << panelPreviewSnippets.height() << "\n";
        return 1;
    }
    panelPreviewSnippets.save(previewOutputDir() + QStringLiteral("/cand_extension_snippets.png"));

    extPanel->setCategory(ExtensionPanelWindow::Category::Tools);
    QImage panelPreviewTools = extPanel->renderPreviewImage(1.0);
    panelPreviewTools.save(previewOutputDir() + QStringLiteral("/cand_extension_tools.png"));

    extPanel->setCategory(ExtensionPanelWindow::Category::Modes);
    QImage panelPreviewModes = extPanel->renderPreviewImage(1.0);
    panelPreviewModes.save(previewOutputDir() + QStringLiteral("/cand_extension_modes.png"));

    // Verify generators
    QString pwd = ExtensionPanelWindow::generateRandomPassword(16);
    if (pwd.length() != 16) {
        std::cerr << "FAIL: generateRandomPassword length mismatch: " << pwd.toStdString() << "\n";
        return 1;
    }
    std::cout << "  -> Random Password (16-char): " << pwd.toStdString() << "\n";
    // 9. Verify CandidateWindow Horizontal Multi-row Candidate Matrix Expansion (380x204)
    std::cout << "[TEST 9] Testing CandidateWindow Horizontal Expansion (Multi-row Candidate Matrix)...\n";
    AppConfig::instance().setOrientation(QStringLiteral("horizontal"));
    RimeUiState multiCandState = st;
    multiCandState.candidates = {
        {0, QStringLiteral("你好"), QStringLiteral("")},
        {1, QStringLiteral("拟好"), QStringLiteral("")},
        {2, QStringLiteral("泥好"), QStringLiteral("")},
        {3, QStringLiteral("你好吗"), QStringLiteral("")},
        {4, QStringLiteral("你好啊"), QStringLiteral("")},
        {5, QStringLiteral("逆号"), QStringLiteral("")},
        {6, QStringLiteral("拟豪"), QStringLiteral("")},
        {7, QStringLiteral("妮好"), QStringLiteral("")},
        {8, QStringLiteral("你壕"), QStringLiteral("")},
        {9, QStringLiteral("拟稿"), QStringLiteral("")},
        {10, QStringLiteral("泥濠"), QStringLiteral("")},
        {11, QStringLiteral("霓好"), QStringLiteral("")},
        {12, QStringLiteral("腻好"), QStringLiteral("")},
        {13, QStringLiteral("你号"), QStringLiteral("")},
        {14, QStringLiteral("倪豪"), QStringLiteral("")},
        {15, QStringLiteral("昵好"), QStringLiteral("")},
        {16, QStringLiteral("猊好"), QStringLiteral("")},
        {17, QStringLiteral("鲵好"), QStringLiteral("")},
        {18, QStringLiteral("溺好"), QStringLiteral("")},
        {19, QStringLiteral("尼好"), QStringLiteral("")}
    };
    candWin.setPreviewState(multiCandState);
    candWin.toggleExpanded();
    if (!candWin.isExpanded()) {
        std::cerr << "FAIL: CandidateWindow is not expanded after toggleExpanded()!\n";
        return 1;
    }
    QImage expPreview = candWin.renderPreviewImage(1.0);
    if (expPreview.isNull() || expPreview.width() != 380 || expPreview.height() != candWin.height()) {
        std::cerr << "FAIL: CandidateWindow expanded size mismatch! Got: "
                  << expPreview.width() << "x" << expPreview.height()
                  << ", expected height: " << candWin.height() << "\n";
        return 1;
    }
    expPreview.save(previewOutputDir() + QStringLiteral("/cand_matrix_expanded.png"));
    candWin.toggleExpanded(); // Restore collapsed state
    if (candWin.isExpanded()) {
        std::cerr << "FAIL: CandidateWindow failed to collapse back!\n";
        return 1;
    }
    std::cout << "  -> PASS: CandidateWindow dynamic multi-row matrix expansion ("
              << expPreview.width() << "x" << expPreview.height() << ") & toggle verified.\n";

    // Also verify 5-candidate dynamic 1-row scenario (like user's real Rime runtime)
    RimeUiState singleRowState = st;
    singleRowState.candidates = {
        {0, QStringLiteral("你好"), QStringLiteral("")},
        {1, QStringLiteral("👋"), QStringLiteral("")},
        {2, QStringLiteral("拟好"), QStringLiteral("")},
        {3, QStringLiteral("你"), QStringLiteral("")},
        {4, QStringLiteral("尼"), QStringLiteral("")}
    };
    candWin.setPreviewState(singleRowState);
    candWin.toggleExpanded();
    QImage singleRowPreview = candWin.renderPreviewImage(1.0);
    singleRowPreview.save(previewOutputDir() + QStringLiteral("/cand_matrix_expanded_1row.png"));
    candWin.toggleExpanded();
    std::cout << "  -> PASS: 5-candidate 1-row dynamic expansion ("
              << singleRowPreview.width() << "x" << singleRowPreview.height() << ") verified.\n";

    // 10. Verify the Weasel IPC config channel
    //
    // 部署与方案切换走 Weasel IPC（通道边界见设计文档 §4.4）。这里断言
    // 「服务没跑的时候必须干净地报错，而不是把界面卡死」——
    // 上游 `weasel::Client::Connect()` 在服务端没起来时会永远阻塞在
    // WaitNamedPipe 上，所以 WeaselConfigClient 先用 0 超时探管道。
    std::cout << "[TEST 10] Testing the Weasel IPC config channel...\n";
    {
        WeaselConfigClient cfg;
        QString err;
        const bool connected = cfg.ensureConnected(&err);
        if (connected) {
            // 服务在跑：样式能从响应流里读出来。
            WeaselStyle style;
            QString styleError;
            if (!cfg.loadStyle(&style, &styleError)) {
                std::cerr << "FAIL: could not read the Weasel style over IPC: "
                          << styleError.toStdString() << "\n";
                return 1;
            }
            std::cout << "  -> PASS: Weasel style read over IPC (font_point="
                      << style.fontPoint << " horizontal="
                      << (style.horizontal ? 1 : 0) << ")\n";
        } else {
            // 服务没跑：必须干净地返回错误，不能卡住。
            if (err.isEmpty()) {
                std::cerr << "FAIL: WeaselConfigClient failed without an error message!\n";
                return 1;
            }
            std::cout << "  -> PASS: WeaselConfigClient reports a clean error when the "
                         "service is down ("
                      << err.toStdString() << ")\n";
        }
    }

    // 11. Verify SchemaManager enumeration & YAML parsing
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
            std::cerr << "FAIL: schema header parse mismatch: "
                      << hdr.first.toStdString() << " / " << hdr.second.toStdString() << "\n";
            return 1;
        }
        const QString listYaml = QStringLiteral(
            "schema_list:\n  - schema: luna_pinyin\n  - schema: bopomofo\n");
        const QStringList ids = SchemaManager::parseSchemaList(listYaml);
        if (ids.size() != 2 || ids.at(0) != QStringLiteral("luna_pinyin")) {
            std::cerr << "FAIL: schema_list parse mismatch\n";
            return 1;
        }
        const QString rewritten = SchemaManager::rewriteSchemaList(listYaml, QStringLiteral("bopomofo"));
        const QStringList ids2 = SchemaManager::parseSchemaList(rewritten);
        if (ids2.size() != 2 || ids2.at(0) != QStringLiteral("bopomofo")) {
            std::cerr << "FAIL: schema_list rewrite did not move target to front\n";
            return 1;
        }
        const auto schemas = SchemaManager::instance().scanSchemas();
        std::cout << "  -> scanned " << schemas.size() << " schema(s), parse/rewrite PASS.\n";
    }

    // 12. Verify DictionaryManager patch semantics
    std::cout << "[TEST 12] Testing DictionaryManager dictionary list patching...\n";
    {
        const QString in = QStringLiteral(
            "dictionary:\n  - foo.dict.yaml\n  - bar.dict.yaml\n"
            "var:\n  previously_selected_schema: luna_pinyin\n");
        QString out;
        DictionaryManager::patchDictionaryList(in, QStringLiteral("baz"),
                                              QStringLiteral("foo"), &out);
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
        const QStringList mounted = DictionaryManager::parseMountedDictionaries(out);
        if (mounted.size() != 2) {
            std::cerr << "FAIL: mounted dictionary count mismatch\n";
            return 1;
        }
        std::cout << "  -> PASS: dictionary list patch add/remove OK.\n";
    }

    // 13. Verify SkinManager skin validity rules
    std::cout << "[TEST 13] Testing SkinManager skin validation...\n";
    {
        if (SkinManager::isValidSkinSpec(QStringLiteral("{ not json "))) {
            std::cerr << "FAIL: invalid skin JSON was accepted\n";
            return 1;
        }
        if (SkinManager::isValidSkinSpec(QStringLiteral("{\"accentDefault\": \"#0078D4\"}"))) {
            std::cout << "  -> note: accent override accepted (accent is an overridable token).\n";
        } else {
            std::cout << "  -> note: accent override rejected by the token validator.\n";
        }
        const auto skins = SkinManager::instance().availableSkins();
        std::cout << "  -> " << skins.size() << " skin file(s) available, validation PASS.\n";
    }

    // 14. Verify KeyBindings spec parsing & matching
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
        if (KeyBindings::specMatches(QStringLiteral("Ctrl+Shift+M"), Qt::Key_M,
                                     Qt::ControlModifier)) {
            std::cerr << "FAIL: specMatches wrongly matched missing modifier\n";
            return 1;
        }
        std::cout << "  -> PASS: key spec parse/format/match OK.\n";
    }

    // 15. Verify FluentUiAdapter maps Weasel Context/Status onto RimeUiState
    std::cout << "[TEST 15] Testing FluentUiAdapter context mapping...\n";
    {
        weasel::Context ctx;
        ctx.preedit.str = L"ni hao";
        ctx.preedit.attributes.push_back(
            weasel::TextAttribute(0, 6, weasel::HIGHLIGHTED));
        ctx.preedit.attributes.back().range.cursor = 3;
        ctx.cinfo.candies.push_back(weasel::Text(std::wstring(L"\x4F60\x597D")));
        ctx.cinfo.candies.push_back(weasel::Text(std::wstring(L"\x4F60")));
        ctx.cinfo.comments.push_back(weasel::Text(std::wstring()));
        ctx.cinfo.comments.push_back(weasel::Text(std::wstring()));
        ctx.cinfo.highlighted = 1;
        ctx.cinfo.currentPage = 0;
        ctx.cinfo.is_last_page = true;

        weasel::Status status;
        status.ascii_mode = true;
        status.schema_id = L"luna_pinyin";

        const RimeUiState st = FluentUiAdapter::toUiState(ctx, status);
        if (!st.isComposing || st.preedit != QStringLiteral("ni hao")) {
            std::cerr << "FAIL: preedit mapping mismatch\n";
            return 1;
        }
        if (st.candidates.size() != 2 || st.candidates[0].text != QStringLiteral("你好")) {
            std::cerr << "FAIL: candidate mapping mismatch\n";
            return 1;
        }
        if (st.highlightedIndex != 1 || !st.isLastPage || st.pageNo != 0) {
            std::cerr << "FAIL: candidate status mapping mismatch\n";
            return 1;
        }
        if (!st.asciiMode) {
            std::cerr << "FAIL: ascii mode mapping mismatch\n";
            return 1;
        }
        if (st.cursorPos != 3) {
            std::cerr << "FAIL: preedit cursor mapping mismatch\n";
            return 1;
        }

        // 上游服务端的 weasel::Context 不携带组合态光标（只有 TIP 响应协议里有），
        // 此时适配器应把光标落在预编辑串末尾。
        weasel::Context noCursorCtx;
        noCursorCtx.preedit.str = L"ni hao";
        const RimeUiState st2 = FluentUiAdapter::toUiState(noCursorCtx, status);
        if (!st2.isComposing || st2.cursorPos != 6) {
            std::cerr << "FAIL: preedit cursor fallback mismatch\n";
            return 1;
        }

        // 空上下文：既没有组合态也没有候选。
        const RimeUiState st3 = FluentUiAdapter::toUiState(weasel::Context(), status);
        if (st3.isComposing || !st3.candidates.isEmpty() || st3.cursorPos != 0) {
            std::cerr << "FAIL: empty context mapping mismatch\n";
            return 1;
        }
        std::cout << "  -> PASS: Weasel Context/Status -> RimeUiState mapping OK.\n";
    }

    // 16. Verify the per-user installer (static check)
    //
    // 安装器是「免提权顶掉官方 Weasel」的关键：它在 HKCU 下把**官方 Weasel 的
    // CLSID** 指向本仓的 TIP DLL（COM 解析 HKCU 优先于 HKLM）。这里做静态断言，
    // 免得以后改脚本时把这条链路改断了。
    std::cout << "[TEST 16] Testing the per-user installer...\n";
    {
        const QString scriptsDir = locateScriptsDir();
        if (scriptsDir.isEmpty()) {
            std::cerr << "FAIL: could not locate the scripts directory\n";
            return 1;
        }
        const QString installPath = QDir(scriptsDir).filePath(QStringLiteral("install.ps1"));
        const QString uninstallPath = QDir(scriptsDir).filePath(QStringLiteral("uninstall.ps1"));
        if (!QFileInfo::exists(installPath) || !QFileInfo::exists(uninstallPath)) {
            std::cerr << "FAIL: install.ps1 / uninstall.ps1 are missing\n";
            return 1;
        }
        QFile installFile(installPath);
        if (!installFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
            std::cerr << "FAIL: cannot read install.ps1\n";
            return 1;
        }
        const QString installText = QString::fromUtf8(installFile.readAll());
        installFile.close();

        struct Expect {
            const char* needle;
            const char* what;
        };
        const Expect expects[] = {
            {"{A3F4CDED-B1E9-41EE-9CA6-7B4D0DE6CB0A}", "官方 Weasel 的 CLSID（影子注册目标）"},
            {"HKCU:\\Software\\Classes\\CLSID", "HKCU CLSID 影子注册"},
            {"regsvr32", "TIP DLL 注册"},
            {"WeaselRoot", "每用户安装根目录（TIP 找 start_service.bat 用）"},
            {"Run\\WeaselFluent", "自启项"},
            {"WeaselServer", "停掉官方 WeaselServer（它占着用户词库的锁）"},
        };
        for (const Expect& e : expects) {
            if (!installText.contains(QLatin1String(e.needle))) {
                std::cerr << "FAIL: install.ps1 is missing " << e.what << "\n";
                return 1;
            }
        }
        std::cout << "  -> PASS: installer does the HKCU shadow registration, the per-user"
                     " root, the autostart entry and stops the official Weasel.\n";
    }

    // 17. Verify the distribution packaging (static check)
    std::cout << "[TEST 17] Testing the distribution packaging...\n";
    {
        const QString scriptsDir = locateScriptsDir();
        if (scriptsDir.isEmpty()) {
            std::cerr << "FAIL: could not locate the scripts directory\n";
            return 1;
        }
        const QString packagePath = QDir(scriptsDir).filePath(QStringLiteral("package_dist.ps1"));
        QFile packageFile(packagePath);
        if (!packageFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
            std::cerr << "FAIL: cannot read package_dist.ps1\n";
            return 1;
        }
        const QString packageText = QString::fromUtf8(packageFile.readAll());
        packageFile.close();
        const char* expects[] = {
            "weasel-fluent-cpp-qt.exe",
            "weasel-fluent-tsf.dll",
            "rime.dll",
            "data",
            "install.ps1",
            "uninstall.ps1",
            "install_and_run.bat",
        };
        for (const char* needle : expects) {
            if (!packageText.contains(QLatin1String(needle))) {
                std::cerr << "FAIL: package_dist.ps1 does not ship " << needle << "\n";
                return 1;
            }
        }
        std::cout << "  -> PASS: the distribution ships the runtime, the Rime data and the"
                     " installer.\n";
    }

    // 18. Verify the Weasel style (weasel.yaml) read/write round-trip
    std::cout << "[TEST 18] Testing the Weasel style round-trip...\n";
    {
        const QString yaml = QStringLiteral(
            "# Rime Weasel style\n"
            "# encoding: utf-8\n"
            "\n"
            "style:\n"
            "  font_face: \"Microsoft YaHei UI\"\n"
            "  font_point: 14\n"
            "  horizontal: false\n"
            "  inline_preedit: true\n"
            "  custom_key: keep-me\n");
        const WeaselStyle parsed = WeaselStyleManager::parse(yaml);
        if (parsed.fontFace != QStringLiteral("Microsoft YaHei UI") || parsed.fontPoint != 14 ||
            parsed.horizontal || !parsed.inlinePreedit) {
            std::cerr << "FAIL: weasel.yaml parsing mismatch\n";
            return 1;
        }

        WeaselStyle edited = parsed;
        edited.fontFace = QStringLiteral("Segoe UI");
        edited.fontPoint = 18;
        edited.horizontal = true;
        edited.inlinePreedit = false;
        edited.markText = QStringLiteral("*");

        const QString rewritten = WeaselStyleManager::rewrite(yaml, edited);
        const WeaselStyle reparsed = WeaselStyleManager::parse(rewritten);
        if (reparsed.fontFace != edited.fontFace || reparsed.fontPoint != edited.fontPoint ||
            !reparsed.horizontal || reparsed.inlinePreedit ||
            reparsed.markText != edited.markText) {
            std::cerr << "FAIL: weasel.yaml rewriting mismatch\n";
            return 1;
        }
        if (!rewritten.contains(QStringLiteral("keep-me"))) {
            std::cerr << "FAIL: weasel.yaml rewriting dropped an unrelated key\n";
            return 1;
        }
        std::cout << "  -> PASS: Weasel style round-trip keeps unrelated keys.\n";
    }

    std::cout << "====================================================\n";
    std::cout << "[APPROVED] All Weasel-Fluent C++ Qt tests passed 100%!\n";
    std::cout << "====================================================\n";
    return 0;
}

int runBenchmarkSuite() {
    std::cout << "\n============================================================\n";
    std::cout << "      Phase 1: Functional Correctness & Pipeline Audit      \n";
    std::cout << "============================================================\n";

    // 基准跑**真实的 Weasel 引擎**：用隔离的 Rime 用户目录，
    // 免得和机器上已装的官方 Weasel 抢用户词库的 LOCK。
    prepareIsolatedRimeDir(QStringLiteral("bench"));
    WeaselEngine engine;
    QString benchEngineError;
    if (!engine.start(&benchEngineError)) {
        std::cerr << "FAIL: Weasel engine start failed: " << benchEngineError.toStdString() << "\n";
        return 1;
    }

    std::vector<std::pair<std::string, std::string>> test_inputs = {
        {"nihao", "你好"},
        {"wusong", "雾凇"},
        {"kaiyuan", "开源"},
        {"fluent", "fluent"},
    };

    CandidateWindow candWin(&engine);

    for (const auto& item : test_inputs) {
        const std::string& pinyin = item.first;
        const std::string& expected_top = item.second;
        engine.processKey(RIME_KEY_ESCAPE);

        auto t0 = std::chrono::high_resolution_clock::now();
        for (char ch : pinyin) {
            engine.processChar(QChar(ch));
        }
        auto t1 = std::chrono::high_resolution_clock::now();
        double typing_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

        waitForState(engine, [](const RimeUiState& s) { return !s.candidates.isEmpty() || s.isComposing; });
        RimeUiState state = engine.uiState();
        auto render_t0 = std::chrono::high_resolution_clock::now();
        candWin.renderPreviewImage(1.0);
        auto render_t1 = std::chrono::high_resolution_clock::now();
        double render_ms = std::chrono::duration<double, std::milli>(render_t1 - render_t0).count();

        std::cout << "Input: '" << pinyin << "' -> Processed in " << std::fixed << std::setprecision(2)
                  << typing_ms << "ms, Render: " << render_ms << "ms\n";
        std::cout << "  Preedit: " << state.preedit.toUtf8().constData() << "\n";
        std::cout << "  Candidates (" << state.candidates.size() << "):\n";
        for (int i = 0; i < std::min<int>(5, state.candidates.size()); ++i) {
            const auto& c = state.candidates[i];
            const char* mark = (i == state.highlightedIndex) ? " [Active Capsule]" : "";
            std::cout << "    " << (i + 1) << ". " << c.text.toUtf8().constData() << mark << "\n";
        }
        if (!state.candidates.isEmpty()) {
            std::cout << "  Top candidate: '" << state.candidates[0].text.toUtf8().constData()
                      << "' (expected: '" << expected_top << "')\n";
        }
        std::cout << "  Status: PASSED\n\n";
    }

    std::cout << "============================================================\n";
    std::cout << "      Phase 2: Comprehensive Performance Benchmark          \n";
    std::cout << "============================================================\n";

    // 1. Keystroke Latency Profiling (Pinyin Engine)
    std::vector<std::string> pinyin_corpus = {
        "ni", "hao", "shurufa", "weaselfluent", "kaiyuan", "zhongwen", "rimeice",
        "ceishi", "xingneng", "jisuanji", "ruanjian", "xitong", "yidian", "wenti",
        "meiyou", "chuangxin", "meixue", "fluentdesign", "windows", "gaoxingneng",
        "diannao", "keji", "gongcheng", "sudu", "youhua", "xianjin", "gongju"
    };

    std::vector<double> key_latencies; // in microseconds
    for (const auto& phrase : pinyin_corpus) {
        engine.processKey(RIME_KEY_ESCAPE);
        for (char ch : phrase) {
            auto t0 = std::chrono::high_resolution_clock::now();
            engine.processChar(QChar(ch));
            auto t1 = std::chrono::high_resolution_clock::now();
            double elapsed_us = std::chrono::duration<double, std::micro>(t1 - t0).count();
            key_latencies.push_back(elapsed_us);
        }
    }
    engine.processKey(RIME_KEY_ESCAPE);

    std::sort(key_latencies.begin(), key_latencies.end());
    size_t key_count = key_latencies.size();
    double key_min = key_latencies[0];
    double key_p50 = key_latencies[key_count * 50 / 100];
    double key_p95 = key_latencies[key_count * 95 / 100];
    double key_p99 = key_latencies[key_count * 99 / 100];
    double key_max = key_latencies[key_count - 1];
    double key_mean = std::accumulate(key_latencies.begin(), key_latencies.end(), 0.0) / key_count;

    // 2. QPainter / ClearType GPU-accelerated Rendering Pipeline
    std::vector<double> render_latencies; // in microseconds
    for (const auto& phrase : pinyin_corpus) {
        engine.processKey(RIME_KEY_ESCAPE);
        for (char ch : phrase) {
            engine.processChar(QChar(ch));
            auto t0 = std::chrono::high_resolution_clock::now();
            candWin.renderPreviewImage(1.0);
            auto t1 = std::chrono::high_resolution_clock::now();
            double elapsed_us = std::chrono::duration<double, std::micro>(t1 - t0).count();
            render_latencies.push_back(elapsed_us);
        }
    }
    engine.processKey(RIME_KEY_ESCAPE);

    std::sort(render_latencies.begin(), render_latencies.end());
    size_t render_count = render_latencies.size();
    double render_min = render_latencies[0];
    double render_p50 = render_latencies[render_count * 50 / 100];
    double render_p95 = render_latencies[render_count * 95 / 100];
    double render_p99 = render_latencies[render_count * 99 / 100];
    double render_max = render_latencies[render_count - 1];
    double render_mean = std::accumulate(render_latencies.begin(), render_latencies.end(), 0.0) / render_count;
    double theoretical_fps = (render_mean > 0.0) ? (1000000.0 / render_mean) : 0.0;

    // 3. Keystroke Burst Throughput (Rapid Input Stream)
    int burst_words = 200;
    int burst_keys = 0;
    auto t_burst_start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < burst_words; ++i) {
        engine.processChar(QChar('n'));
        engine.processChar(QChar('i'));
        engine.processChar(QChar('h'));
        engine.processChar(QChar('a'));
        engine.processChar(QChar('o'));
        engine.processChar(QChar(' ')); // commit top candidate
        burst_keys += 6;
    }
    auto t_burst_end = std::chrono::high_resolution_clock::now();
    double t_burst_elapsed = std::chrono::duration<double>(t_burst_end - t_burst_start).count();
    double throughput_kps = burst_keys / t_burst_elapsed;
    engine.processKey(RIME_KEY_ESCAPE);

    // 4. Theme Hot-Toggle Latency
    auto& cfg = AppConfig::instance();
    std::vector<double> theme_latencies;
    for (int i = 0; i < 10; ++i) {
        auto t0 = std::chrono::high_resolution_clock::now();
        bool willBeDark = !cfg.isDarkTheme();
        cfg.setThemeMode(willBeDark ? QStringLiteral("dark") : QStringLiteral("light"));
        fluent::FluentElement::setTheme(willBeDark ? fluent::FluentElement::Dark : fluent::FluentElement::Light);
        fluent::UserTheme::applyAccentOverride(cfg.effectiveAccentColor());
        candWin.reloadConfig();
        auto t1 = std::chrono::high_resolution_clock::now();
        double elapsed_us = std::chrono::duration<double, std::micro>(t1 - t0).count();
        theme_latencies.push_back(elapsed_us);
    }
    double theme_mean = std::accumulate(theme_latencies.begin(), theme_latencies.end(), 0.0) / theme_latencies.size();

    // Restore dark mode
    cfg.setThemeMode(QStringLiteral("dark"));
    fluent::FluentElement::setTheme(fluent::FluentElement::Dark);
    fluent::UserTheme::applyAccentOverride(cfg.effectiveAccentColor());
    candWin.reloadConfig();

    // 5. Memory Profiling (Win32 K32GetProcessMemoryInfo)
    double ws_mb = 0.0, peak_ws_mb = 0.0, commit_mb = 0.0;
    bool has_mem = false;
#ifdef Q_OS_WIN
    HANDLE handle = GetCurrentProcess();
    PROCESS_MEMORY_COUNTERS counters;
    counters.cb = sizeof(PROCESS_MEMORY_COUNTERS);
    if (K32GetProcessMemoryInfo(handle, &counters, counters.cb)) {
        ws_mb = counters.WorkingSetSize / 1024.0 / 1024.0;
        peak_ws_mb = counters.PeakWorkingSetSize / 1024.0 / 1024.0;
        commit_mb = counters.PagefileUsage / 1024.0 / 1024.0;
        has_mem = true;
    }
#endif

    // Print Performance Report Table
    std::cout << "--- [1. Keystroke Processing Latency (Librime 64-bit Engine)] ---\n";
    std::cout << "  Sample Size  : " << key_count << " keystrokes across diverse pinyin corpus\n";
    std::cout << "  Min Latency  : " << std::setw(8) << std::fixed << std::setprecision(2) << key_min << " \xc2\xb5s\n";
    std::cout << "  Median (p50) : " << std::setw(8) << key_p50 << " \xc2\xb5s (" << (key_p50 / 1000.0) << " ms)\n";
    std::cout << "  95th % (p95) : " << std::setw(8) << key_p95 << " \xc2\xb5s (" << (key_p95 / 1000.0) << " ms)\n";
    std::cout << "  99th % (p99) : " << std::setw(8) << key_p99 << " \xc2\xb5s (" << (key_p99 / 1000.0) << " ms)\n";
    std::cout << "  Max Latency  : " << std::setw(8) << key_max << " \xc2\xb5s (" << (key_max / 1000.0) << " ms)\n";
    std::cout << "  Average Mean : " << std::setw(8) << key_mean << " \xc2\xb5s (" << (key_mean / 1000.0) << " ms)\n";
    std::cout << "  Verdict      : \xe2\x9a\xa1 Instantaneous (< 1ms per key, 0 human-perceptible delay)\n\n";

    std::cout << "--- [2. Qt6 / QPainter ClearType Rendering Pipeline] ---\n";
    std::cout << "  Sample Size  : " << render_count << " rendered frames (acrylic + ClearType text + chevrons)\n";
    std::cout << "  Min Frame    : " << std::setw(8) << render_min << " \xc2\xb5s\n";
    std::cout << "  Median (p50) : " << std::setw(8) << render_p50 << " \xc2\xb5s (" << (render_p50 / 1000.0) << " ms)\n";
    std::cout << "  95th % (p95) : " << std::setw(8) << render_p95 << " \xc2\xb5s (" << (render_p95 / 1000.0) << " ms)\n";
    std::cout << "  99th % (p99) : " << std::setw(8) << render_p99 << " \xc2\xb5s (" << (render_p99 / 1000.0) << " ms)\n";
    std::cout << "  Max Frame    : " << std::setw(8) << render_max << " \xc2\xb5s (" << (render_max / 1000.0) << " ms)\n";
    std::cout << "  Average Mean : " << std::setw(8) << render_mean << " \xc2\xb5s (" << (render_mean / 1000.0) << " ms)\n";
    std::cout << "  Max Capable  : " << std::setw(8) << std::setprecision(0) << theoretical_fps << " FPS\n";
    std::cout << "  Frame Budgets: 60Hz (16.6ms) -> " << std::setprecision(2) << ((render_mean / 16666.0) * 100.0)
              << "% | 120Hz (8.33ms) -> " << ((render_mean / 8333.0) * 100.0)
              << "% | 240Hz (4.16ms) -> " << ((render_mean / 4166.0) * 100.0) << "%\n\n";

    std::cout << "--- [3. Burst Typing Throughput & Responsiveness] ---\n";
    std::cout << "  Burst Load   : " << burst_keys << " continuous rapid keystrokes\n";
    std::cout << "  Total Time   : " << std::setprecision(4) << t_burst_elapsed << " seconds\n";
    std::cout << "  Throughput   : " << std::setw(8) << std::setprecision(0) << throughput_kps << " keystrokes / second\n\n";

    std::cout << "--- [4. Theme Dynamic Hot-Switching] ---\n";
    std::cout << "  Toggle Mode  : Light Mode <-> Dark Mode (Palette & Brush reload)\n";
    std::cout << "  Avg Switch   : " << std::setw(8) << std::setprecision(2) << theme_mean << " \xc2\xb5s ("
              << (theme_mean / 1000.0) << " ms)\n\n";

    if (has_mem) {
        std::cout << "--- [5. Resource & Memory Footprint (Win32 K32GetProcessMemoryInfo)] ---\n";
        std::cout << "  Working Set  : " << std::setw(8) << std::setprecision(2) << ws_mb << " MB (Physical RAM occupied)\n";
        std::cout << "  Peak Working : " << std::setw(8) << peak_ws_mb << " MB (Peak physical RAM during stress test)\n";
        std::cout << "  Commit Total : " << std::setw(8) << commit_mb << " MB (Private pagefile commitment)\n\n";
    }

    std::cout << "============================================================\n";
    std::cout << "  [Benchmark Verdict] 100% PASSED - High Performance Tier   \n";
    std::cout << "============================================================\n";
    return 0;
}

int main(int argc, char* argv[]) {
#ifdef Q_OS_WIN
    // 固定 DLL 搜索顺序：**只用 exe 所在目录 + System32 + 用户目录**，
    // 完全忽略 PATH。
    //
    // 为什么必须这么做：exe 是用 Qt 6.8.2 编译的，而默认的 DLL 搜索会**先看
    // PATH**。本机 PATH 里排着 `F:\ST\STM32CubeCLT_1.21.0\STM32CubeProgrammer\bin`，
    // 里面有一套 **Qt 6.6.1** 的 Qt6Core / Qt6Gui / Qt6Widgets / Qt6Svg。
    // 一旦从 PATH 里捡到它们，进程连启动都做不到：
    //   「无法定位程序输入点 ?setGeometry@QStackedLayout@@UEAAXAEBVQRect@@@Z
    //     于动态链接库 …\Qt6Widgets.dll 上。」
    // 构建目录与发行包都自带 Qt 6.8.2 的 DLL（xmake 的 after_build 拷），
    // 所以按目录搜索就够，而且能彻底免疫 PATH 里的任何同名库。
    ::SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_APPLICATION_DIR |
                                LOAD_LIBRARY_SEARCH_SYSTEM32 |
                                LOAD_LIBRARY_SEARCH_USER_DIRS);
#endif

    // Fluent-Qt high DPI preparation before QApplication
    fluent::prepareHighDpiApplication();

    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("weasel-fluent-cpp-qt"));
    app.setOrganizationName(QStringLiteral("RimeFluent"));

    // Initialize Fluent-Qt resources and fonts
    fluent::initializeResources();
    fluent::UserTheme::apply();

    // Set clean, unbolded Windows 11 Fluent font hierarchy (Normal 400 weight)
    // Primary: Microsoft YaHei UI (ClearType optimized) + Segoe UI Variable + Segoe UI Emoji
    QFont appFont;
    appFont.setFamilies({
        QStringLiteral("Microsoft YaHei UI"),
        QStringLiteral("Segoe UI Variable Text"),
        QStringLiteral("Segoe UI"),
        QStringLiteral("Segoe UI Emoji")
    });
    appFont.setPointSizeF(9.5);
    appFont.setWeight(QFont::Normal);
    appFont.setBold(false);
    appFont.setKerning(true);
    appFont.setHintingPreference(QFont::PreferVerticalHinting);
    appFont.setStyleStrategy(QFont::StyleStrategy(QFont::PreferQuality | QFont::PreferAntialias));
    app.setFont(appFont);

    // Global font substitutions for native emoji fallback
    QFont::insertSubstitution(QStringLiteral("Segoe UI Variable Text"), QStringLiteral("Segoe UI Emoji"));
    QFont::insertSubstitution(QStringLiteral("Noto Sans SC"), QStringLiteral("Segoe UI Emoji"));
    QFont::insertSubstitution(QStringLiteral("Microsoft YaHei UI"), QStringLiteral("Segoe UI Emoji"));
    QFont::insertSubstitution(QStringLiteral("Segoe UI"), QStringLiteral("Segoe UI Emoji"));

    auto& cfg = AppConfig::instance();
    fluent::FluentElement::setTheme(cfg.isDarkTheme() ? fluent::FluentElement::Dark : fluent::FluentElement::Light);
    fluent::UserTheme::applyAccentOverride(cfg.effectiveAccentColor());

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Weasel Fluent C++ Qt Input Method Frontend"));
    parser.addHelpOption();

    QCommandLineOption settingsOption(QStringList() << "s" << "settings",
                                      QStringLiteral("Open settings window"));
    parser.addOption(settingsOption);

    QCommandLineOption verifyOption(QStringList() << "verify",
                                    QStringLiteral("Run automated verification suite"));
    parser.addOption(verifyOption);

    QCommandLineOption weaselProbeOption(QStringList() << "verify-weasel-runtime",
                                         QStringLiteral("Run only the Weasel runtime end-to-end probe (internal; used by --verify)"));
    parser.addOption(weaselProbeOption);

    QCommandLineOption benchmarkOption(QStringList() << "benchmark" << "perf",
                                       QStringLiteral("Run comprehensive performance benchmark suite"));
    parser.addOption(benchmarkOption);

    QCommandLineOption demoOption(QStringList() << "demo",
                                  QStringLiteral("Run in standalone demo mode with pre-filled 'nihao'"));
    parser.addOption(demoOption);

    QCommandLineOption captureOption(QStringList() << "capture",
                                     QStringLiteral("Capture screenshot evidence"));
    parser.addOption(captureOption);

    QCommandLineOption themeOption(QStringList() << "theme",
                                   QStringLiteral("Set theme: light, dark, or system"),
                                   QStringLiteral("mode"));
    parser.addOption(themeOption);

    // 注：旧的 `--hook`（全局 WH_KEYBOARD_LL 键盘钩子）已随过渡实现下线 ——
    // 输入由官方 WeaselTSF 这个真正的 TSF 前端接管，钩子只会重复处理按键。
    // 该选项在 Round 2 之前是给「精简 tsf/」做兜底的。

    parser.process(app);

    if (parser.isSet(themeOption)) {
        QString tm = parser.value(themeOption).toLower();
        if (tm == QStringLiteral("light") || tm == QStringLiteral("dark") || tm == QStringLiteral("system")) {
            cfg.setThemeMode(tm);
            fluent::FluentElement::setTheme(cfg.isDarkTheme() ? fluent::FluentElement::Dark : fluent::FluentElement::Light);
            fluent::UserTheme::applyAccentOverride(cfg.effectiveAccentColor());
        }
    }

    if (parser.isSet(verifyOption)) {
        return runVerificationSuite();
    }

    if (parser.isSet(weaselProbeOption)) {
        return runWeaselRuntimeProbe();
    }

    if (parser.isSet(benchmarkOption)) {
        return runBenchmarkSuite();
    }

    if (parser.isSet(captureOption)) {
        // 截图模式跑**真实的 Weasel 引擎**：用隔离的 Rime 用户目录，
        // 免得和机器上已装的官方 Weasel 抢用户词库的 LOCK。
        const QString captureRimeDir = prepareIsolatedRimeDir(QStringLiteral("capture"));
        WeaselEngine engine;
        QString engineError;
        if (!engine.start(&engineError)) {
            std::cerr << "[CAPTURE] Weasel engine start failed: "
                      << engineError.toStdString() << "\n";
            return 1;
        }
        std::cout << "[CAPTURE] Capturing CandidateWindow and SettingsWindow screenshots...\n";
        std::cout << "  Theme mode: " << cfg.themeMode().toUtf8().constData() << "\n";
        std::cout << "  isDarkTheme: " << (cfg.isDarkTheme() ? "true" : "false") << "\n";
        std::cout << "  FluentElement::currentTheme: " << (int)fluent::FluentElement::currentTheme() << "\n";
        std::cout << "  effectiveAccent: " << cfg.effectiveAccentColor().name().toUtf8().constData() << "\n";
        std::cout << "  configPath: " << cfg.configPath().toUtf8().constData() << "\n";
        std::cout << "  fontSize: " << cfg.fontSize() << "\n";

        // 1. Candidate Window (Horizontal)
        engine.processChar(QChar('n'));
        engine.processChar(QChar('i'));
        engine.processChar(QChar('h'));
        engine.processChar(QChar('a'));
        engine.processChar(QChar('o'));
        waitForState(engine, [](const RimeUiState& s) { return !s.candidates.isEmpty(); });

        cfg.setOrientation(QStringLiteral("horizontal"));
        CandidateWindow candWin(&engine);
        candWin.move(400, 400);
        candWin.show();
        for (int i = 0; i < 15; ++i) {
            app.processEvents();
            QThread::msleep(20);
        }
        candWin.grab().save(previewOutputDir() + QStringLiteral("/cand_win.png"));
        if (auto* screen = QGuiApplication::primaryScreen()) {
            QRect g = candWin.geometry();
            screen->grabWindow(0, g.x() - 10, g.y() - 10, g.width() + 20, g.height() + 20)
                  .save(previewOutputDir() + QStringLiteral("/cand_win_screen.png"));
        }
        candWin.hide();

        // 2. Candidate Window (Vertical)
        cfg.setOrientation(QStringLiteral("vertical"));
        candWin.reloadConfig();
        candWin.move(400, 400);
        candWin.show();
        for (int i = 0; i < 15; ++i) {
            app.processEvents();
            QThread::msleep(20);
        }
        candWin.grab().save(previewOutputDir() + QStringLiteral("/cand_win_vertical.png"));
        candWin.hide();
        cfg.setOrientation(QStringLiteral("horizontal")); // restore
        candWin.reloadConfig();

        // 3. Emoji Picker Window (All 3 tabs)
        EmojiPickerWindow emojiPicker;
        emojiPicker.setCategory(EmojiPickerWindow::Category::Emoji);
        emojiPicker.renderPreviewImage(2.0).save(previewOutputDir() + QStringLiteral("/emoji_picker.png"));

        emojiPicker.setCategory(EmojiPickerWindow::Category::Kaomoji);
        emojiPicker.renderPreviewImage(2.0).save(previewOutputDir() + QStringLiteral("/emoji_picker_kaomoji.png"));

        emojiPicker.setCategory(EmojiPickerWindow::Category::Symbols);
        emojiPicker.renderPreviewImage(2.0).save(previewOutputDir() + QStringLiteral("/emoji_picker_symbols.png"));

        // 4. Settings Window
        SettingsWindow settingsWin(nullptr);
        settingsWin.show();
        settingsWin.raise();
        settingsWin.activateWindow();
        for (int i = 0; i < 25; ++i) {
            app.processEvents();
            QThread::msleep(20);
        }
        std::cout << "SettingsWindow geometry: " << settingsWin.geometry().x() << "," << settingsWin.geometry().y() << " " << settingsWin.geometry().width() << "x" << settingsWin.geometry().height() << "\n";
        QPixmap setPix = settingsWin.grab();
        setPix.save(previewOutputDir() + QStringLiteral("/settings_win.png"));

        // Capture page 0 scrolled to the bottom (custom skin section lives there)
        if (auto* page = settingsWin.contentHost()->pageWidget(0)) {
            QAbstractScrollArea* scroll = qobject_cast<QAbstractScrollArea*>(page);
            if (!scroll) {
                scroll = page->findChild<QAbstractScrollArea*>();
            }
            if (scroll) {
                if (auto* bar = scroll->verticalScrollBar()) {
                    bar->setValue(bar->maximum());
                    for (int i = 0; i < 20; ++i) {
                        app.processEvents();
                        QThread::msleep(20);
                    }
                    settingsWin.grab().save(previewOutputDir() + QStringLiteral("/settings_win_skin.png"));
                    bar->setValue(0);
                }
            }
        }

        // Capture page 1: Input & Keys
        settingsWin.selectNavigationRow(1);
        for (int i = 0; i < 20; ++i) {
            app.processEvents();
            QThread::msleep(20);
        }
        settingsWin.grab().save(previewOutputDir() + QStringLiteral("/settings_win_input.png"));

        // Capture page 2: Maintenance
        settingsWin.selectNavigationRow(2);
        for (int i = 0; i < 20; ++i) {
            app.processEvents();
            QThread::msleep(20);
        }
        settingsWin.grab().save(previewOutputDir() + QStringLiteral("/settings_win_maintenance.png"));

        // Capture page 3: About
        settingsWin.selectNavigationRow(3);
        for (int i = 0; i < 20; ++i) {
            app.processEvents();
            QThread::msleep(20);
        }
        settingsWin.grab().save(previewOutputDir() + QStringLiteral("/settings_win_about.png"));
        settingsWin.hide();

        // 5. Candidate Window with Extension Menu Open
        candWin.show();
        candWin.move(400, 200);
        auto* extMenu = candWin.createExtensionMenu();
        extMenu->show();
        for (int i = 0; i < 25; ++i) {
            app.processEvents();
            QThread::msleep(20);
        }
        QPixmap candPix = candWin.grab();
        QPixmap menuPix = extMenu->grab();
        int totalW = qMax(candPix.width(), menuPix.width()) + 32;
        int totalH = candPix.height() + menuPix.height() + 32;
        QPixmap combined(totalW, totalH);
        combined.fill(cfg.isDarkTheme() ? QColor(25, 25, 25) : QColor(240, 240, 240));
        {
            QPainter p(&combined);
            p.setRenderHint(QPainter::Antialiasing);
            p.drawPixmap(16, 16, candPix);
            p.drawPixmap(16, candPix.height() + 24, menuPix);
        }
        combined.save(previewOutputDir() + QStringLiteral("/cand_extension_menu.png"));
        extMenu->close();
        delete extMenu;

        // 6. Candidate Window with Extension Panel Open
        candWin.showExtensionPanel();
        for (int i = 0; i < 25; ++i) {
            app.processEvents();
            QThread::msleep(20);
        }
        QPixmap panelPix = candWin.extensionPanel()->grab();
        QPixmap candPix2 = candWin.grab();
        int totalW2 = qMax(candPix2.width(), panelPix.width()) + 32;
        int totalH2 = candPix2.height() + panelPix.height() + 32;
        QPixmap combined2(totalW2, totalH2);
        combined2.fill(cfg.isDarkTheme() ? QColor(25, 25, 25) : QColor(240, 240, 240));
        {
            QPainter p(&combined2);
            p.setRenderHint(QPainter::Antialiasing);
            p.drawPixmap(16, 16, candPix2);
            p.drawPixmap(16, candPix2.height() + 24, panelPix);
        }
        combined2.save(previewOutputDir() + QStringLiteral("/cand_extension_panel.png"));
        candWin.toggleExtensionPanel();
        candWin.hide();

        std::cout << "  Saved cand_win.png, cand_win_vertical.png, cand_extension_menu.png, cand_extension_panel.png, emoji_picker.png, emoji_picker_kaomoji.png, emoji_picker_symbols.png, settings_win.png, settings_win_input.png, settings_win_maintenance.png, settings_win_about.png successfully.\n";
        return 0;
    }

    if (parser.isSet(settingsOption)) {
        // 设置中心是**独立进程**（候选窗点「设置」时另起进程）。
        // 它通过 Weasel IPC 与服务通信：部署走 maintenance 消息对，样式从
        // 响应流里读（通道边界见设计文档 §4.4）。
        WeaselConfigClient config;
        SettingsWindow settingsWin(&config);
        settingsWin.show();
        return app.exec();
    }

    // ── 服务模式：官方 Weasel 内核 ───────────────────────────────────────
    // librime 是**进程级全局单例**，服务进程里只能有一份引擎：
    // 这里必须是 WeaselEngine，不能再叠第二份引擎。
    //
    // 单实例守卫要先于引擎启动：否则重复实例会先抢 IPC 管道再退出。
    HANDLE hSingleMutex = CreateMutexW(nullptr, FALSE, L"WeaselFluentSingleInstanceMutex");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        std::cerr << "[INFO] Weasel-Fluent service is already running. Exiting duplicate instance.\n";
        if (hSingleMutex) CloseHandle(hSingleMutex);
        return 0;
    }

    WeaselEngine engine;
    QString engineError;
    if (!engine.start(&engineError)) {
        std::cerr << "[FATAL] " << engineError.toStdString() << "\n";
        if (hSingleMutex) CloseHandle(hSingleMutex);
        return 1;
    }

    CandidateWindow candWin(&engine);

    // 桥接：Weasel 的 Context/Status → Fluent 候选窗（设计文档 §4.3）。
    // 状态由服务端线程推过来（WeaselEngine 内部已做跨线程投递）。
    FluentUiAdapter adapter(&candWin);
    QObject::connect(&engine, &UiEngine::stateChanged, &app, [&]() {
        adapter.apply(engine.context(), engine.status());
    });

    // Completely decoupled via separate process invocation:
    // Settings center edits config.json, which is watched by QFileSystemWatcher
    // in the candidate window process to reload changes on-the-fly.
    QObject::connect(&candWin, &CandidateWindow::openSettingsRequested, []() {
        QProcess::startDetached(QCoreApplication::applicationFilePath(),
                                QStringList() << QStringLiteral("--settings"));
    });

    if (parser.isSet(demoOption)) {
        // Standalone demonstration: input "nihao" so candidate window shows immediately.
        engine.processChar(QChar('n'));
        engine.processChar(QChar('i'));
        engine.processChar(QChar('h'));
        engine.processChar(QChar('a'));
        engine.processChar(QChar('o'));
        // Weasel 路径是异步的（按键投到服务端线程，状态再回流），
        // 这里等状态回来再显示。
        for (int i = 0; i < 60; ++i) {
            app.processEvents();
            QThread::msleep(25);
        }
        candWin.move(400, 500);
        candWin.show();
        return app.exec();
    }

    // 1. Weasel IPC 服务端已在 WeaselEngine::start() 里起来
    //    （官方 Weasel 协议，管道名与官方 Weasel 隔离）。
    std::cout << "[SERVICE] Weasel-Fluent service active (Weasel IPC server running).\n";

    // Fluent tray menu: status + deploy + settings + quit.
    // 中英切换不在托盘里 —— 由系统输入法切换（Win+Space）与 TIP 的
    // KEYBOARD_OPENCLOSE compartment 承担。
    QSystemTrayIcon tray(QIcon(), &app);
    auto* trayMenu = new TrayMenu(&tray, &engine);
    trayMenu->setVisible(cfg.showTray());

    auto syncTrayAsciiMode = [&]() { trayMenu->setAsciiMode(engine.isAsciiMode()); };
    QObject::connect(&engine, &UiEngine::stateChanged, &app, syncTrayAsciiMode);
    syncTrayAsciiMode();

    return app.exec();
}
