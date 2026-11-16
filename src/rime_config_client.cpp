#include "rime_config_client.h"

#include "rime_engine.h"

bool EngineConfigClient::deployFull(QString* error) {
    if (!m_engine) {
        if (error) *error = QStringLiteral("引擎未初始化");
        return false;
    }
    return m_engine->deployFull(error);
}

bool EngineConfigClient::selectSchema(const QString& schemaId, QString* error) {
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

QString EngineConfigClient::activeSchema() const {
    return m_engine ? m_engine->currentSchema() : QString();
}
