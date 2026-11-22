#include "weasel_engine.h"

#include <QApplication>
#include <QDebug>
#include <QProcess>
#include <QMetaObject>
#include <QSet>

#include <rime_api.h>
// rime_api.h 自带 include guard，此时已是空操作；带上 compat 头是为了让
// 「宏泄漏」这件事在任何包含顺序下都能被立刻发现，而不是让 Qt 头莫名其妙地解析失败。
#include "librime_compat.h"

// 编译期探针：librime 的 Bool/True/False 宏绝不能活到 Qt 头解析之后。
// 若哪天被重新引入，Qt 的 QVariant::Type::Bool 会被改写成 `int = QMetaType::int`，
// MSVC 会在 qvariant.h:146 报 C2144/C2589 —— 报错位置离病因极远，极难排查。
// 这个探针把错误拉回到真正的源头。
#if defined(Bool) || defined(True) || defined(False)
#error "librime 的 Bool/True/False 宏泄漏了 —— 检查 src/librime_compat.h 的包含位置"
#endif

#include <windows.h>

#include <atomic>
#include <map>
#include <mutex>
#include <vector>

#include <WeaselUtility.h>
#include <resource.h>

#include "ascii_switch.h"
#include "fluent_ui_adapter.h"
#include "key_bindings.h"
#include "paths.h"
#include "schema_manager.h"

// ascii_switch 里镜像的 ibus 掩码位必须和上游头文件一致，否则判定会静默失配。
static_assert(ascii_switch::kShiftMask == ibus::SHIFT_MASK, "Shift 掩码位与上游 ibus::Modifier 不一致");
static_assert(ascii_switch::kControlMask == ibus::CONTROL_MASK, "Control 掩码位与上游 ibus::Modifier 不一致");
static_assert(ascii_switch::kAltMask == ibus::ALT_MASK, "Alt 掩码位与上游 ibus::Modifier 不一致");
static_assert(ascii_switch::kReleaseMask == ibus::RELEASE_MASK, "Release 掩码位与上游 ibus::Modifier 不一致");

