#ifndef RIME_CONFIG_CLIENT_H
#define RIME_CONFIG_CLIENT_H

#include <QString>

class RimeEngine;

/// Rime 配置操作的抽象。
///
/// Round 1 由本仓的 RimeEngine 实现；Round 2 改接官方 Weasel 的
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

/// 基于本仓 RimeEngine 的 RimeConfigClient 实现。
class EngineConfigClient : public RimeConfigClient {
public:
    explicit EngineConfigClient(RimeEngine* engine) : m_engine(engine) {}

    bool deployFull(QString* error) override;

    bool selectSchema(const QString& schemaId, QString* error) override;

    QString activeSchema() const override;

private:
    RimeEngine* m_engine = nullptr;
};

#endif // RIME_CONFIG_CLIENT_H
