#include "weasel_engine.h"

#include <QDebug>
#include <QMetaObject>

#include <rime_api.h>

WeaselEngine::WeaselEngine(QObject* parent) : QObject(parent) {}

WeaselEngine::~WeaselEngine() {
    stop();
}

bool WeaselEngine::start(QString* error) {
    if (m_running) {
        return true;
    }

    // weasel::UI 只作数据载体：绝不 Create()，因此不会有 D2D 候选面板窗口。
    m_ui = new weasel::UI();

    m_handler = std::make_unique<RimeWithWeaselHandler>(m_ui);

    m_server = new weasel::Server();
    m_server->SetRequestHandler(m_handler.get());

    if (!m_server->Start()) {
        if (error) *error = QStringLiteral("Weasel IPC 服务启动失败");
        return false;
    }

    m_handler->Initialize();

    // 上游用这个回调通知「托盘要刷新」；我们用它把组合态送到 Qt 线程。
    m_handler->OnUpdateUI([this]() { onServerThreadUpdate(); });

    // IPC 消息循环是阻塞的 Win32 循环，放到独立线程，避免与 Qt 事件循环打架。
    m_running = true;
    m_serverThread = std::thread([this]() {
        if (m_server) {
            m_server->Run();
        }
    });

    if (error) {
        *error = QStringLiteral("Weasel IPC 服务已启动");
    }
    return true;
}

void WeaselEngine::stop() {
    if (!m_running) {
        return;
    }
    m_running = false;

    if (m_server) {
        m_server->Stop();
    }
    if (m_serverThread.joinable()) {
        m_serverThread.join();
    }
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

void WeaselEngine::onServerThreadUpdate() {
    if (!m_ui) {
        return;
    }
    {
        QMutexLocker lock(&m_stateMutex);
        m_context = m_ui->ctx();
        m_status = m_ui->status();
    }
    // 切回 Qt 线程再发信号，避免跨线程直接碰界面。
    QMetaObject::invokeMethod(this, [this]() { emit stateChanged(); }, Qt::QueuedConnection);
}

weasel::Context WeaselEngine::context() const {
    QMutexLocker lock(&m_stateMutex);
    return m_context;
}

weasel::Status WeaselEngine::status() const {
    QMutexLocker lock(&m_stateMutex);
    return m_status;
}

bool WeaselEngine::deployFull(QString* error) {
    // librime 1.17 的 rime_api.h 只把维护接口放在 RimeApi vtable 上
    // （start_maintenance / join_maintenance_thread），没有独立的
    // RimeStartMaintenance() 自由函数 —— 那两个符号虽在 DLL 里导出，但不再声明。
    RimeApi* api = rime_get_api();
    if (!api) {
        if (error) *error = QStringLiteral("无法获取 Rime API");
        return false;
    }
    // full_check = true —— 全量重建（重新编译方案与词库）
    if (!api->start_maintenance(true)) {
        if (error) *error = QStringLiteral("Rime 拒绝了全量部署请求");
        return false;
    }
    api->join_maintenance_thread();
    return true;
}

HWND WeaselEngine::serverHwnd() const {
    return m_server ? m_server->GetHWnd() : nullptr;
}
