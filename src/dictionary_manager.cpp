#include "dictionary_manager.h"

#include "paths.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>

namespace {

QString readTextFile(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QString();
    }
    const QByteArray raw = f.readAll();
    f.close();
    return QString::fromUtf8(raw);
}

/// 词库条目在 default.custom.yaml 中的写法：  - name.dict.yaml
QString entryLine(const QString& dictFileName) {
    return QStringLiteral("  - ") + dictFileName;
}

/// 从 default.custom.yaml 文本中取出 dictionary: 块下的条目名。
QStringList mountedEntries(const QString& yamlText) {
    QStringList names;
    bool inDict = false;
    const QStringList lines = yamlText.split(QLatin1Char('\n'));
    for (const QString& raw : lines) {
        const QString trimmed = raw.trimmed();
        if (trimmed.isEmpty() || trimmed.startsWith(QLatin1Char('#'))) {
            continue;
        }
        const int indent = raw.length() - raw.trimmed().length();
        if (indent == 0) {
            inDict = trimmed.startsWith(QStringLiteral("dictionary:"));
            continue;
        }
        if (!inDict) {
            continue;
        }
        if (trimmed.startsWith(QLatin1Char('-'))) {
            QString name = trimmed.mid(1).trimmed();
            name.remove(QStringLiteral(".dict.yaml"));
            names << name;
        }
    }
    return names;
}

} // namespace

DictionaryManager& DictionaryManager::instance() {
    static DictionaryManager s_inst;
    return s_inst;
}

bool DictionaryManager::looksLikeDictionary(const QString& text) {
    if (text.trimmed().isEmpty()) {
        return false;
    }
    if (text.startsWith(QStringLiteral("---"))) {
        return true;
    }
    // 纯文本词库：每行是「词条<TAB>...」
    return text.contains(QLatin1Char('\t'));
}

QStringList DictionaryManager::parseMountedDictionaries(const QString& yamlText) {
    return mountedEntries(yamlText);
}

bool DictionaryManager::patchDictionaryList(const QString& yamlText,
                                            const QString& addName,
                                            const QString& removeName,
                                            QString* outText) {
    QString text = yamlText;

    // --- 摘除 ---
    if (!removeName.isEmpty()) {
        const QString target = removeName.endsWith(QStringLiteral(".dict.yaml"))
                                   ? removeName
                                   : removeName + QStringLiteral(".dict.yaml");
        QStringList kept;
        bool inDict = false;
        const QStringList lines = text.split(QLatin1Char('\n'));
        for (const QString& raw : lines) {
            const QString trimmed = raw.trimmed();
            if (trimmed.isEmpty()) {
                kept << raw;
                continue;
            }
            const int indent = raw.length() - raw.trimmed().length();
            if (indent == 0) {
                inDict = trimmed.startsWith(QStringLiteral("dictionary:"));
                kept << raw;
                continue;
            }
            if (inDict && trimmed.startsWith(QLatin1Char('-')) &&
                trimmed.mid(1).trimmed() == target) {
                continue; // 丢弃这一条
            }
            kept << raw;
        }
        text = kept.join(QLatin1Char('\n'));
    }

    // --- 追加 ---
    if (!addName.isEmpty()) {
        const QString fileName = addName.endsWith(QStringLiteral(".dict.yaml"))
                                     ? addName
                                     : addName + QStringLiteral(".dict.yaml");
        const QStringList existing = mountedEntries(text);
        if (!existing.contains(fileName)) {
            const QString entry = entryLine(fileName);
            const QRegularExpression head(QStringLiteral("(?m)^dictionary:\\s*$"));
            if (head.match(text).hasMatch()) {
                text.replace(head, QStringLiteral("dictionary:\n") + entry);
            } else {
                text = QStringLiteral("dictionary:\n") + entry + QStringLiteral("\n") + text;
            }
        }
    }

    if (outText) {
        *outText = text;
    }
    return true;
}

QVector<DictionaryEntry> DictionaryManager::listDictionaries() const {
    QVector<DictionaryEntry> out;
    const QString userDir = WeaselPaths::userDataDir();
    if (userDir.isEmpty()) {
        return out;
    }
    const QDir dir(userDir);
    const QStringList files = dir.entryList(QStringList() << QStringLiteral("*.dict.yaml"),
                                            QDir::Files, QDir::Name);
    // 已挂载到当前方案的词库（来自 default.custom.yaml）。
    const QStringList mounted = mountedEntries(
        readTextFile(userDir + QStringLiteral("/default.custom.yaml")));

    for (const QString& f : files) {
        DictionaryEntry e;
        e.name = f;
        e.name.remove(QStringLiteral(".dict.yaml"));
        e.path = dir.filePath(f);
        // 方案内置词库（如 luna_pinyin）不算用户自定义。
        e.custom = !mounted.contains(e.name);
        out.append(e);
    }
    return out;
}

bool DictionaryManager::importDictionary(const QString& srcPath, QString* error) {
    const QFileInfo src(srcPath);
    if (!src.exists() || !src.isFile()) {
        if (error) *error = QStringLiteral("源文件不存在");
        return false;
    }

    const QString text = readTextFile(srcPath);
    if (!looksLikeDictionary(text)) {
        if (error) *error = QStringLiteral("不是有效的词库文件（需为 .dict 或 .dict.yaml）");
        return false;
    }

    const QString userDir = WeaselPaths::userDataDir();
    const QString baseName = src.completeBaseName();   // 去掉最后一个扩展名
    QString fileName = baseName;
    if (!fileName.endsWith(QStringLiteral(".dict"))) {
        fileName = fileName + QStringLiteral(".dict.yaml");
    }
    const QString target = userDir + QStringLiteral("/") + fileName;

    if (QFileInfo::exists(target) && QFileInfo(target) != src) {
        if (error) *error = QStringLiteral("已存在同名词库：%1").arg(fileName);
        return false;
    }
    if (!QFile::copy(srcPath, target)) {
        if (error) *error = QStringLiteral("复制词库失败");
        return false;
    }

    // 挂载到当前方案。
    const QString customPath = userDir + QStringLiteral("/default.custom.yaml");
    QString customText = readTextFile(customPath);
    QString patched;
    patchDictionaryList(customText, fileName, QString(), &patched);
    {
        QFile f(customPath);
        if (f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
            f.write(patched.toUtf8());
            f.close();
        }
    }
    return true;
}

bool DictionaryManager::removeDictionary(const QString& name, QString* error) {
    const QString userDir = WeaselPaths::userDataDir();
    const QString fileName = name.endsWith(QStringLiteral(".dict.yaml"))
                                 ? name
                                 : name + QStringLiteral(".dict.yaml");
    const QString target = userDir + QStringLiteral("/") + fileName;
    if (!QFileInfo::exists(target)) {
        if (error) *error = QStringLiteral("词库不存在：%1").arg(fileName);
        return false;
    }
    if (!QFile::remove(target)) {
        if (error) *error = QStringLiteral("删除词库文件失败");
        return false;
    }

    const QString customPath = userDir + QStringLiteral("/default.custom.yaml");
    QString customText = readTextFile(customPath);
    QString patched;
    patchDictionaryList(customText, QString(), fileName, &patched);
    {
        QFile f(customPath);
        if (f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
            f.write(patched.toUtf8());
            f.close();
        }
    }
    return true;
}
