#ifndef APP_ICON_H
#define APP_ICON_H

#include <QColor>
#include <QFont>
#include <QIcon>
#include <QPainter>
#include <QPixmap>
#include <QRectF>
#include <QString>

/// 主程序 / 托盘 / 设置窗口共用的应用图标。
///
/// 为什么要抽成公共的：托盘图标是运行时用 QPainter 画的（主题色方块 + 「中」字），
/// 而 exe 的**资源图标**是另一张图（黑底「业」字）。设置窗口之前没有 setWindowIcon，
/// 于是任务栏 / Alt+Tab 里显示的是「业」字，跟托盘里的「中」字不是一个样。
/// 现在统一走这里，窗口与托盘必然一致。
///
/// 多尺寸都画一遍：Windows 会按目标尺寸（任务栏 16/24、标题栏 16、Alt+Tab 32）
/// 从 QIcon 里挑最合适的那张，比只给一张 32px 再缩放清晰得多。
inline QIcon makeAppIcon(bool chinese, const QColor& accent) {
    QIcon icon;
    const int sizes[] = {16, 20, 24, 32, 48, 64};

    for (int s : sizes) {
        QPixmap pix(s, s);
        pix.fill(Qt::transparent);
        {
            QPainter p(&pix);
            p.setRenderHint(QPainter::Antialiasing);

            const QColor body = chinese ? accent : QColor(120, 120, 120);
            const double inset = s * 2.0 / 32.0;
            const double radius = s * 6.0 / 32.0;
            p.setBrush(body);
            p.setPen(Qt::NoPen);
            p.drawRoundedRect(QRectF(inset, inset, s - 2 * inset, s - 2 * inset),
                              radius, radius);

            p.setPen(Qt::white);
            QFont f = p.font();
            f.setPixelSize(qMax(8, s * 14 / 32));
            f.setBold(true);
            p.setFont(f);
            p.drawText(QRectF(0, 0, s, s), Qt::AlignCenter,
                       chinese ? QStringLiteral("中") : QStringLiteral("英"));
        }
        icon.addPixmap(pix);
    }
    return icon;
}

/// 主程序标识图标（固定中文态）—— 设置窗口、任务栏、Alt+Tab 用它。
inline QIcon makeAppIcon(const QColor& accent) {
    return makeAppIcon(true, accent);
}

#endif // APP_ICON_H
