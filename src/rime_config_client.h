#ifndef RIME_CONFIG_CLIENT_H
#define RIME_CONFIG_CLIENT_H

#include <QString>

#include "weasel_style.h"

/// Rime 配置操作的抽象（设计文档 §4.1 的「切换点」）。
///
/// 实现只有一个：`WeaselConfigClient` —— 走官方 Weasel 的 IPC 通道。
/// 旧的 `EngineConfigClient`（基于自研 RimeEngine）已随过渡实现一起删除（R6）。
///
/// 通道边界见设计文档 §4.4：
///   * **走 IPC**：重新部署、Weasel 样式（内容层）；
///   * **走文件系统**：方案清单、用户词库、快捷键 —— 由 `SchemaManager` /
///     `DictionaryManager` / `KeyBindings` 负责，不在本接口里。
class RimeConfigClient {
public:
    virtual ~RimeConfigClient() = default;

    /// 全量重建：重新编译方案与词库。失败时 error 非空。
    virtual bool deployFull(QString* error) = 0;

    /// 切换到指定方案并使其生效。
    virtual bool selectSchema(const QString& schemaId, QString* error) = 0;

    /// 当前生效的方案 id。
    virtual QString activeSchema() const = 0;

    /// 读取**服务端当前生效**的 Weasel 样式（响应流里的 `weasel::UIStyle`）。
    virtual bool loadStyle(WeaselStyle* out, QString* error = nullptr) = 0;

    /// 写入 Weasel 样式并重新部署，让新样式生效。
    ///
    /// 样式内容落在用户 Rime 目录的 `weasel.yaml`（服务端 `LoadWeaselUIStyle()`
    /// 读它），部署这一步才走 IPC。
    virtual bool applyStyle(const WeaselStyle& style, QString* error) = 0;
};

#endif // RIME_CONFIG_CLIENT_H
