#include "weasel_style.h"

#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStringList>
#include <QTextStream>

#include "paths.h"

namespace {

/// 读取文本文件（失败返回空串）。
QString readTextFile(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QString();
    }
    const QString text = QString::fromUtf8(f.readAll());
    f.close();
    return text;
}

bool writeTextFile(const QString& path, const QString& text, QString* error) {
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        if (error) *error = QStringLiteral("无法写入 %1").arg(path);
        return false;
    }
    QTextStream out(&f);
    out.setEncoding(QStringConverter::Utf8);
    out << text;
    f.close();
    return true;
}

/// 从一行 yaml 里取出值（去掉注释、引号与首尾空白）。
QString yamlValue(const QString& rawLine) {
    QString line = rawLine;
    const int hash = line.indexOf('#');
    if (hash >= 0) {
        line = line.left(hash);
    }
    line = line.trimmed();
    const int colon = line.indexOf(':');
    if (colon < 0) {
        return QString();
    }
    line = line.mid(colon + 1).trimmed();
    if (line.size() >= 2 && line.startsWith('"') && line.endsWith('"')) {
        line = line.mid(1, line.size() - 2);
    }
    return line;
}

/// 这个键是不是我们管的。
bool isManagedKey(const QString& key) {
    static const QStringList keys = {
        QStringLiteral("font_face"),
        QStringLiteral("font_point"),
        QStringLiteral("label_font_point"),
        QStringLiteral("comment_font_point"),
        QStringLiteral("horizontal"),
        QStringLiteral("inline_preedit"),
        QStringLiteral("preedit_type"),
        QStringLiteral("mark_text"),
        QStringLiteral("candidate_abbreviate_length"),
        QStringLiteral("display_tray_icon"),
    };
    return keys.contains(key);
}

/// 把一行 yaml 改写成 `  key: value`（保留原来的缩进层级）。
QString yamlLineWithValue(const QString& rawLine, const QString& key, const QString& value) {
    const int indent = rawLine.length() - rawLine.trimmed().length();
    return QString(indent, QLatin1Char(' ')) + key + QStringLiteral(": ") + value;
}

} // namespace

WeaselStyleManager& WeaselStyleManager::instance() {
    static WeaselStyleManager mgr;
    return mgr;
}

QString WeaselStyleManager::styleFilePath() {
    const QString dir = WeaselPaths::userDataDir();
    if (dir.isEmpty()) {
        return QString();
    }
    return dir + QStringLiteral("/weasel.yaml");
}

WeaselStyle WeaselStyleManager::parse(const QString& yamlText) {
    WeaselStyle style;
    bool inStyle = false;
    const QStringList lines = yamlText.split(QLatin1Char('\n'));
    for (const QString& raw : lines) {
        const QString line = raw.trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#'))) {
            continue;
        }
        const int indent = raw.length() - raw.trimmed().length();
        if (indent == 0) {
            inStyle = (line == QStringLiteral("style:"));
            continue;
        }
        if (!inStyle) {
            continue;
        }
        const int colon = line.indexOf(':');
        if (colon < 0) {
            continue;
        }
        const QString key = line.left(colon).trimmed();
        const QString value = yamlValue(line);
        if (!isManagedKey(key)) {
            continue;
        }
        if (key == QLatin1String("font_face")) {
            style.fontFace = value;
        } else if (key == QLatin1String("font_point")) {
            style.fontPoint = value.toInt();
        } else if (key == QLatin1String("label_font_point")) {
            style.labelFontPoint = value.toInt();
        } else if (key == QLatin1String("comment_font_point")) {
            style.commentFontPoint = value.toInt();
        } else if (key == QLatin1String("horizontal")) {
            style.horizontal = value.toLower() != QLatin1String("false");
        } else if (key == QLatin1String("inline_preedit")) {
            style.inlinePreedit = value.toLower() == QLatin1String("true");
        } else if (key == QLatin1String("preedit_type")) {
            style.preeditType = value;
        } else if (key == QLatin1String("mark_text")) {
            style.markText = value;
        } else if (key == QLatin1String("candidate_abbreviate_length")) {
            style.candidateAbbreviateLength = value.toInt();
        } else if (key == QLatin1String("display_tray_icon")) {
            style.displayTrayIcon = value.toLower() != QLatin1String("false");
        }
    }
    return style;
}

