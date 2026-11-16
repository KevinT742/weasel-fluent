#include "skin_manager.h"

#include "paths.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

#include "components/foundation/UserTheme.h"

namespace {

/// Fluent 的用户主题覆盖文件（fluent.json）。
QString fluentUserThemeFile() {
    return fluent::UserTheme::filePath();
}

/// Fluent 的皮肤目录。
QString fluentThemeDir() {
    return fluent::UserTheme::directory();
}

} // namespace

SkinManager& SkinManager::instance() {
    static SkinManager s_inst;
    return s_inst;
}

QString SkinManager::currentSkin() const {
    return m_currentSkin;
}

QStringList SkinManager::availableSkins() const {
    QStringList out;
    const QDir dir(fluentThemeDir());
    if (!dir.exists()) {
        return out;
    }
    const QStringList files = dir.entryList(QStringList() << QStringLiteral("*.json"),
                                            QDir::Files, QDir::Name);
    const QString main = QFileInfo(fluentUserThemeFile()).fileName();
    for (const QString& f : files) {
        if (f == main) {
            continue;   // 主覆盖文件本身不是可选皮肤
        }
        out << f;
    }
    return out;
}

bool SkinManager::isValidSkinSpec(const QString& jsonText) {
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(jsonText.toUtf8(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        return false;
    }
    // Fluent 的 token 校验器：非法字段会整体拒绝。
    return fluent::UserTheme::applyOverrides(doc.object());
}

bool SkinManager::importSkin(const QString& srcPath, QString* error) {
    const QFileInfo src(srcPath);
    if (!src.exists() || !src.isFile()) {
        if (error) *error = QStringLiteral("皮肤文件不存在");
        return false;
    }

    QFile in(srcPath);
    if (!in.open(QIODevice::ReadOnly)) {
        if (error) *error = QStringLiteral("无法读取皮肤文件");
        return false;
    }
    const QByteArray raw = in.readAll();
    in.close();

    QJsonParseError perr{};
    const QJsonDocument doc = QJsonDocument::fromJson(raw, &perr);
    if (perr.error != QJsonParseError::NoError || !doc.isObject()) {
        if (error) *error = QStringLiteral("皮肤文件不是合法的 JSON 对象：%1").arg(perr.errorString());
        return false;
    }
    if (!fluent::UserTheme::applyOverrides(doc.object())) {
        if (error) *error = QStringLiteral("皮肤 token 校验未通过（字段非法或与当前主题冲突）");
        return false;
    }

    QDir().mkpath(fluentThemeDir());
    const QString target = fluentThemeDir() + QStringLiteral("/") + src.fileName();
    if (!QFile::copy(srcPath, target)) {
        if (error) *error = QStringLiteral("复制皮肤文件失败");
        return false;
    }
    return applySkin(src.fileName(), error);
}

bool SkinManager::applySkin(const QString& fileName, QString* error) {
    if (fileName.isEmpty() || fileName == QStringLiteral("默认")) {
        return resetToDefault(error);
    }

    const QString path = fluentThemeDir() + QStringLiteral("/") + fileName;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error) *error = QStringLiteral("找不到皮肤文件：%1").arg(fileName);
        return false;
    }
    const QByteArray raw = f.readAll();
    f.close();

    QJsonParseError perr{};
    const QJsonDocument doc = QJsonDocument::fromJson(raw, &perr);
    if (perr.error != QJsonParseError::NoError || !doc.isObject()) {
        if (error) *error = QStringLiteral("皮肤 JSON 解析失败：%1").arg(perr.errorString());
        return false;
    }
    if (!fluent::UserTheme::applyOverrides(doc.object())) {
        if (error) *error = QStringLiteral("皮肤 token 校验未通过");
        return false;
    }

    // 写入 Fluent 的用户主题文件，使其在下次启动时仍然生效。
    QDir().mkpath(QFileInfo(fluentUserThemeFile()).absolutePath());
    {
        QFile out(fluentUserThemeFile());
        if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            if (error) *error = QStringLiteral("无法写入 Fluent 用户主题文件");
            return false;
        }
        out.write(raw);
        out.close();
    }

    // 重新应用，确保当前进程内的所有 Fluent 控件立刻刷新。
    fluent::UserTheme::apply();
    m_currentSkin = fileName;
    return true;
}

bool SkinManager::exportSkin(const QString& fileName, bool overwrite, QString* error) {
    QDir().mkpath(fluentThemeDir());
    const QString target = fluentThemeDir() + QStringLiteral("/") + fileName;
    if (!overwrite && QFileInfo::exists(target)) {
        if (error) *error = QStringLiteral("同名皮肤已存在：%1").arg(fileName);
        return false;
    }
    if (!fluent::UserTheme::exportTemplate(true)) {
        if (error) *error = QStringLiteral("导出主题模板失败");
        return false;
    }
    // exportTemplate 写入的是 Fluent 主覆盖文件，再复制一份为独立皮肤。
    if (!QFile::copy(fluentUserThemeFile(), target)) {
        if (error) *error = QStringLiteral("复制主题模板失败");
        return false;
    }
    return true;
}

bool SkinManager::resetToDefault(QString* error) {
    const QString userFile = fluentUserThemeFile();
    if (QFileInfo::exists(userFile) && !QFile::remove(userFile)) {
        if (error) *error = QStringLiteral("无法移除用户主题覆盖文件");
        return false;
    }
    fluent::UserTheme::apply();
    m_currentSkin.clear();
    return true;
}
