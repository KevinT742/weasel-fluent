#ifndef UI_GLYPH_ICON_H
#define UI_GLYPH_ICON_H

#include <QColor>
#include <QIcon>
#include <QPainter>
#include <QPixmap>

#include "design/Typography.h"

/// 用 Fluent 字形绘制多尺寸图标（16/20/24 @2x）。
/// 候选窗扩展菜单与托盘菜单共用。
inline QIcon createFluentMenuIcon(const QString& glyph, const QColor& color) {
    QIcon icon;
    for (int size : {16, 20, 24}) {
        QPixmap pm(size * 2, size * 2);
        pm.setDevicePixelRatio(2.0);
        pm.fill(Qt::transparent);
        {
            QPainter p(&pm);
            p.setRenderHint(QPainter::TextAntialiasing);
            p.setPen(color);
            Typography::Icons::paintGlyph(p, QRectF(0, 0, size, size), glyph, size,
                                          Qt::AlignCenter);
        }
        icon.addPixmap(pm, QIcon::Normal, QIcon::Off);
        icon.addPixmap(pm, QIcon::Active, QIcon::Off);
        icon.addPixmap(pm, QIcon::Selected, QIcon::Off);
        icon.addPixmap(pm, QIcon::Normal, QIcon::On);
        icon.addPixmap(pm, QIcon::Active, QIcon::On);
    }
    return icon;
}

#endif // UI_GLYPH_ICON_H
