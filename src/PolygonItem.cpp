#include "PolygonItem.h"
#include "Naming.h"
#include "PropertyPane/PolygonPropertyPane.h"

#include <QPainter>
#include <QPainterPath>
#include <QLineF>

PolygonItem::PolygonItem(const QPolygonF &localPoints, bool closed)
    : m_points(localPoints)
    , m_closed(closed)
{
    const QRectF bounds = localPoints.boundingRect();
    setRect(bounds);
    setOrigin(bounds.center());

    setName(Naming::nextName(typeName()));

}

physics::Geometry PolygonItem::physicsGeometry() const
{
    physics::Geometry geometry;
    geometry.kind = m_closed ? physics::GeometryKind::Polygon : physics::GeometryKind::Chain;
    geometry.closed = m_closed;
    geometry.smoothChain = smoothChain();

    const QPointF pivot = origin();
    geometry.points.reserve(m_points.size());
    for (const QPointF &point : m_points)
        geometry.points.append(point - pivot);
    return geometry;
}

PropertyPane *PolygonItem::makePropertyPane() const
{
    return new PolygonPropertyPane();
}

void PolygonItem::paintShape(QPainter *painter, const QRectF &) const
{
    const QBrush fillBrush = painter->brush();
    const QPen strokePen = painter->pen();

    QPainterPath fillPath;
    fillPath.addPolygon(m_points);
    fillPath.closeSubpath();
    painter->setPen(Qt::NoPen);
    painter->setBrush(fillBrush);
    painter->drawPath(fillPath);

    painter->setPen(strokePen);
    painter->setBrush(Qt::NoBrush);
    if (m_closed)
        painter->drawPolygon(m_points);
    else
        painter->drawPolyline(m_points);
}

QPainterPath PolygonItem::localShapePath() const
{
    QPainterPath path;
    path.addPolygon(m_points);
    path.closeSubpath();
    return path;
}

void PolygonItem::resizeByHandle(HandleId id, const QPointF &localPos)
{
    if (id == HandleId::None)
        return;
    applyRect(adjustedRectForHandle(id, localPos));
}

void PolygonItem::applyRect(const QRectF &newRect)
{
    // The points are what the polygon really is; the rect only frames them, so
    // they are carried across proportionally.
    const QRectF oldRect = rect();
    if (oldRect.width() > 0.0 && oldRect.height() > 0.0) {
        for (QPointF &p : m_points) {
            const qreal nx = (p.x() - oldRect.left()) / oldRect.width();
            const qreal ny = (p.y() - oldRect.top()) / oldRect.height();
            p.setX(newRect.left() + nx * newRect.width());
            p.setY(newRect.top() + ny * newRect.height());
        }
    }

    setRect(newRect);
}

int PolygonItem::nodeAt(const QPointF &localPos) const
{
    for (int i = 0; i < m_points.size(); ++i) {
        if (QLineF(m_points[i], localPos).length() <= kNodeRadius + 3.0) // small grab margin
            return i;
    }
    return -1;
}

QPointF PolygonItem::nodePosition(int index) const
{
    if (index < 0 || index >= m_points.size())
        return QPointF();
    return m_points[index];
}

void PolygonItem::moveNode(int index, const QPointF &localPos)
{
    if (index < 0 || index >= m_points.size())
        return;
    m_points[index] = localPos;
    setRect(m_points.boundingRect());
}

void PolygonItem::deleteNode(int index)
{
    if (index < 0 || index >= m_points.size())
        return;
    if (m_points.size() <= kMinPoints)
        return;
    m_points.remove(index);
    setRect(m_points.boundingRect());
}

void PolygonItem::insertNodeBetween(int i, int j)
{
    if (i < 0 || i >= m_points.size() || j < 0 || j >= m_points.size() || i == j)
        return;

    int lo = qMin(i, j);
    int hi = qMax(i, j);
    const bool linearAdjacent = (hi == lo + 1);
    const bool wrapAdjacent = m_closed && lo == 0 && hi == m_points.size() - 1;
    if (!linearAdjacent && !wrapAdjacent)
        return;

    const QPointF midpoint = (m_points[i] + m_points[j]) / 2.0;
    const int insertAt = wrapAdjacent ? m_points.size() : lo + 1;
    m_points.insert(insertAt, midpoint);
    setRect(m_points.boundingRect());
}

void PolygonItem::closeShape()
{
    if (m_closed)
        return;
    m_closed = true;
    update();
}

void PolygonItem::setSelectedNodes(const QSet<int> &indices)
{
    m_selectedNodeIndices = indices;
    update();
}

void PolygonItem::paintEditNodes(QPainter *painter) const
{
    for (int i = 0; i < m_points.size(); ++i) {
        const bool selected = m_selectedNodeIndices.contains(i);
        if (selected) {
            painter->setPen(QPen(QColor(200, 60, 40), 1.5));
            painter->setBrush(QColor(230, 100, 80));
        } else {
            painter->setPen(QPen(QColor(30, 90, 140), 1));
            painter->setBrush(QColor(90, 170, 220));
        }
        const qreal r = selected ? kNodeRadius + 1.5 : kNodeRadius;
        painter->drawEllipse(m_points[i], r, r);
    }
}
