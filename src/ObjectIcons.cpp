#include "ObjectIcons.h"

#include "Icons.h"
#include "ShapeItem.h"

#include <QApplication>
#include <QColor>
#include <QPainter>
#include <QPainterPath>
#include <QPainterPathStroker>
#include <QPixmap>
#include <QtMath>

namespace {

constexpr int kIconSize = 18;

// Icon-sized canvas at the screen's own pixel density. A painter on it already
// works in logical units, so callers draw in a plain 18-unit box and must not
// scale again.
QPixmap blankIcon()
{
    const int ratio = qMax(1, qCeil(qApp ? qApp->devicePixelRatio() : 1.0));
    QPixmap pixmap(kIconSize * ratio, kIconSize * ratio);
    pixmap.setDevicePixelRatio(ratio);
    pixmap.fill(Qt::transparent);
    return pixmap;
}

} // namespace

int ObjectIcons::size()
{
    return kIconSize;
}

QIcon ObjectIcons::forShape(ShapeItem *shape)
{
    const QString type = shape->typeName();
    if (type == QLatin1String("circle"))
        return Icons::circle();
    if (type == QLatin1String("polygon"))
        return Icons::polygon();
    return Icons::rectangle();
}

QIcon ObjectIcons::forBody(const QColor &color)
{
    // Half-pixel coordinates and a 1px pen: anything else spreads across two
    // rows under antialiasing and reads as a drop shadow at this size.
    QPixmap pixmap = blankIcon();
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);

    QColor fill = color;
    fill.setAlpha(140);
    painter.setBrush(fill);
    painter.setPen(QPen(color, 1.0));
    painter.drawRoundedRect(QRectF(1.5, 1.5, kIconSize - 3.0, kIconSize - 3.0), 4.0, 4.0);
    painter.end();

    return QIcon(pixmap);
}

QIcon ObjectIcons::forJoint(const QColor &color)
{
    QPixmap pixmap = blankIcon();
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const QPointF a(5.5, 5.5);
    const QPointF b(12.5, 12.5);
    constexpr qreal kRing = 3.0;

    QPainterPath bone;
    bone.addEllipse(a, kRing, kRing);
    QPainterPath shaft;
    shaft.moveTo(a);
    shaft.lineTo(b);
    QPainterPathStroker stroker;
    stroker.setWidth(2.4);
    stroker.setCapStyle(Qt::FlatCap);
    bone = bone.united(stroker.createStroke(shaft));
    QPainterPath ringB;
    ringB.addEllipse(b, kRing, kRing);
    bone = bone.united(ringB);

    painter.setBrush(color);
    painter.setPen(QPen(color.darker(220), 1.0));
    painter.drawPath(bone);
    painter.end();

    return QIcon(pixmap);
}
