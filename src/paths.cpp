#include "paths.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>

namespace {

/// 从 HKCU\Software\Microsoft\Windows\CurrentVersion\Run 的 WeaselFluent 项
/// 解析服务可执行文件所在目录。安装器会写入该项，因此它是最可靠的
/// 「本程序装在哪」线索。
QString installDirFromRunKey() {
    QSettings s(QSettings::NativeFormat, QSettings::UserScope,
                QStringLiteral("Microsoft"), QStringLiteral("Windows"));
    s.beginGroup(QStringLiteral("CurrentVersion/Run"));
    const QString exe = s.value(QStringLiteral("WeaselFluent")).toString();
    s.endGroup();
    if (exe.isEmpty()) {
        return QString();
    }
    QString clean = exe.trimmed();
    if (clean.startsWith(QLatin1Char('"')) && clean.endsWith(QLatin1Char('"'))) {
        clean = clean.mid(1, clean.size() - 2);
    }
    if (clean.isEmpty()) {
        return QString();
    }
    return QFileInfo(clean).absolutePath();
}

QString firstExisting(const QStringList& candidates) {
    for (const QString& c : candidates) {
        if (!c.isEmpty() && QFileInfo::exists(c)) {
            return QFileInfo(c).absoluteFilePath();
        }
    }
    return QString();
}

} // namespace

namespace WeaselPaths {

QString installDir() {
    const QString appDir = QCoreApplication::applicationDirPath();
    const QString fromRun = installDirFromRunKey();
    if (!fromRun.isEmpty()) {
        return fromRun;
    }
    return QDir(appDir).absolutePath();
}

QString rimeDllPath() {
    const QString appDir = QCoreApplication::applicationDirPath();
    const QString fromRun = installDirFromRunKey();
    return firstExisting({
        appDir + QStringLiteral("/rime.dll"),
        fromRun + QStringLiteral("/rime.dll"),
    });
}

QString sharedDataDir() {
    const QString appDir = QCoreApplication::applicationDirPath();
    const QString fromRun = installDirFromRunKey();
    return firstExisting({
        appDir + QStringLiteral("/data"),
        fromRun + QStringLiteral("/data"),
    });
}

QString userDataDir() {
    // Rime 的约定是 %APPDATA%\Rime（Roaming）。注意 Qt 的 GenericDataLocation
    // 在 Windows 上指向 **Local**，而 AppDataLocation 会追加应用名 —— 两者都
    // 不能直接用。这里直接读 %APPDATA%，与 Weasel/librime 的行为完全一致。
    QString base = qEnvironmentVariable("APPDATA");
    if (base.isEmpty()) {
        const QString appData = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        base = appData.isEmpty() ? QString() : QFileInfo(appData).absolutePath();
    }
    if (base.isEmpty()) {
        base = QDir::homePath() + QStringLiteral("/.local/share");
    }
    const QString dir = QDir(base).absoluteFilePath(QStringLiteral("Rime"));
    QDir().mkpath(dir);
    return dir;
}

QString configDir() {
    const QString appDir = QCoreApplication::applicationDirPath();
    const QString local = appDir + QStringLiteral("/config.json");
    if (QFileInfo::exists(local)) {
        return QFileInfo(local).absolutePath();
    }
    QString dataDir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    if (dataDir.isEmpty()) {
        dataDir = appDir;
    }
    QDir().mkpath(dataDir);
    return QDir(dataDir).absolutePath();
}

QString skinDir() {
    const QString dir = QDir(configDir()).absoluteFilePath(QStringLiteral("skins"));
    QDir().mkpath(dir);
    return dir;
}

} // namespace WeaselPaths
