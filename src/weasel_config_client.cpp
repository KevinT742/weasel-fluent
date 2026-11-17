#include "weasel_config_client.h"

#include <ResponseParser.h>

#include <QDebug>

#include <windows.h>

#include "schema_manager.h"

WeaselConfigClient::WeaselConfigClient() = default;

WeaselConfigClient::~WeaselConfigClient() {
    disconnectFromService();
}

bool WeaselConfigClient::ensureConnected(QString* error) {
    if (m_connected) {
        return true;
    }

    // ⚠️ 不能直接调 `weasel::Client::Connect()`：上游 `PipeChannelBase::_Connect()`
    // 在服务端没起来时会**永远阻塞**在 `WaitNamedPipe` 上，设置中心的界面就卡死了。
    // 所以先用 0 超时的 WaitNamedPipe 探一下管道在不在。
    const std::wstring pipeName = weasel::GetPipeName();
    if (!::WaitNamedPipe(pipeName.c_str(), 0)) {
        if (error) {
            *error = QStringLiteral("Weasel 服务未运行（请先启动 Weasel-Fluent 服务）");
        }
        return false;
    }

    if (!m_client.Connect()) {
        if (error) *error = QStringLiteral("无法连接 Weasel 服务");
        return false;
    }
    m_client.StartSession();
    m_connected = true;
    return true;
}

void WeaselConfigClient::disconnectFromService() {
    if (!m_connected) {
        return;
    }
    m_connected = false;
    m_client.Disconnect();
}

bool WeaselConfigClient::fetchResponse(weasel::Status* status, weasel::UIStyle* style) const {
    if (!m_connected) {
        return false;
    }
    // 发一个空按键，逼服务端回一份响应（上游 TIP 也是这么干的）。
    m_client.ProcessKeyEvent(weasel::KeyEvent(0, 0));

    weasel::ResponseParser parser(nullptr, nullptr, status, nullptr, style);
    return m_client.GetResponseData(std::ref(parser));
}

bool WeaselConfigClient::deployFull(QString* error) {
    if (!ensureConnected(error)) {
        return false;
    }
    // 上游的部署消息对：服务端 finalize 掉引擎再重新 initialize，
    // 重新初始化时若工作区过期就会触发 Rime 的增量部署。
    m_client.StartMaintenance();
    m_client.EndMaintenance();
    // maintenance 会清掉所有会话，重新连一次。
    m_connected = false;
    m_client.Disconnect();

    if (error) *error = QStringLiteral("已重新部署");
    return true;
}

bool WeaselConfigClient::selectSchema(const QString& schemaId, QString* error) {
    // 方案清单走文件系统（通道边界见设计文档 §4.4）：把选中项写进
    // default.custom.yaml，再走 IPC 部署让新方案生效。
    QString writeError;
    if (!SchemaManager::instance().writeSchemaSelection(schemaId, &writeError)) {
        if (error) *error = writeError;
        return false;
    }
    return deployFull(error);
}

QString WeaselConfigClient::activeSchema() const {
    weasel::Status status;
    if (!fetchResponse(&status, nullptr)) {
        return QString();
    }
    return QString::fromStdWString(status.schema_id);
}

bool WeaselConfigClient::loadStyle(weasel::UIStyle* out, QString* error) {
    if (!ensureConnected(error)) {
        return false;
    }
    weasel::UIStyle style;
    if (!fetchResponse(nullptr, &style)) {
        if (error) *error = QStringLiteral("读取服务端样式失败");
        return false;
    }
    if (out) {
        *out = style;
    }
    return true;
}