namespace {

/// 本仓自定义的 WM_COMMAND 命令号。
///
/// 上游的 `ServerImpl::OnCommand` 会把 WM_COMMAND 的 LOWORD(wParam) 当作
/// 命令号，在 `m_MenuHandlers` 里查回调并**在服务端消息线程上执行** ——
/// 这正是我们需要的能力：候选窗在 Qt 线程上，但它对 handler 的调用
/// 必须回到服务端线程（handler 的会话表不是线程安全的）。
/// 上游用的是 40001–40016（resource.h 的 ID_WEASELTRAY_*），这里从 41001 起。
constexpr UINT kWeaselFluentCommandId = 41001;

/// 自建命令窗口用的消息号。
constexpr UINT kCommandMessageId = WM_APP + 1;

/// 命令窗口的类名。
constexpr const wchar_t* kCommandWindowClass = L"WeaselFluentCommandWindow";

/// 命令窗口的窗口过程：把 UI 命令交给服务端线程执行。
LRESULT CALLBACK CommandWindowProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    if (msg == kCommandMessageId) {
        auto* engine = reinterpret_cast<WeaselEngine*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (engine) {
            engine->executePendingCommands();
        }
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

/// ibus 掩码里的修饰位全集（比较用）。
///
/// 上游 `ConvertKeyEvent` 只可能设出 SHIFT / CONTROL / ALT 三种
/// （LOCK 是 CapsLock 状态，与切换键无关；SUPER / META 在 Windows 上压根不设）。
constexpr UINT kIbusModifierBits = ascii_switch::kModifierMask;

/// 把输入框矩形的变化记到临时目录的日志里。
///
/// 为什么需要：TIP 送来的矩形是「哪个控件的哪个范围」的判断结果，
/// 而 Win32 没有标准 API 能反查「这个矩形到底是谁给的」。出了问题时
/// 只能把两边的数字摆在一起看：
///   * TIP 送来的 rc（服务端已换算过的值）；
///   * 系统里**真实的光标位置**（GetGUIThreadInfo 的 hwndCaret + ClientToScreen）。
/// 两者对不上 ⇒ 是 TIP 取错了矩形；两者一致 ⇒ 是本仓的坐标换算错了。
///
/// 默认关闭：设环境变量 WEASEL_FLUENT_POSITION_LOG=1 才落盘，
/// 免得每次输入都往临时目录写文件。
///
/// 用 GetTempPath 而不是固定路径：既不依赖安装位置，也不会污染用户目录。
void logInputPosition(RECT const& rc) {
    static const bool enabled = []() {
        wchar_t flag[8] = {0};
        return ::GetEnvironmentVariableW(L"WEASEL_FLUENT_POSITION_LOG", flag,
                                         _countof(flag)) > 0 &&
               flag[0] == L'1';
    }();
    if (!enabled) {
        return;
    }

    static std::atomic<int> counter{0};
    // 前 200 条足够定位一次输入会话，又不会把日志刷爆。
    if (counter.fetch_add(1) >= 200) {
        return;
    }

    wchar_t tempDir[MAX_PATH] = {0};
    ::GetTempPathW(_countof(tempDir), tempDir);
    const std::wstring path = std::wstring(tempDir) + L"weasel-fluent-position.log";

    GUITHREADINFO gti = {0};
    gti.cbSize = sizeof(gti);
    HWND caretWnd = nullptr;
    POINT caret = {0};
    if (::GetGUIThreadInfo(::GetWindowThreadProcessId(::GetForegroundWindow(), nullptr),
                           &gti)) {
        caretWnd = gti.hwndCaret;
        caret.x = gti.rcCaret.left;
        caret.y = gti.rcCaret.top;
    }

    HWND fg = ::GetForegroundWindow();
    RECT fgRect = {0};
    if (fg) {
        ::GetWindowRect(fg, &fgRect);
    }
    POINT caretScreen = caret;
    if (caretWnd) {
        ::ClientToScreen(caretWnd, &caretScreen);
    }
    POINT caretClientOrigin = {0};
    if (caretWnd) {
        ::ClientToScreen(caretWnd, &caretClientOrigin);
    }

    const std::string line =
        "rc=(" + std::to_string(rc.left) + "," + std::to_string(rc.top) + "," +
        std::to_string(rc.right) + "," + std::to_string(rc.bottom) + ")" +
        " fg=0x" + std::to_string(reinterpret_cast<uintptr_t>(fg)) +
        " fgRect=(" + std::to_string(fgRect.left) + "," + std::to_string(fgRect.top) + "," +
        std::to_string(fgRect.right) + "," + std::to_string(fgRect.bottom) + ")" +
        " caretWnd=0x" + std::to_string(reinterpret_cast<uintptr_t>(caretWnd)) +
        " caretClient=(" + std::to_string(caret.x) + "," + std::to_string(caret.y) + ")" +
        " caretScreen=(" + std::to_string(caretScreen.x) + "," +
        std::to_string(caretScreen.y) + ")" +
        " caretOrigin=(" + std::to_string(caretClientOrigin.x) + "," +
        std::to_string(caretClientOrigin.y) + ")" +
        " dpi=" + std::to_string(fg ? ::GetDpiForWindow(fg) : 0) +
        " sysDpi=" + std::to_string(::GetDpiForSystem()) +
        " serverAwareness=" +
        std::to_string(::GetAwarenessFromDpiAwarenessContext(
            ::GetThreadDpiAwarenessContext())) +
        "\r\n";

    HANDLE file = ::CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, nullptr,
                                OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return;
    }
    DWORD written = 0;
    ::WriteFile(file, line.data(), static_cast<DWORD>(line.size()), &written, nullptr);
    ::CloseHandle(file);
}

/// Qt 键码 → ibus 键码集合。
///
/// ibus 的键码约定：小写 ASCII 就是字符本身（XK_c == 0x63），
/// 而 TIP 的 `ConvertKeyEvent` 在按键不是修饰键时走 `ToUnicodeEx`，
/// 所以 Shift+A 送上来的是 'A'（0x41）—— 字母两种大小写都要收。
void appendIbusKeycodes(int qtKey, QSet<UINT>& out) {
    if (qtKey >= Qt::Key_A && qtKey <= Qt::Key_Z) {
        const char lower = static_cast<char>(qtKey);
        out.insert(static_cast<UINT>(lower));
        out.insert(static_cast<UINT>(lower - 'a' + 'A'));
        return;
    }
    if (qtKey >= 0x20 && qtKey <= 0x7E) {
        out.insert(static_cast<UINT>(qtKey));   // 空格与标点：ASCII 码即 ibus 码
        return;
    }
    switch (qtKey) {
    case Qt::Key_Tab:        out.insert(ibus::Tab); break;
    case Qt::Key_Return:
    case Qt::Key_Enter:      out.insert(ibus::Return); break;
    case Qt::Key_Escape:     out.insert(ibus::Escape); break;
    case Qt::Key_Backspace:  out.insert(ibus::BackSpace); break;
    case Qt::Key_Delete:     out.insert(ibus::Delete); break;
    case Qt::Key_Insert:     out.insert(ibus::Insert); break;
    case Qt::Key_Home:       out.insert(ibus::Home); break;
    case Qt::Key_End:        out.insert(ibus::End); break;
    case Qt::Key_Left:       out.insert(ibus::Left); break;
    case Qt::Key_Up:         out.insert(ibus::Up); break;
    case Qt::Key_Right:      out.insert(ibus::Right); break;
    case Qt::Key_Down:       out.insert(ibus::Down); break;
    case Qt::Key_PageUp:     out.insert(ibus::Page_Up); break;
    case Qt::Key_PageDown:   out.insert(ibus::Page_Down); break;
    case Qt::Key_F1:         out.insert(ibus::F1); break;
    case Qt::Key_F2:         out.insert(ibus::F2); break;
    case Qt::Key_F3:         out.insert(ibus::F3); break;
    case Qt::Key_F4:         out.insert(ibus::F4); break;
    case Qt::Key_F5:         out.insert(ibus::F5); break;
    case Qt::Key_F6:         out.insert(ibus::F6); break;
    case Qt::Key_F7:         out.insert(ibus::F7); break;
    case Qt::Key_F8:         out.insert(ibus::F8); break;
    case Qt::Key_F9:         out.insert(ibus::F9); break;
    case Qt::Key_F10:        out.insert(ibus::F10); break;
    case Qt::Key_F11:        out.insert(ibus::F11); break;
    case Qt::Key_F12:        out.insert(ibus::F12); break;
    default: break;
    }
}

/// 修饰键规格 → ibus 键码 + 期望修饰位。
///
/// "Shift" 两侧都收；"Shift_L" / "Shift_R" 只收一侧。
/// 这套左右之分是中文输入法的老规矩：左 Shift 切中英，右 Shift 另有其用。
bool parseModifierSwitchSpec(const QString& base, bool hasSide, bool isLeft,
                              ascii_switch::Spec& out) {
    const QString b = base.toLower();
    UINT ownMask = 0;
    UINT leftCode = 0;
    UINT rightCode = 0;
    if (b == QLatin1String("shift")) {
        ownMask = ibus::SHIFT_MASK;   leftCode = ibus::Shift_L;   rightCode = ibus::Shift_R;
    } else if (b == QLatin1String("ctrl")) {
        ownMask = ibus::CONTROL_MASK; leftCode = ibus::Control_L; rightCode = ibus::Control_R;
    } else if (b == QLatin1String("alt")) {
        ownMask = ibus::ALT_MASK;     leftCode = ibus::Alt_L;     rightCode = ibus::Alt_R;
    } else if (b == QLatin1String("win")) {
        // TIP 的 TranslateKeycode 把 VK_LWIN / VK_RWIN 译成 Meta_L / Meta_R。
        ownMask = ibus::META_MASK;    leftCode = ibus::Meta_L;    rightCode = ibus::Meta_R;
    } else {
        return false;
    }
    out.expectedMods = ownMask;
    out.isModifier = true;
    if (hasSide) {
        out.keycodes.insert(isLeft ? leftCode : rightCode);
    } else {
        out.keycodes.insert(leftCode);
        out.keycodes.insert(rightCode);
    }
    out.valid = true;
    return true;
}

/// 规格串 → 中英文切换键的匹配条件。
ascii_switch::Spec parseAsciiSwitchSpec(const QString& spec) {
    ascii_switch::Spec out;
    const QString s = spec.trimmed();
    if (s.isEmpty()) {
        return out;
    }
    // 纯修饰键规格（"Shift" / "Shift_L" / "Ctrl_R" …）。
    if (!s.contains(QLatin1Char('+'))) {
        QString base;
        bool isLeft = false;
        const bool hasSide = KeyActions::splitSideSuffix(s, &base, &isLeft);
        if (parseModifierSwitchSpec(base, hasSide, isLeft, out)) {
            return out;
        }
    }
    // 一般组合键（"Ctrl+Shift+A" 之类）。
    int qtKey = 0;
    Qt::KeyboardModifiers mods = Qt::NoModifier;
    if (!KeyBindings::parseSpec(s, &qtKey, &mods)) {
        return out;
    }
    QSet<UINT> keycodes;
    appendIbusKeycodes(qtKey, keycodes);
    if (keycodes.isEmpty()) {
        return out;
    }
    for (UINT code : keycodes) {
        out.keycodes.insert(code);
    }
    if (mods & Qt::ShiftModifier)   out.expectedMods |= ibus::SHIFT_MASK;
    if (mods & Qt::ControlModifier) out.expectedMods |= ibus::CONTROL_MASK;
    if (mods & Qt::AltModifier)     out.expectedMods |= ibus::ALT_MASK;
    out.valid = true;
    return out;
}

/// 记录「当前活动会话」的 handler。
///
/// 上游 `RimeWithWeaselHandler::m_active_session` 是私有的，而我们的候选窗
/// 需要对**用户正在使用的那一个会话**发指令（自己新建会话会得到不同的
/// 组合态，候选列表就对不上了）。这里用继承 + 覆写的方式把它记下来，
/// 不用改 vendor 源码。
class TrackingHandler : public RimeWithWeaselHandler {
public:
    explicit TrackingHandler(weasel::UI* ui, WeaselEngine* engine)
        : RimeWithWeaselHandler(ui), m_engine(engine) {}

