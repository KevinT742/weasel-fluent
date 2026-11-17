#ifndef RIME_CONFIG_CLIENT_H
#define RIME_CONFIG_CLIENT_H

#include <QString>

/// Rime 配置操作的抽象（设计文档 §4.1 的「切换点」）。
///
/// 实现只有一个：`WeaselConfigClient` —— 走官方 Weasel 的 IPC 通道。
/// 旧的 `EngineConfigClient`（基于自研 RimeEngine）已随过渡实现一起删除（R6）。
///
/// 方案清单 / 用户词库 / 快捷键**不在这个接口里**：它们走文件系统
/// （`SchemaManager` / `DictionaryManager` / `KeyBindings`），通道边界见 §4.4。
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

#endif // RIME_CONFIG_CLIENT_H
