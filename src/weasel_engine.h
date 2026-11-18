#ifndef WEASEL_ENGINE_H
#define WEASEL_ENGINE_H

#include <QMutex>
#include <QObject>
#include <QSemaphore>
#include <QString>

#include <WeaselIPC.h>
#include <WeaselIPCData.h>
#include <RimeWithWeasel.h>
// RimeWithWeasel.h 间接引入了 librime 的 rime_api.h，而它把 Bool/True/False
// 定义成了宏 —— 与 Qt 的 QVariant::Type::Bool 正面相撞。立刻清掉。
// 详见 librime_compat.h 里的完整说明。
#include "librime_compat.h"

#include <atomic>
#include <deque>
#include <memory>
#include <string>
#include <thread>

#include "ui_engine.h"

/// 封装官方 Weasel 的 Rime 引擎与 IPC 服务端。
///
/// 上游 Weasel 的分层是「服务持有引擎 + TIP 作为客户端」；本类维持同一分层，
/// 但把「可见的候选窗」交给 Fluent 前端：
///
///   * weasel::UI 只作为**数据载体** —— 绝不调用 Create()，因此不会创建
///     Weasel 的 Direct2D 面板（与 Fluent 候选窗二选一，见设计文档 §4.2 R4）；
///   * RimeWithWeaselHandler 在每次状态变化时把 Context/Status 写进这个 UI 对象
///     （上游 RimeWithWeasel.cpp: m_ui->Update(ctx, status)），并通过
///     OnUpdateUI 回调通知「该刷新了」—— 我们在这里把状态快照给 Qt 线程。
///
/// 线程模型：
///   * 上游的 `weasel::Server::Start()` 会在**调用它的线程**上创建窗口，
///     `Run()` 必须在同一个线程上跑消息循环（否则收不到消息）。
///     因此整个服务端（建窗口 + 初始化引擎 + 消息循环）都在独立线程上；
///   * `RimeWithWeaselHandler` 的状态（会话表等）只在**服务端线程**上安全，
///     所以 Qt 线程发来的交互命令（按键 / 选词 / 翻页 / 选项）一律排队，
///     用 `WM_COMMAND` 投到服务端窗口，由服务端线程执行。
class WeaselEngine : public UiEngine {
    Q_OBJECT
public:
    explicit WeaselEngine(QObject* parent = nullptr);
    ~WeaselEngine() override;

    /// 启动 IPC 服务端并初始化 Rime 引擎。
    bool start(QString* error = nullptr);

    /// 停止 IPC 服务端与引擎。
    void stop();

    // ── UiEngine（候选窗 / 扩展面板 / 托盘用） ─────────────────────────
    RimeUiState uiState() const override;

    bool processKey(int keyCode, int mask = 0) override;
    bool processChar(QChar ch) override;
    bool selectCandidate(int index) override;
    void commitDirectText(const QString& text) override;

    bool isAsciiMode() const override;
    void setAsciiMode(bool ascii) override;
    bool isSimplified() const override;
    void setSimplified(bool simp) override;
    bool isFullShape() const override;
    void setFullShape(bool full) override;

    bool selectSchema(const QString& schemaId) override;
    QString currentSchema() const override;
    bool deploy() override;

    // ── Weasel 专有 ────────────────────────────────────────────────────
    /// 当前组合态快照（线程安全）。
    weasel::Context context() const;

    /// 当前状态快照（线程安全）。
    weasel::Status status() const;

    /// 当前样式（weasel.yaml → weasel::UIStyle）。
    weasel::UIStyle style() const;

    /// 触发全量重新部署（RimeStartMaintenance(FullDeploy) + Join）。
    bool deployFull(QString* error = nullptr);

    /// IPC 服务端窗口句柄（安装器与托盘用）。
    HWND serverHwnd() const;

    /// 服务端是否已就绪。
    bool isRunning() const { return m_running.load(); }

public:
    /// 注册语言栏（输入法工具栏）的菜单命令。
    void registerLanguageBarCommands();

    /// 在服务端线程把排队的命令执行掉。
    /// 公开是因为命令窗口的窗口过程要调用它。
    void executePendingCommands();

private:
    /// 交互命令（在服务端线程执行）。
    enum class CommandType {
        Key,
        SelectCandidate,
        ChangePage,
        CommitComposition,
        ClearComposition,
        SetOption,
        Deploy,
    };

    struct Command {
        CommandType type = CommandType::Key;
        weasel::KeyEvent key;
        int index = 0;
        bool backward = false;
        std::string option;
        bool value = false;
    };

    /// 在 Qt 线程把命令排队，并投递给服务端线程。
    void postCommand(const Command& cmd);

    /// 在服务端线程执行单条命令。
    void dispatchCommand(const Command& cmd);
    /// 当前应当操作的会话：优先 TIP 正在用的会话，否则用本进程自建的会话。
    DWORD currentSessionId() const;

    /// 在 IPC 线程上被调用：取快照并投递到 Qt 线程。
    void onServerThreadUpdate();

    weasel::UI* m_ui = nullptr;
    std::unique_ptr<RimeWithWeaselHandler> m_handler;
    weasel::Server* m_server = nullptr;
    std::thread m_serverThread;
    std::atomic<bool> m_running{false};
    /// 自建的命令窗口：UI 侧的命令通过它投递给服务端线程。
    std::atomic<HWND> m_commandHwnd{nullptr};
    /// 服务端线程 id（engine-only 模式下用来退出我们自己的消息循环）。
    std::atomic<DWORD> m_serverThreadId{0};
    /// 本进程自建的会话（没有 TIP 客户端时使用）。
    std::atomic<DWORD> m_localSessionId{0};
    /// 简繁状态。weasel::Status 不带这个字段，故由本类记录，
    /// 它与投给 Rime 的 "simplification" 选项始终一致。
    std::atomic<bool> m_simplified{true};

    mutable QMutex m_stateMutex;
    weasel::Context m_context;
    weasel::Status m_status;
    weasel::UIStyle m_style;

    QMutex m_commandMutex;
    std::deque<Command> m_commands;
    QSemaphore m_readySemaphore;
};

#endif // WEASEL_ENGINE_H