    BOOL ProcessKeyEvent(weasel::KeyEvent keyEvent, WeaselSessionId ipc_id,
                         EatLine eat) override {
        if (!sessionAlive(ipc_id)) {
            return FALSE;
        }
        m_lastSessionId.store(ipc_id);

        // ── 中英文切换键 ────────────────────────────────────────────────
        //
        // 判定必须在 Rime 处理**之前**做：要读「按下瞬间」的 ascii_mode，
        // 因为 ascii_composer 的 inline_ascii（用户方案里 Shift_L 就是它）
        // 会在按住 Shift 期间把 ascii_mode 临时置成 true。
        //
        // 但**落地**必须在 Rime 处理**之后**：inline_ascii 在抬手时会把
        // ascii_mode 恢复回去，不后置就会被它覆盖掉。
        //
        // 按键**不吞掉** —— 吞掉的话 Shift+A 的大小写就没了。
        refreshSwitchSpec();
        bool desired = false;
        const bool cachedAscii = m_engine ? m_engine->isAsciiMode() : false;
        const bool currentAscii = knownAsciiMode(ipc_id, cachedAscii);
        const bool toggle = m_switchDetector.handle(
            keyEvent.keycode,
            (keyEvent.mask & ibus::RELEASE_MASK) == 0,
            keyEvent.mask & kIbusModifierBits,
            currentAscii,
            &desired);

        BOOL eaten = RimeWithWeaselHandler::ProcessKeyEvent(keyEvent, ipc_id, eat);

        if (toggle) {
            // 上游的 SetOption 会顺手刷新 UI（状态与语言栏图标）。
            SetOption(ipc_id, "ascii_mode", desired);
        }
        return eaten;
    }

    void ClearComposition(WeaselSessionId ipc_id) override {
        if (!sessionAlive(ipc_id)) {
            return;
        }
        m_lastSessionId.store(ipc_id);
        RimeWithWeaselHandler::ClearComposition(ipc_id);
    }

    void CommitComposition(WeaselSessionId ipc_id) override {
        if (!sessionAlive(ipc_id)) {
            return;
        }
        m_lastSessionId.store(ipc_id);
        RimeWithWeaselHandler::CommitComposition(ipc_id);
    }

