#ifndef SCHEMA_MANAGER_H
#define SCHEMA_MANAGER_H

#include <QObject>
#include <QPair>
#include <QString>
#include <QVector>

/// 一个 Rime 输入方案的元信息。
struct SchemaInfo {
    QString id;        ///< schema_id
    QString name;      ///< 供界面显示的名字
    QString fileName;  ///< 源文件名，如 luna_pinyin.schema.yaml
    bool deployed = false; ///< 是否已在 build/ 中编译
};

/// 枚举用户 Rime 目录下的输入方案，并负责改写方案清单与选中项。
class SchemaManager : public QObject {
    Q_OBJECT
public:
    static SchemaManager& instance();

    /// 扫描用户 Rime 目录下的 *.schema.yaml。
    /// 同时检查 build/ 目录，标记该方案是否已部署。
    QVector<SchemaInfo> scanSchemas() const;

    /// 把 schemaId 写进 default.custom.yaml 的 var.previously_selected_schema，
    /// 并确保它出现在 default.yaml 的 schema_list 中。
    bool writeSchemaSelection(const QString& schemaId, QString* error);

    /// 从 schema yaml 中解析 schema.name，解析不到返回空串。
    static QString parseSchemaName(const QString& yamlPath);

    /// 从 schema yaml 中解析 schema.schema_id，解析不到返回空串。
    static QString parseSchemaId(const QString& yamlPath);

    /// 供测试使用：把 yaml 文本解析成 (name, schema_id)。
    static QPair<QString, QString> parseSchemaHeader(const QString& yamlText);

    /// 供测试使用：把 default.yaml 文本里的 schema_list 解析成 id 列表。
    static QStringList parseSchemaList(const QString& yamlText);

    /// 供测试使用：改写 default.yaml 文本的 schema_list。
    /// 保证 schemaId 在列表中且位于首位，其余项保持原有相对顺序。
    static QString rewriteSchemaList(const QString& yamlText, const QString& schemaId);
};

#endif // SCHEMA_MANAGER_H
