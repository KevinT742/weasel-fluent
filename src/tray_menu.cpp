#include "tray_menu.h"

#include "config.h"
#include "rime_engine.h"
#include "ui_glyph_icon.h"

#include <QApplication>
#include <QIcon>
#include <QPainter>
#include <QPixmap>
#include <QProcess>

#include "components/menus_toolbars/Menu.h"

namespace {

QIcon makeTrayIcon(bool chinese, const QColor& accent) {
    QPixmap pix(32, 32);
    pix.fill(Qt::transparent);
    {
        QPainter p(&pix);
        p.setRenderHint(QPainter::Antialiasing);
        p.setBrush(chinese ? accent : QColor(120, 120, 120));
        p.setPen(Qt::NoPen);
        p.drawRoundedRect(QRectF(2, 2, 28, 28), 6, 6);
        p.setPen(Qt::white);
        QFont f = p.font();
        f.setPointSize(14);
        f.setBold(true);
        p.setFont(f);
        p.drawText(QRectF(0, 0, 32, 32), Qt::AlignCenter,
                   chinese ? QStringLiteral("中") : QStringLiteral("英"));
    }
    return QIcon(pix);
}

} // namespace

TrayMenu::TrayMenu(QSystemTrayIcon* icon, UiEngine* engine, QObject* parent)
    : QObject(parent), m_icon(icon), m_engine(engine) {
    if (!m_icon) {
        return;
    }
    rebuildMenu();
    rebuildIcon();
}

void TrayMenu::rebuildMenu() {
    if (!m_icon) {
        return;
    }
    const QColor accent = AppConfig::instance().effectiveAccentColor();

    auto* menu = new fluent::menus_toolbars::FluentMenu(QStringLiteral("Weasel Fluent"));

    // 1. 状态指示（只读）
    auto* statusItem = new fluent::menus_toolbars::FluentMenuItem(
        m_ascii ? QStringLiteral("当前：英文模式") : QStringLiteral("当前：中文模式"));
    statusItem->setEnabled(false);
    menu->addAction(statusItem);
    menu->addSeparator();

    // 2. 重新部署
    auto* deployItem = new fluent::menus_toolbars::FluentMenuItem(
        QStringLiteral("重新部署词库与配置"));
    deployItem->setIcon(createFluentMenuIcon(Typography::Icons::Sync, accent));
    connect(deployItem, &QAction::triggered, this, [this]() {
        if (m_engine) {
            m_engine->deploy();
        }
    });
    menu->addAction(deployItem);

    // 3. 设置中心
    auto* settingsItem = new fluent::menus_toolbars::FluentMenuItem(
        QStringLiteral("输入法设置"));
    settingsItem->setIcon(createFluentMenuIcon(Typography::Icons::Document, accent));
    connect(settingsItem, &QAction::triggered, this, []() {
        QProcess::startDetached(QCoreApplication::applicationFilePath(),
                                QStringList() << QStringLiteral("--settings"));
    });
    menu->addAction(settingsItem);

    menu->addSeparator();

    // 4. 退出
    auto* quitItem = new fluent::menus_toolbars::FluentMenuItem(QStringLiteral("退出"));
    connect(quitItem, &QAction::triggered, this, []() {
        QApplication::quit();
    });
    menu->addAction(quitItem);

    // 注意：中英切换**不在**托盘里 —— 由系统输入法切换与 TIP compartment 承担。
    m_icon->setContextMenu(menu);
}

void TrayMenu::setAsciiMode(bool ascii) {
    if (m_ascii == ascii) {
        return;
    }
    m_ascii = ascii;
    rebuildMenu();
    rebuildIcon();
}

void TrayMenu::setVisible(bool visible) {
    if (!m_icon) {
        return;
    }
    if (visible) {
        m_icon->show();
    } else {
        m_icon->hide();
    }
}

void TrayMenu::rebuildIcon() {
    if (!m_icon) {
        return;
    }
    auto& cfg = AppConfig::instance();
    m_icon->setIcon(makeTrayIcon(!m_ascii, cfg.effectiveAccentColor()));
    m_icon->setToolTip(m_ascii ? QStringLiteral("Weasel Fluent 输入法 - 英文模式")
                               : QStringLiteral("Weasel Fluent 输入法 - 中文模式"));
}
