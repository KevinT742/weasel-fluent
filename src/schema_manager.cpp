#include "schema_manager.h"

#include "paths.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QRegularExpression>
#include <QTextStream>

namespace {

/// 读取 UTF-8 文本文件，失败返回空串。
QString readTextFile(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QString();
    }
    const QByteArray raw = f.readAll();
    f.close();
    return QString::fromUtf8(raw);
}

/// 极简 YAML 读取：只取 schema: 块下的 name: 与 schema_id: 两级缩进。
/// 不引入 YAML 库 —— 我们只需要这两个字段。
QPair<QString, QString> parseHeader(const QString& text) {
    QString name;
    QString id;
    bool inSchema = false;
    const QStringList lines = text.split(QLatin1Char('\n'));
    for (const QString& raw : lines) {
        const QString line = raw.trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')) || line.startsWith(QLatin1Char('-'))) {
            continue;
        }

        const int indent = raw.length() - raw.trimmed().length();
        if (indent == 0) {
            inSchema = line.startsWith(QStringLiteral("schema:"));
            continue;
        }
        if (!inSchema) {
            continue;
        }
        if (line.startsWith(QStringLiteral("name:"))) {
            name = line.mid(5).trimmed();
        } else if (line.startsWith(QStringLiteral("schema_id:"))) {
            id = line.mid(10).trimmed();
        }
    }
    return {name, id};
}

/// 去掉 YAML 值的引号。
QString unquote(QString v) {
    v = v.trimmed();
    if (v.size() >= 2 && ((v.startsWith(QLatin1Char('"')) && v.endsWith(QLatin1Char('"'))) ||
                          (v.startsWith(QLatin1Char('\'')) && v.endsWith(QLatin1Char('\''))))) {
        return v.mid(1, v.size() - 2);
    }
    return v;
}

} // namespace

SchemaManager& SchemaManager::instance() {
    static SchemaManager s_inst;
    return s_inst;
}

QPair<QString, QString> SchemaManager::parseSchemaHeader(const QString& yamlText) {
    return parseHeader(yamlText);
}

QString SchemaManager::parseSchemaName(const QString& yamlPath) {
    return parseHeader(readTextFile(yamlPath)).first;
}

QString SchemaManager::parseSchemaId(const QString& yamlPath) {
    return parseHeader(readTextFile(yamlPath)).second;
}

QStringList SchemaManager::parseSchemaList(const QString& yamlText) {
    QStringList ids;
    bool inList = false;
    const QStringList lines = yamlText.split(QLatin1Char('\n'));
    for (const QString& raw : lines) {
        const QString line = raw.trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#'))) {
            continue;
        }
        const int indent = raw.length() - raw.trimmed().length();
        if (indent == 0) {
            inList = line.startsWith(QStringLiteral("schema_list:"));
            continue;
        }
        if (!inList) {
            continue;
        }
        if (line.startsWith(QStringLiteral("- schema:"))) {
            ids << unquote(line.mid(QStringLiteral("- schema:").size()));
        }
    }
    return ids;
}

QString SchemaManager::rewriteSchemaList(const QString& yamlText, const QString& schemaId) {
    if (schemaId.isEmpty()) {
        return yamlText;
    }

    // 收集原有顺序（去掉目标项），再把目标放到首位。
    QStringList others;
    for (const QString& id : parseSchemaList(yamlText)) {
        if (id != schemaId) {
            others << id;
        }
    }
    others.prepend(schemaId);

    QStringList out;
    out << QStringLiteral("schema_list:");
    for (const QString& id : others) {
        out << QStringLiteral("  - schema: %1").arg(id);
    }
    const QString listBlock = out.join(QLatin1Char('\n'));

    const QRegularExpression listRe(QStringLiteral("(?ms)^schema_list:\\s*\\n(?:\\s*-\\s*schema:.*\\n)*"));
    if (listRe.match(yamlText).hasMatch()) {
        QString result = yamlText;
        return result.replace(listRe, listBlock + QLatin1Char('\n'));
    }
    return listBlock + QLatin1Char('\n') + yamlText;
}

