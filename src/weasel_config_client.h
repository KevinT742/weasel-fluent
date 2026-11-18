#ifndef WEASEL_CONFIG_CLIENT_H
#define WEASEL_CONFIG_CLIENT_H

#include "rime_config_client.h"

#include <WeaselIPC.h>
#include <WeaselIPCData.h>

#include <QString>

/// 基于官方 Weasel IPC 的配置操作实现。
///
/// 只覆盖真正走 IPC 的两项（通道边界见设计文档 §4.4）：
///   - **部署**：Weasel IPC 的 maintenance 消息对
///     （`WEASEL_IPC_START_MAINTENANCE` / `WEASEL_IPC_END_MAINTENANCE`），
///     服务端会 finalize 再重新 initialize 引擎，重新初始化时若工作区过期
///     就触发 Rime 的增量部署 —— 这正是上游部署器的工作模式；
///   - **样式**：响应流里的 `weasel::UIStyle`，由上游 `ResponseParser` 反序列化。
///
/// 方案清单 / 用户词库 / 快捷键**不在 IPC 上**，仍由 `SchemaManager` /
/// `DictionaryManager` / `KeyBindings` 负责（它们读的是 Rime 自己的文件）。
///
/// 设置中心是**独立进程**，所以这个类自己持有一个 `weasel::Client` 去连服务。
class WeaselConfigClient : public RimeConfigClient {
public:
    WeaselConfigClient();
    ~WeaselConfigClient() override;

    /// 连上服务（幂等）。
    bool ensureConnected(QString* error = nullptr);

    /// 是否已经连上服务。
    bool isConnected() const { return m_connected; }

    /// 断开。
    void disconnectFromService();

    bool deployFull(QString* error) override;
    bool selectSchema(const QString& schemaId, QString* error) override;
    QString activeSchema() const override;

    /// 读取服务端当前样式（weasel.yaml → weasel::UIStyle）。
    bool loadStyle(WeaselStyle* out, QString* error = nullptr) override;

    /// 写入 weasel.yaml 并重新部署。
    bool applyStyle(const WeaselStyle& style, QString* error) override;

private:
    /// 向服务端要一份响应（发一个空按键逼服务端回包，和上游 TIP 同一个套路）。
    bool fetchResponse(weasel::Status* status, weasel::UIStyle* style) const;

    mutable weasel::Client m_client;
    mutable weasel::Status m_status;
    mutable weasel::UIStyle m_style;
    bool m_connected = false;
};

#endif // WEASEL_CONFIG_CLIENT_H