QString WeaselStyleManager::rewrite(const QString& yamlText, const WeaselStyle& style) {
    // key → 值的字符串形式
    QMap<QString, QString> values;
    values[QStringLiteral("font_face")] = QStringLiteral("\"%1\"").arg(style.fontFace);
    values[QStringLiteral("font_point")] = QString::number(style.fontPoint);
    values[QStringLiteral("label_font_point")] = QString::number(style.labelFontPoint);
    values[QStringLiteral("comment_font_point")] = QString::number(style.commentFontPoint);
    values[QStringLiteral("horizontal")] = style.horizontal ? QStringLiteral("true")
                                                           : QStringLiteral("false");
    values[QStringLiteral("inline_preedit")] = style.inlinePreedit ? QStringLiteral("true")
                                                                   : QStringLiteral("false");
    values[QStringLiteral("preedit_type")] = style.preeditType;
    values[QStringLiteral("mark_text")] = QStringLiteral("\"%1\"").arg(style.markText);
    values[QStringLiteral("candidate_abbreviate_length")] =
        QString::number(style.candidateAbbreviateLength);
    values[QStringLiteral("display_tray_icon")] = style.displayTrayIcon
                                                     ? QStringLiteral("true")
                                                     : QStringLiteral("false");

    const QStringList lines = yamlText.split(QLatin1Char('\n'));
    QStringList out;
    QSet<QString> written;
    bool inStyle = false;
    bool sawStyleBlock = false;

    for (const QString& raw : lines) {
        const QString line = raw.trimmed();
        const int indent = raw.length() - raw.trimmed().length();

        if (!line.isEmpty() && !line.startsWith(QLatin1Char('#')) && indent == 0) {
            inStyle = (line == QStringLiteral("style:"));
            if (inStyle) {
                sawStyleBlock = true;
                out << raw;
                continue;
            }
        }

        if (inStyle && !line.isEmpty() && !line.startsWith(QLatin1Char('#'))) {
            const int colon = line.indexOf(':');
            if (colon > 0) {
                const QString key = line.left(colon).trimmed();
                if (isManagedKey(key) && values.contains(key)) {
                    out << yamlLineWithValue(raw, key, values.value(key));
                    written.insert(key);
                    continue;
                }
            }
        }
        out << raw;
    }

    // style: 块里缺的键补在块末尾
    if (sawStyleBlock) {
        QStringList missing;
        for (auto it = values.constBegin(); it != values.constEnd(); ++it) {
            if (!written.contains(it.key())) {
                missing << QStringLiteral("  %1: %2").arg(it.key(), it.value());
            }
        }
        if (!missing.isEmpty()) {
            // 找到 style: 块的末尾（下一个顶层键之前）
            int insertAt = out.size();
            for (int i = 0; i < out.size(); ++i) {
                const QString l = out.at(i).trimmed();
                const int ind = out.at(i).length() - l.length();
                if (!l.isEmpty() && !l.startsWith('#') && ind == 0 && l != QStringLiteral("style:")) {
                    insertAt = i;
                    break;
                }
            }
            QStringList merged;
            for (int i = 0; i < out.size(); ++i) {
                merged << out.at(i);
                if (i + 1 == insertAt) {
                    merged += missing;
                }
            }
            if (insertAt >= out.size()) {
                merged += missing;
            }
            out = merged;
        }
    } else {
        // 整个文件都没有 style: 块 ⇒ 追加一个
        QStringList merged = out;
        while (!merged.isEmpty() && merged.last().trimmed().isEmpty()) {
            merged.removeLast();
        }
        merged << QStringLiteral("style:");
        for (auto it = values.constBegin(); it != values.constEnd(); ++it) {
            merged << QStringLiteral("  %1: %2").arg(it.key(), it.value());
        }
        merged << QString();
        out = merged;
    }

    return out.join(QLatin1Char('\n'));
}

WeaselStyle WeaselStyleManager::load(QString* error) const {
    const QString path = styleFilePath();
    if (path.isEmpty()) {
        if (error) *error = QStringLiteral("无法定位用户 Rime 目录");
        return WeaselStyle();
    }
    const QString text = readTextFile(path);
    if (text.isEmpty() && !QFileInfo::exists(path)) {
        // 没有这个文件就是「还没配过」，不是错误。
        return WeaselStyle();
    }
    return parse(text);
}

bool WeaselStyleManager::save(const WeaselStyle& style, QString* error) {
    const QString path = styleFilePath();
    if (path.isEmpty()) {
        if (error) *error = QStringLiteral("无法定位用户 Rime 目录");
        return false;
    }
    QString text = readTextFile(path);
    if (text.isEmpty() && !QFileInfo::exists(path)) {
        text = QStringLiteral("# Rime Weasel style\n# encoding: utf-8\n\n");
    }
    text = rewrite(text, style);
    return writeTextFile(path, text, error);
}