    void SelectCandidateOnCurrentPage(size_t index, WeaselSessionId ipc_id) override {
        if (!sessionAlive(ipc_id)) {
            return;
        }
        RimeWithWeaselHandler::SelectCandidateOnCurrentPage(index, ipc_id);
    }

    bool HighlightCandidateOnCurrentPage(size_t index, WeaselSessionId ipc_id,
                                         EatLine eat) override {
        if (!sessionAlive(ipc_id)) {
            return false;
        }
        return RimeWithWeaselHandler::HighlightCandidateOnCurrentPage(index, ipc_id, eat);
    }

    bool ChangePage(bool backward, WeaselSessionId ipc_id, EatLine eat) override {
        if (!sessionAlive(ipc_id)) {
            return false;
        }
        return RimeWithWeaselHandler::ChangePage(backward, ipc_id, eat);
    }

    void FocusIn(DWORD param, WeaselSessionId ipc_id) override {
        if (!sessionAlive(ipc_id)) {
            return;
        }
        m_lastSessionId.store(ipc_id);
        RimeWithWeaselHandler::FocusIn(param, ipc_id);
    }

    void FocusOut(DWORD param, WeaselSessionId ipc_id) override {
        if (!sessionAlive(ipc_id)) {
            return;
        }
        RimeWithWeaselHandler::FocusOut(param, ipc_id);
    }

    void SetOption(WeaselSessionId ipc_id, const std::string& opt, bool val) override {
        // 语言栏的「切换中/英」**不带会话 id**（上游 TrayCommand 的 lParam 是 0），
        // 上游于是落到 m_active_session —— 那是 vendor 的私有成员，我们看不到。
        // 这里改用自己记的 active 会话：它要是已经随维护失效了，整条命令丢掉，
        // 免得打进 Rime 里那个不存在的 session。
        if (!ipc_id) {
            ipc_id = m_lastSessionId.load();
        }
        if (!sessionAlive(ipc_id)) {
            return;
        }

        // 记住我们自己设过的中英状态 —— 判定「要不要切」必须用**实时**值。
        //
        // 为什么不能直接用缓存的 m_status：librime 的 ascii_mode 是**异步生效**
        // 的（set_option 之后要等引擎处理待办事件，get_status 才反映出来）。
        // 缓存因此可能还停在旧值 —— 连点两下 Shift 时，第二次会拿旧值取反，
        // 算出「还是原来那个模式」的目标，看起来就是 Shift 没反应（实测 2026-11-22）。
        //
        // 这里记的是**我们自己**写进去的值：inline_ascii 那种「按住 Shift 临时
        // 置成 true、抬手再恢复」的临时态走的是 Rime 内部，不会经过 SetOption，
        // 所以不会污染这里记下的「稳定态」。
        if (opt == "ascii_mode") {
            std::lock_guard<std::mutex> lock(m_asciiMutex);
            m_asciiMode[ipc_id] = val;
        }
        RimeWithWeaselHandler::SetOption(ipc_id, opt, val);
    }

    DWORD RemoveSession(WeaselSessionId ipc_id) override {
        {
            std::lock_guard<std::mutex> lock(m_asciiMutex);
            m_asciiMode.erase(ipc_id);
        }
        if (!sessionAlive(ipc_id)) {
            // 会话已经随维护消失 ⇒ 只清我们自己的记录，别再去 destroy。
            return 0;
        }
        return RimeWithWeaselHandler::RemoveSession(ipc_id);
    }

    void UpdateInputPosition(RECT const& rc, WeaselSessionId ipc_id) override {
        // [weasel-fluent] Pass input-box rect to WeaselEngine for candidate
        // window follow. The rect comes from the TIP client (already converted
        // to logical pixels by the IPC server).
        logInputPosition(rc);
        if (m_engine) {
            m_engine->setInputBox(QRect(rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top));
        }
        m_lastSessionId.store(ipc_id);
        RimeWithWeaselHandler::UpdateInputPosition(rc, ipc_id);
    }

    WeaselSessionId lastSessionId() const { return m_lastSessionId.load(); }

private:
    /// 这个会话在 Rime 那边**还活着**吗？
    ///
    /// 维护操作（重新部署 / 同步用户数据）会重建 Rime 的 SessionManager，
    /// 旧会话全部消失 —— 但客户端（TIP）手里还攥着旧的 ipc_id，而 vendor 的
    /// `to_session_id()` 会把会话表里那个**已经失效**的 session_id 取出来。
    /// 之后任何针对它的 Rime 调用（clear_composition / process_key /
    /// set_option …）都会打进一个不存在的会话：librime 1.17 在这些接口里
    /// **不做空指针检查** ⇒ rime.dll 里抛 0xC0000005，**整个服务进程直接
    /// 崩掉**（实测 2026-11-22：语言栏点一下「同步用户数据」，随后来一条
    /// 普通消息，服务就没了；HEAD 构建同样复现，与本仓改动无关）。
    ///
    /// FindSession 走的是 `rime_api->find_session` —— 只查表、不碰会话对象，
    /// 维护之后再问一次也只是返回 0（实测安全，服务照常活着）。
    /// 所以：**进 Rime 之前先问一句**，死了就直接把这条消息丢掉。
    ///
    /// 代价：维护之后的那一个会话是废的，直到失焦再回来（TIP 会重新
    /// StartSession 拿到新 id）。这比整个服务崩掉好得多。
    bool sessionAlive(WeaselSessionId ipc_id) {
        return ipc_id != 0 && FindSession(ipc_id) != 0;
    }

