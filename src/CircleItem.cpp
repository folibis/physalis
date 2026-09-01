#include "CircleItem.h"
#include "Naming.h"
#include "PropertyPane/CirclePropertyPane.h"

#include <QPainter>
#include <QPainterPath>
#include <QtMath>

namespace {
constexpr qreal kMinRadius = 5.0;
}

CircleItem::CircleItem()
{
    const QRectF r = createdRect();
    setRect(r);
    setOrigin(r.center());
    setName(Naming::nextName(typeName()));
}

physics::Geometry CircleItem::physicsGeometry() const
{
    physics::Geometry geometry;
    geometry.kind = physics::GeometryKind::Circle;
    geometry.center = rect().center() - origin();
    geometry.radius = qMin(rect().width(), rect().height()) / 2.0;
    return geometry;
}

PropertyPane *CircleItem::makePropertyPane() const
{
    return new CirclePropertyPane();
}

void CircleItem::paintShape(QPainter *painter, const QRectF &rect) const
{
    painter->drawEllipse(rect);
}

QPainterPath CircleItem::localShapePath() const
{
    QPainterPath path;
    path.addEllipse(rect());
    return path;
}

std::vector<HandleId> CircleItem::activeHandles() const
{
    return { HandleId::Top, HandleId::Right, HandleId::Bottom, HandleId::Left };
}

void CircleItem::resizeByHandle(HandleId id, const QPointF &localPos)
{
    const QPointF center = rect().center();

    qreal newRadius;
    switch (id) {
    case HandleId::Top:
    case HandleId::Bottom:
        newRadius = qAbs(localPos.y() - center.y());
        break;
    case HandleId::Left:
    case HandleId::Right:
        newRadius = qAbs(localPos.x() - center.x());
        break;
    default:
        return;
    }
    newRadius = qMax(newRadius, kMinRadius);

    setRect(QRectF(center.x() - newRadius, center.y() - newRadius, newRadius * 2.0, newRadius * 2.0));
}
