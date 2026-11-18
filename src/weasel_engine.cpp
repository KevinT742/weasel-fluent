#include "weasel_engine.h"

#include <QDebug>
#include <QMetaObject>

#include <rime_api.h>

#include <windows.h>

#include <atomic>
#include <vector>

#include "fluent_ui_adapter.h"
#include "schema_manager.h"

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

/// 记录「当前活动会话」的 handler。
///
/// 上游 `RimeWithWeaselHandler::m_active_session` 是私有的，而我们的候选窗
/// 需要对**用户正在使用的那一个会话**发指令（自己新建会话会得到不同的
/// 组合态，候选列表就对不上了）。这里用继承 + 覆写的方式把它记下来，
/// 不用改 vendor 源码。
class TrackingHandler : public RimeWithWeaselHandler {
public:
    explicit TrackingHandler(weasel::UI* ui) : RimeWithWeaselHandler(ui) {}

    BOOL ProcessKeyEvent(weasel::KeyEvent keyEvent, WeaselSessionId ipc_id,
                         EatLine eat) override {
        m_lastSessionId.store(ipc_id);
        return RimeWithWeaselHandler::ProcessKeyEvent(keyEvent, ipc_id, eat);
    }

    WeaselSessionId lastSessionId() const { return m_lastSessionId.load(); }

private:
    std::atomic<WeaselSessionId> m_lastSessionId{0};
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

    m_handler = std::make_unique<TrackingHandler>(m_ui);

    m_server = new weasel::Server();
    m_server->SetRequestHandler(m_handler.get());
    m_server->AddMenuHandler(kWeaselFluentCommandId, [this]() {
        executePendingCommands();
        return true;
    });

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

void WeaselEngine::stop() {
    if (!m_running.exchange(false)) {
        return;
    }

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

HWND WeaselEngine::serverHwnd() const {
    return m_server ? m_server->GetHWnd() : nullptr;
}