QVector<SchemaInfo> SchemaManager::scanSchemas() const {
    QVector<SchemaInfo> result;

    // 方案可能来自两处：用户 Rime 目录（用户自己安装的）与共享数据目录
    // （随发行包分发的内置方案）。用户目录优先。
    const QString userDir = WeaselPaths::userDataDir();
    const QString sharedDir = WeaselPaths::sharedDataDir();

    QVector<QPair<QString, bool>> searchDirs;  // (目录, 是否用户目录)
    if (!userDir.isEmpty()) {
        searchDirs.append({userDir, true});
    }
    if (!sharedDir.isEmpty() && sharedDir != userDir) {
        searchDirs.append({sharedDir, false});
    }

    QHash<QString, SchemaInfo> byId;
    QStringList order;

    for (const auto& dirEntry : searchDirs) {
        const QString dirPath = dirEntry.first;
        const bool isUserDir = dirEntry.second;
        const QString buildDir = userDir + QStringLiteral("/build");

        const QDir dir(dirPath);
        const QStringList files = dir.entryList(QStringList() << QStringLiteral("*.schema.yaml"),
                                                QDir::Files, QDir::Name);
        for (const QString& f : files) {
            SchemaInfo info;
            info.fileName = f;
            const auto hdr = parseHeader(readTextFile(dir.filePath(f)));
            info.name = unquote(hdr.first);
            info.id = unquote(hdr.second);
            if (info.id.isEmpty()) {
                // schema_id 缺省时退回到文件名（去掉 .schema.yaml）。
                info.id = f;
                info.id.remove(QStringLiteral(".schema.yaml"));
            }
            if (info.name.isEmpty()) {
                info.name = info.id;
            }
            info.deployed = QFileInfo::exists(buildDir + QStringLiteral("/") + f);

            if (byId.contains(info.id)) {
                // 用户目录中的同名方案覆盖共享目录中的。
                const int idx = order.indexOf(info.id);
                if (idx >= 0) {
                    byId[info.id] = info;
                }
                continue;
            }
            byId.insert(info.id, info);
            order.append(info.id);
        }
    }

    for (const QString& id : order) {
        result.append(byId.value(id));
    }
    return result;
}

bool SchemaManager::writeSchemaSelection(const QString& schemaId, QString* error) {
    if (schemaId.isEmpty()) {
        if (error) *error = QStringLiteral("方案 id 为空");
        return false;
    }
    const QString userDir = WeaselPaths::userDataDir();
    if (userDir.isEmpty()) {
        if (error) *error = QStringLiteral("无法定位用户 Rime 目录");
        return false;
    }

    // 1) default.yaml —— 保证 schema_id 在 schema_list 中且位于首位。
    const QString defaultPath = userDir + QStringLiteral("/default.yaml");
    QString defaultText = readTextFile(defaultPath);
    if (defaultText.isEmpty()) {
        defaultText = QStringLiteral("# Rime default settings\n# encoding: utf-8\n\n");
    }
    defaultText = rewriteSchemaList(defaultText, schemaId);
    {
        QFile f(defaultPath);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
            if (error) *error = QStringLiteral("无法写入 default.yaml");
            return false;
        }
        f.write(defaultText.toUtf8());
        f.close();
    }

    // 2) default.custom.yaml —— 写入 var.previously_selected_schema。
    const QString customPath = userDir + QStringLiteral("/default.custom.yaml");
    QString customText = readTextFile(customPath);
    const QRegularExpression varRe(
        QStringLiteral("(?m)^(\\s*)previously_selected_schema:.*$"));
    if (varRe.match(customText).hasMatch()) {
        customText.replace(varRe,
                           QStringLiteral("\\1previously_selected_schema: ") + schemaId);
    } else {
        const QString entry = QStringLiteral("  previously_selected_schema: ") + schemaId;
        const QRegularExpression varHead(QStringLiteral("(?m)^var:\\s*$"));
        if (varHead.match(customText).hasMatch()) {
            customText.replace(varHead, QStringLiteral("var:\n") + entry);
        } else {
            customText = QStringLiteral("var:\n") + entry + QStringLiteral("\n") + customText;
        }
    }
    {
        QFile f(customPath);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
            if (error) *error = QStringLiteral("无法写入 default.custom.yaml");
            return false;
        }
        f.write(customText.toUtf8());
        f.close();
    }

    return true;
}
