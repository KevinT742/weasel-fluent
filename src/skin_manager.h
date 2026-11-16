#ifndef SKIN_MANAGER_H
#define SKIN_MANAGER_H

#include <QObject>
#include <QString>
#include <QStringList>

/// 管理 Fluent 界面皮肤。
///
/// 皮肤沿用 Fluent-Qt 自带的 token 覆盖体系：一个皮肤就是一个 JSON 文件，
/// 内容是 Fluent 颜色 / 圆角 / 字体 token 的覆盖。生效方式是把该文件写成
/// Fluent 的用户主题文件（UserTheme::filePath()）后调用 UserTheme::apply()，
/// 因此它能真正改写 Fluent 自绘控件的配色 —— 普通 QSS 做不到这一点。
class SkinManager : public QObject {
    Q_OBJECT
public:
    static SkinManager& instance();

    /// 皮肤目录中可用的皮肤文件名（.json），不含 Fluent 的主覆盖文件。
    QStringList availableSkins() const;

    /// 导入皮肤 JSON 到皮肤目录，并立即应用。
    bool importSkin(const QString& srcPath, QString* error);

    /// 应用指定皮肤文件。
    bool applySkin(const QString& fileName, QString* error);

    /// 导出当前生效的 token 覆盖为一个独立皮肤文件。
    bool exportSkin(const QString& fileName, bool overwrite, QString* error);

    /// 清除当前皮肤覆盖，恢复 Fluent 默认外观。
    bool resetToDefault(QString* error);

    /// 当前生效的皮肤文件名，空串表示默认外观。
    QString currentSkin() const;

    /// 供测试使用：校验一份皮肤文本是否为合法的 Fluent token 覆盖。
    static bool isValidSkinSpec(const QString& jsonText);

private:
    QString m_currentSkin;
};

#endif // SKIN_MANAGER_H