    /// 取这个会话**已知**的中英状态；没有记录时回退到引擎缓存的值。
    bool knownAsciiMode(WeaselSessionId ipc_id, bool fallback) const {
        std::lock_guard<std::mutex> lock(m_asciiMutex);
        auto it = m_asciiMode.find(ipc_id);
        return it == m_asciiMode.end() ? fallback : it->second;
    }

    /// 配置变了才重新解析切换键规格（每个按键都解析一次太浪费）。
    void refreshSwitchSpec() {
        const QString spec = KeyActions::effectiveAsciiSwitchSpec();
        if (spec == m_switchSpecText) {
            return;
        }
        m_switchSpecText = spec;
        m_switchDetector.setSpec(parseAsciiSwitchSpec(spec));
    }

    std::atomic<WeaselSessionId> m_lastSessionId{0};
    WeaselEngine* m_engine = nullptr;
    ascii_switch::Detector m_switchDetector;
    QString m_switchSpecText;
    mutable std::mutex m_asciiMutex;  // mutable：判定函数是 const 的
    std::map<WeaselSessionId, bool> m_asciiMode;
};

} // namespace

WeaselEngine::WeaselEngine(QObject* parent) : UiEngine(parent) {}

WeaselEngine::~WeaselEngine() {
    stop();
}

