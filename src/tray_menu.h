#ifndef TRAY_MENU_H
#define TRAY_MENU_H

#include <QObject>
#include <QSystemTrayIcon>

class RimeEngine;

/// Fluent 风格的托盘菜单。
///
/// 职责刻意收缩为「状态指示 + 重新部署 + 设置中心 + 退出」：
/// 输入法的中英切换不再由托盘承担 —— 选择输入法交给系统语言切换
/// （Win+Space），中英状态由 TIP 通过 KEYBOARD_OPENCLOSE compartment
/// 同步给 Windows 语言栏。
class TrayMenu : public QObject {
    Q_OBJECT
public:
    TrayMenu(QSystemTrayIcon* icon, RimeEngine* engine, QObject* parent = nullptr);

    /// 同步中英状态到图标与菜单文字。
    void setAsciiMode(bool ascii);

    /// 显示或隐藏托盘图标。
    void setVisible(bool visible);

private:
    void rebuildIcon();
    void rebuildMenu();

    QSystemTrayIcon* m_icon = nullptr;
    RimeEngine* m_engine = nullptr;
    bool m_ascii = false;
};

#endif // TRAY_MENU_H
