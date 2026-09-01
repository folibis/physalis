#include "RectangleItem.h"
#include "Naming.h"
#include "PropertyPane/RectanglePropertyPane.h"

RectangleItem::RectangleItem()
{
    const QRectF r = createdRect();
    setRect(r);
    setOrigin(r.center()); // default origin: the rectangle's center
    setName(Naming::nextName(typeName()));
}

physics::Geometry RectangleItem::physicsGeometry() const
{
    physics::Geometry geometry;
    geometry.kind = physics::GeometryKind::Box;
    geometry.center = rect().center() - origin();
    geometry.halfExtents = QPointF(rect().width() / 2.0, rect().height() / 2.0);
    geometry.cornerRadius = cornerRadius();
    return geometry;
}

PropertyPane *RectangleItem::makePropertyPane() const
{
    return new RectanglePropertyPane();
}