bool WeaselEngine::start(QString* error) {
    if (m_running.load()) {
        return true;
    }

    // weasel::UI 只作数据载体：绝不 Create()，因此不会有 D2D 候选面板窗口。
    m_ui = new weasel::UI();

    m_handler = std::make_unique<TrackingHandler>(m_ui, this);

    m_server = new weasel::Server();
    m_server->SetRequestHandler(m_handler.get());
    m_server->AddMenuHandler(kWeaselFluentCommandId, [this]() {
        executePendingCommands();
        return true;
    });
    registerLanguageBarCommands();

    // 服务端整体跑在独立线程上：建窗口、初始化引擎、跑消息循环必须在同一个
    // 线程（上游 weasel::Server::Start() 在调用线程创建窗口，Run() 是该线程的
    // 消息循环）。Qt 主线程只负责界面。
    m_running = true;
    m_serverThread = std::thread([this]() {
        m_serverThreadId.store(::GetCurrentThreadId());
        HWND hwnd = m_server->Start();

        // 自建「命令窗口」：UI 侧（候选窗 / 扩展面板 / 托盘）的按键、选词、
        // 翻页、选项命令都投递给它，由**服务端线程**执行 ——
        // handler 的会话表不是线程安全的。
        //
        // 为什么不用上游服务端自己的窗口：上游 `weasel::Server::Start()` 在
        // 单实例互斥量被占用时直接返回 0（没有窗口），此时我们仍要能驱动引擎
        // （自检探针与服务并存、重复实例等场景）。
        WNDCLASSEXW wc;
        ZeroMemory(&wc, sizeof(wc));
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = CommandWindowProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = kCommandWindowClass;
        RegisterClassExW(&wc);   // 重复注册会失败，忽略即可
        HWND cmdHwnd = CreateWindowExW(0, kCommandWindowClass, L"", 0, 0, 0, 0, 0,
                                       HWND_MESSAGE, nullptr, wc.hInstance, nullptr);
        if (cmdHwnd) {
            SetWindowLongPtrW(cmdHwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
            m_commandHwnd.store(cmdHwnd);
        }

        // ⚠️ 状态回流回调**必须无条件**挂上：即使 IPC 服务端没抢到（另一个实例
        // 占着单实例互斥量），引擎照样工作，组合态也照样要能送到 Qt 线程 ——
        // 否则候选窗永远拿不到状态（实测：服务在跑时探针取不到任何候选）。
        m_handler->OnUpdateUI([this]() { onServerThreadUpdate(); });

        if (!hwnd) {
            qWarning("Weasel IPC 服务端启动失败（可能已有实例在跑），本次仅以本地引擎模式运行。");
        }

        // 引擎初始化与**有没有窗口无关**：即便 IPC 服务端没抢到，引擎也必须
        // 初始化，否则状态永远是空的。
        m_handler->Initialize();

        // 自建一个会话：没有 TIP 客户端时（独立运行 / 自检套件），
        // 候选窗的操作要作用在某个会话上。
        // 注意 _ReadClientInfo 会按 WEASEL_IPC_BUFFER_LENGTH 读缓冲区，
        // 所以缓冲区必须给足长度。
        std::vector<wchar_t> clientInfo(WEASEL_IPC_BUFFER_LENGTH, 0);
        m_localSessionId.store(m_handler->AddSession(clientInfo.data()));

        m_readySemaphore.release();
        if (hwnd) {
            // 上游的消息循环：同时会泵我们自己的命令窗口（同一线程的窗口
            // 都由 GetMessage/DispatchMessage 分发）。
            m_server->Run();
        } else {
            // 没有服务端窗口 ⇒ 自己跑一个消息循环，专门处理 UI 命令。
            MSG msg;
            while (::GetMessageW(&msg, nullptr, 0, 0) > 0) {
                ::TranslateMessage(&msg);
                ::DispatchMessageW(&msg);
            }
        }
    });

    m_readySemaphore.acquire();

    if (error) {
        *error = QStringLiteral("Weasel IPC 服务已启动");
    }
    return true;
}

void WeaselEngine::registerLanguageBarCommands() {
    if (!m_server) {
        return;
    }

    // ── 语言栏（输入法工具栏）的菜单命令 ──────────────────────────────────
    //
    // 上游是由 WeaselServer 注册这些命令的（WeaselServerApp::SetupMenuHandlers）：
    // 语言栏里点一下，TIP 就用 TrayCommand 消息发给服务端，由服务端的
    // m_MenuHandlers 回调处理。我们取代了 WeaselServer，所以**必须自己注册**，
    // 否则语言栏的「部署 / 设置 / 退出…」点了没反应。
    //
    // ⚠️ 线程模型：这些回调跑在**管道的 worker 线程**上，**不是**服务端的消息线程。
    // 上游 WeaselServerImpl.cpp：PipeServer::Listen 每接一个连接就 boost::thread
    // 起一个 _ProcessPipeThread，消息走 HandlePipeMessage → OnCommand →
    // m_MenuHandlers[uID]()，全程都在那个 worker 线程上（上游还专门为托盘刷新
    // 加了一次「投回服务端消息线程」的跳转，见 SetTrayRefreshCallback）。
    // 所以这里的规矩是：
    //   * 纯 Win32 调用（ShellExecuteW）可以直接做；
    //   * 碰 Qt 的（QProcess / qApp）必须用 QueuedConnection 投回 Qt 线程；
    //   * WeaselEngine::deploy() 走 postCommand()（互斥量 + PostMessage），跨线程安全。

    const QString appDir = QCoreApplication::applicationDirPath();
    const QString userRimeDir = WeaselPaths::userDataDir();

    // 退出服务（Qt 主线程退出 ⇒ 服务进程结束）。
    m_server->AddMenuHandler(ID_WEASELTRAY_QUIT, [this]() {
        QMetaObject::invokeMethod(qApp, &QCoreApplication::quit, Qt::QueuedConnection);
        return true;
    });

    // 重新部署。注意：本地变量不能也叫 deploy —— 那会把成员函数 WeaselEngine::deploy()
    // 遮住，lambda 里 `deploy()` 就成了「在自己的初始值设定项中引用自己」。
    // deploy() 内部是 postCommand()（互斥量 + PostMessage），跨线程安全。
    const auto redeploy = [this]() {
        this->deploy();
        return true;
    };
    m_server->AddMenuHandler(ID_WEASELTRAY_DEPLOY, redeploy);

    // 「同步用户数据」跟「重新部署」是**两件事**：
    //   重新部署 = librime 重新编译词库（start_maintenance + join）；
    //   同步     = 把用户自造词同步到 sync 目录（sync_user_data）。
    // 上游 WeaselServer 走的是 WeaselDeployer /sync，这里对齐它的语义 ——
    // 之前误把 SYNC 也映射到 deploy，菜单上写着「同步用户数据」实际却在重新编译。
    m_server->AddMenuHandler(ID_WEASELTRAY_SYNC, [this]() {
        this->syncUserData();
        return true;
    });

    // 打开设置中心（独立进程）。
    // QProcess 是 Qt 类，必须投回 Qt 线程再用（见上面的线程模型说明）。
    const auto openSettings = [appDir]() {
        QMetaObject::invokeMethod(
            qApp,
            [appDir]() {
                QProcess::startDetached(
                    appDir + QStringLiteral("\\weasel-fluent-cpp-qt.exe"),
                    QStringList() << QStringLiteral("--settings"));
            },
            Qt::QueuedConnection);
        return true;
    };
    m_server->AddMenuHandler(ID_WEASELTRAY_SETTINGS, openSettings);
    m_server->AddMenuHandler(ID_WEASELTRAY_DICT_MANAGEMENT, openSettings);

    // 打开目录 / 网址。
    // ShellExecuteW 是 W 版，必须传宽串；QString::utf16() 返回的指针在本次调用
    // 期间有效（QString 还活着），ShellExecuteW 内部会复制走。
    const auto openPath = [](const QString& path) {
        if (!path.isEmpty()) {
            ::ShellExecuteW(nullptr, L"open", reinterpret_cast<LPCWSTR>(path.utf16()),
                            nullptr, nullptr, SW_SHOWNORMAL);
        }
        return true;
    };
    m_server->AddMenuHandler(ID_WEASELTRAY_USERCONFIG, [openPath, userRimeDir]() {
        return openPath(userRimeDir);
    });
    m_server->AddMenuHandler(ID_WEASELTRAY_INSTALLDIR, [openPath, appDir]() {
        return openPath(appDir);
    });
    m_server->AddMenuHandler(ID_WEASELTRAY_LOGDIR, [openPath]() {
        return openPath(QString::fromStdWString(WeaselLogPath().wstring()));
    });
    m_server->AddMenuHandler(ID_WEASELTRAY_WIKI, [openPath]() {
        return openPath(QStringLiteral("https://rime.im/docs/"));
    });
    m_server->AddMenuHandler(ID_WEASELTRAY_FORUM, [openPath]() {
        return openPath(QStringLiteral("https://rime.im/discuss/"));
    });
    m_server->AddMenuHandler(ID_WEASELTRAY_HOMEPAGE, [openPath]() {
        return openPath(QStringLiteral("https://rime.im/"));
    });
    m_server->AddMenuHandler(ID_WEASELTRAY_CHECKUPDATE, [openPath]() {
        return openPath(QStringLiteral("https://github.com/KevinT742/weasel-fluent/releases"));
    });

    // 重启服务：拉起 start_service.bat。
    //
    // TIP 侧其实**本地**处理这一条（见 vendor 的 LanguageBar.cpp）：
    // 它读注册表里的 WeaselRoot 再拼 "start_service.bat"。所以这一条注册在
    // 服务端是兜底路径 —— 万一命令被转发到了服务端，也不会变成哑巴。
    // start_service.bat 由 package_dist.ps1 生成并随安装包分发。
    m_server->AddMenuHandler(ID_WEASELTRAY_RERUN_SERVICE, [appDir]() {
        const QString bat = appDir + QStringLiteral("\\start_service.bat");
        ::ShellExecuteW(nullptr, L"open", reinterpret_cast<LPCWSTR>(bat.utf16()),
                        nullptr, nullptr, SW_HIDE);
        return true;
    });
}

void WeaselEngine::stop() {

    if (serverHwnd()) {
        // 正常模式：让上游的消息循环退出。
        m_server->Stop();
    } else {
        // engine-only 模式（上游服务端窗口没建起来）：退出我们自己的消息循环。
        const DWORD tid = m_serverThreadId.load();
        if (tid) {
            ::PostThreadMessageW(tid, WM_QUIT, 0, 0);
        }
    }
    if (m_serverThread.joinable()) {
        m_serverThread.join();
    }
    m_commandHwnd.store(nullptr);
    if (m_handler) {
        m_handler->Finalize();
        m_handler.reset();
    }
    if (m_server) {
        delete m_server;
        m_server = nullptr;
    }
    if (m_ui) {
        m_ui->Destroy(true);
        delete m_ui;
        m_ui = nullptr;
    }
}

// ── 状态快照 ────────────────────────────────────────────────────────────
weasel::Context WeaselEngine::context() const {
    QMutexLocker lock(&m_stateMutex);
    return m_context;
}

weasel::Status WeaselEngine::status() const {
    QMutexLocker lock(&m_stateMutex);
    return m_status;
}

weasel::UIStyle WeaselEngine::style() const {
    QMutexLocker lock(&m_stateMutex);
    return m_style;
}

void WeaselEngine::setInputBox(const QRect& rc) {
    // Called from IPC server thread (TrackingHandler on the pipe worker thread).
    // Store with mutex; Qt thread reads via lastInputBox().
    QMutexLocker lock(&m_inputBoxMutex);
    m_lastInputBox = {true, rc};
}

WeaselEngine::InputBoxRect WeaselEngine::lastInputBox() const {
    QMutexLocker lock(&m_inputBoxMutex);
    return m_lastInputBox;
}

void WeaselEngine::onServerThreadUpdate() {
    if (!m_ui) {
        return;
    }
    {
        QMutexLocker lock(&m_stateMutex);
        m_context = m_ui->ctx();
        m_status = m_ui->status();
        m_style = m_ui->style();
    }
    // 切回 Qt 线程再发信号，避免跨线程直接碰界面。
    QMetaObject::invokeMethod(this, [this]() { emit stateChanged(); }, Qt::QueuedConnection);
}

// ── 命令派发 ────────────────────────────────────────────────────────────
void WeaselEngine::postCommand(const Command& cmd) {
    {
        QMutexLocker lock(&m_commandMutex);
        m_commands.push_back(cmd);
    }
    HWND hwnd = m_commandHwnd.load();
    if (hwnd) {
        PostMessageW(hwnd, kCommandMessageId, 0, 0);
    }
}

void WeaselEngine::executePendingCommands() {
    std::deque<Command> batch;
    {
        QMutexLocker lock(&m_commandMutex);
        batch.swap(m_commands);
    }
    for (const Command& cmd : batch) {
        dispatchCommand(cmd);
    }
}

void WeaselEngine::dispatchCommand(const Command& cmd) {
    if (!m_handler) {
        return;
    }
    const DWORD session = currentSessionId();
    switch (cmd.type) {
    case CommandType::Key: {
        // ⚠️ `eat` 回调**不能传 nullptr**：上游 `_Respond()` 末尾直接调用
        // `eat(header)` / `eat(body)` 而不检查空指针 —— 调用空的 std::function
        // 会抛 std::bad_function_call，在服务端线程里直接终止整个进程
        // （实测 0xC0000409）。这里给一个什么都不做的回调。
        m_handler->ProcessKeyEvent(cmd.key, session,
                                   [](std::wstring&) { return true; });
        break;
    }
    case CommandType::SelectCandidate:
        m_handler->SelectCandidateOnCurrentPage(static_cast<size_t>(cmd.index), session);
        break;
    case CommandType::ChangePage:
        m_handler->ChangePage(cmd.backward, session, nullptr);
        break;
    case CommandType::CommitComposition:
        m_handler->CommitComposition(session);
        break;
    case CommandType::ClearComposition:
        m_handler->ClearComposition(session);
        break;
    case CommandType::SetOption:
        m_handler->SetOption(session, cmd.option, cmd.value);
        break;
    case CommandType::Deploy: {
        // 全量重建：RimeStartMaintenance(FullDeploy) + JoinMaintenanceThread。
        // librime 的维护接口在 RimeApi vtable 上（rime_api.h 只声明了 vtable，
        // 自由函数虽在 DLL 里导出但不再声明）。
        RimeApi* api = rime_get_api();
        if (api) {
            api->start_maintenance(true);
            api->join_maintenance_thread();
        }
        break;
    }
    case CommandType::Sync: {
        // 同步用户数据（把用户词库同步到 sync 目录）。
        // 与 Deploy 分开：部署是重新编译词库，同步是搬运用户自造词 ——
        // 上游 WeaselServer 走的是 WeaselDeployer /sync，语义要对齐。
        RimeApi* api = rime_get_api();
        if (api && !api->sync_user_data()) {
            qWarning() << "[weasel-fluent] 同步用户数据失败";
        }
        break;
    }
    }
}

DWORD WeaselEngine::currentSessionId() const {
    const TrackingHandler* handler = static_cast<const TrackingHandler*>(m_handler.get());
    if (handler) {
        const WeaselSessionId tipSession = handler->lastSessionId();
        if (tipSession) {
            return tipSession;
        }
    }
    return m_localSessionId.load();
}

// ── UiEngine 实现 ───────────────────────────────────────────────────────
RimeUiState WeaselEngine::uiState() const {
    return FluentUiAdapter::toUiState(context(), status());
}

bool WeaselEngine::processKey(int keyCode, int mask) {
    Command cmd;
    cmd.type = CommandType::Key;
    cmd.key = weasel::KeyEvent(static_cast<UINT>(keyCode), static_cast<UINT>(mask));
    postCommand(cmd);
    return true;
}

bool WeaselEngine::processChar(QChar ch) {
    // Rime 的键码约定是 ibus 的：小写 ASCII 就是字母本身（ibus::XK_c == 0x63）。
    return processKey(ch.unicode(), 0);
}

bool WeaselEngine::selectCandidate(int index) {
    Command cmd;
    cmd.type = CommandType::SelectCandidate;
    cmd.index = index;
    postCommand(cmd);
    return true;
}

void WeaselEngine::commitDirectText(const QString& text) {
    if (text.isEmpty()) {
        return;
    }
    // 先把组合态上屏，再把文本送进当前焦点窗口。
    Command commit;
    commit.type = CommandType::CommitComposition;
    postCommand(commit);

    std::vector<INPUT> inputs;
    inputs.reserve(static_cast<size_t>(text.length()) * 2);
    for (int i = 0; i < text.length(); ++i) {
        const WORD utf16 = text.at(i).unicode();

        INPUT down = {};
        down.type = INPUT_KEYBOARD;
        down.ki.wVk = 0;
        down.ki.wScan = utf16;
        down.ki.dwFlags = KEYEVENTF_UNICODE;
        inputs.push_back(down);

        INPUT up = {};
        up.type = INPUT_KEYBOARD;
        up.ki.wVk = 0;
        up.ki.wScan = utf16;
        up.ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;
        inputs.push_back(up);
    }
    if (!inputs.empty()) {
        ::SendInput(static_cast<UINT>(inputs.size()), inputs.data(), sizeof(INPUT));
    }
}

bool WeaselEngine::isAsciiMode() const {
    return status().ascii_mode;
}

void WeaselEngine::setAsciiMode(bool ascii) {
    Command cmd;
    cmd.type = CommandType::SetOption;
    cmd.option = "ascii_mode";
    cmd.value = ascii;
    postCommand(cmd);
}

bool WeaselEngine::isSimplified() const {
    // weasel::Status 里没有 simplified 字段（上游 Status 只有
    // ascii_mode/composing/disabled/full_shape），所以以本类记录的值为准，
    // 它与投给 Rime 的 "simplification" 选项始终一致。
    return m_simplified.load();
}

void WeaselEngine::setSimplified(bool simp) {
    m_simplified.store(simp);
    Command cmd;
    cmd.type = CommandType::SetOption;
    cmd.option = "simplification";
    cmd.value = simp;
    postCommand(cmd);
}

bool WeaselEngine::isFullShape() const {
    return status().full_shape;
}

void WeaselEngine::setFullShape(bool full) {
    Command cmd;
    cmd.type = CommandType::SetOption;
    cmd.option = "full_shape";
    cmd.value = full;
    postCommand(cmd);
}

bool WeaselEngine::selectSchema(const QString& schemaId) {
    // 方案清单走文件系统（通道边界见设计文档 §4.4）：把选中项写进
    // default.custom.yaml，再触发全量部署让新方案生效。
    QString error;
    if (!SchemaManager::instance().writeSchemaSelection(schemaId, &error)) {
        qWarning() << "[weasel-fluent] 切换方案失败:" << error;
        return false;
    }
    return deploy();
}

QString WeaselEngine::currentSchema() const {
    return QString::fromStdWString(status().schema_id);
}

bool WeaselEngine::deploy() {
    Command cmd;
    cmd.type = CommandType::Deploy;
    postCommand(cmd);
    return true;
}

bool WeaselEngine::deployFull(QString* error) {
    // 部署要跑在服务端线程（librime 的维护接口不是线程安全的）。
    // 走的是**命令窗口**，所以 engine-only 模式（没有上游服务端窗口）也能用。
    HWND hwnd = m_commandHwnd.load();
    if (!hwnd) {
        if (error) *error = QStringLiteral("Weasel 服务未运行，无法部署");
        return false;
    }
    Command cmd;
    cmd.type = CommandType::Deploy;
    {
        QMutexLocker lock(&m_commandMutex);
        m_commands.push_back(cmd);
    }
    PostMessageW(hwnd, kCommandMessageId, 0, 0);

    if (error) *error = QStringLiteral("已重新部署");
    return true;
}

bool WeaselEngine::syncUserData(QString* error) {
    // 同步也要跑在服务端线程（librime 的接口不是线程安全的）。
    // 与 deployFull() 一样走命令窗口。
    HWND hwnd = m_commandHwnd.load();
    if (!hwnd) {
        if (error) *error = QStringLiteral("Weasel 服务未运行，无法同步用户数据");
        return false;
    }
    Command cmd;
    cmd.type = CommandType::Sync;
    {
        QMutexLocker lock(&m_commandMutex);
        m_commands.push_back(cmd);
    }
    PostMessageW(hwnd, kCommandMessageId, 0, 0);

    if (error) *error = QStringLiteral("已同步用户数据");
    return true;
}

HWND WeaselEngine::serverHwnd() const {
    return m_server ? m_server->GetHWnd() : nullptr;
}
