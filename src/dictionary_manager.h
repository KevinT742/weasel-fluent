#ifndef DICTIONARY_MANAGER_H
#define DICTIONARY_MANAGER_H

#include <QObject>
#include <QString>
#include <QVector>

/// 一个词库条目。
struct DictionaryEntry {
    QString name;      ///< 不含扩展名的词库名
    QString path;      ///< 绝对路径
    bool custom = false; ///< 是否为用户导入（而非方案内置）
};

/// 管理用户自定义词库：导入、移除、挂载到当前方案。
class DictionaryManager : public QObject {
    Q_OBJECT
public:
    static DictionaryManager& instance();

    /// 列出用户 Rime 目录下的所有词库文件。
    QVector<DictionaryEntry> listDictionaries() const;

    /// 导入词库：复制到用户 Rime 目录并挂载到 default.custom.yaml。
    bool importDictionary(const QString& srcPath, QString* error);

    /// 移除词库：删除文件并从 default.custom.yaml 的 dictionary 列表摘除。
    bool removeDictionary(const QString& name, QString* error);

    /// 在 default.custom.yaml 文本中增删 dictionary 条目。
    /// addName / removeName 为空表示该侧不改动。成功时 outText 持有结果。
    static bool patchDictionaryList(const QString& yamlText,
                                    const QString& addName,
                                    const QString& removeName,
                                    QString* outText);

    /// 粗略校验文本是否为词库文件（首行 --- 或含制表符分隔的词条）。
    static bool looksLikeDictionary(const QString& text);

    /// 供测试使用：解析 default.custom.yaml 文本中已挂载的词库名列表。
    static QStringList parseMountedDictionaries(const QString& yamlText);
};

#endif // DICTIONARY_MANAGER_H
