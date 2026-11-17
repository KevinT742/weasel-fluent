#ifndef WEASEL_ENGINE_H
#define WEASEL_ENGINE_H

#include <QMutex>
#include <QObject>
#include <QString>

#include <WeaselIPC.h>
#include <WeaselIPCData.h>
#include <RimeWithWeasel.h>

#include <atomic>
#include <memory>
#include <thread>

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
/// 线程模型：上游的 weasel::Server::Run() 是阻塞的 Win32 消息循环，与 Qt 的
/// 事件循环冲突。因此 IPC 循环跑在独立线程上，Qt 主线程只负责界面。
class WeaselEngine : public QObject {
    Q_OBJECT
public:
    explicit WeaselEngine(QObject* parent = nullptr);
    ~WeaselEngine() override;

    /// 启动 IPC 服务端并初始化 Rime 引擎。
    bool start(QString* error = nullptr);

    /// 停止 IPC 服务端与引擎。
    void stop();

    /// 当前组合态快照（线程安全）。
    weasel::Context context() const;

    /// 当前状态快照（线程安全）。
    weasel::Status status() const;

    /// 触发全量重新部署（RimeStartMaintenance(FullDeploy) + Join）。
    bool deployFull(QString* error = nullptr);

    /// IPC 服务端窗口句柄（安装器与托盘用）。
    HWND serverHwnd() const;

signals:
    /// 组合态/状态发生变化（在 Qt 线程发出）。
    void stateChanged();

private:
    /// 在 IPC 线程上被调用：取快照并投递到 Qt 线程。
    void onServerThreadUpdate();

    weasel::UI* m_ui = nullptr;
    std::unique_ptr<RimeWithWeaselHandler> m_handler;
    weasel::Server* m_server = nullptr;
    std::thread m_serverThread;
    std::atomic<bool> m_running{false};

    mutable QMutex m_stateMutex;
    weasel::Context m_context;
    weasel::Status m_status;
};

#endif // WEASEL_ENGINE_H
