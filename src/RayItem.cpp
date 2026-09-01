#include "RayItem.h"

#include "CanvasScene.h"

#include <QPainter>
#include <QStyleOptionGraphicsItem>
#include <QtMath>

namespace {
const QColor kLine(0x3A, 0x7B, 0xD5);
const QColor kSelected(0x1E, 0x6F, 0xD9);
const QColor kBeyond(0x3A, 0x7B, 0xD5, 0x50);
const QColor kHit(0xFE, 0x54, 0x0F);
const qreal kOriginRadius = 8.0;
}

RayItem::RayItem(QGraphicsItem *parent)
    : QGraphicsObject(parent)
{
    setZValue(50);
}

void RayItem::setName(const QString &name)
{
    if (m_name == name)
        return;
    m_name = name;
    update();
    emit propertyChanged();
}

void RayItem::setAngleDegrees(qreal degrees)
{
    if (qFuzzyCompare(m_angleDegrees, degrees))
        return;
    prepareGeometryChange();
    m_angleDegrees = degrees;
    update();
    emit propertyChanged();
}

void RayItem::setLength(qreal length)
{
    const qreal clamped = qMax(1.0, length);
    if (qFuzzyCompare(m_length, clamped))
        return;
    prepareGeometryChange();
    m_length = clamped;
    update();
    emit propertyChanged();
}

void RayItem::setMaskBits(quint64 bits)
{
    if (m_maskBits == bits)
        return;
    m_maskBits = bits;
    emit propertyChanged();
}

QPointF RayItem::reach() const
{
    const qreal radians = qDegreesToRadians(m_angleDegrees);
    return QPointF(qCos(radians) * m_length, qSin(radians) * m_length);
}

void RayItem::setReading(bool hit, const QPointF &point, qreal distance,
                         const QString &shapeName)
{
    m_hit = hit;
    m_hitPoint = point;
    m_distance = distance;
    m_hitName = shapeName;
    update();
}

void RayItem::clearReading()
{
    if (!m_hit)
        return;
    m_hit = false;
    m_hitPoint = QPointF();
    m_hitName.clear();
    m_distance = 0.0;
    update();
}

QRectF RayItem::boundingRect() const
{
    // The whole line either way, plus room for the arrowhead and the marker.
    return QRectF(QPointF(0, 0), reach()).normalized().adjusted(-18, -18, 18, 18);
}

QPainterPath RayItem::shape() const
{
    // A fat line, so the ray can be grabbed anywhere along it.
    QPainterPath path;
    path.moveTo(0, 0);
    path.lineTo(reach());
    QPainterPathStroker stroker;
    stroker.setWidth(10.0);
    QPainterPath grabbable = stroker.createStroke(path);
    grabbable.addEllipse(QPointF(0, 0), kOriginRadius + 3.0, kOriginRadius + 3.0);
    return grabbable;
}

void RayItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *option,
                    QWidget *widget)
{
    Q_UNUSED(option);
    Q_UNUSED(widget);

    // A ray is a measuring tool, not part of the scene: during a run it shows
    // only under Debug View, the same as the joints.
    const auto *canvas = qobject_cast<const CanvasScene *>(scene());
    if (canvas && canvas->simulationRunning() && !canvas->debugView())
        return;

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);

    const QPointF far = reach();
    const QPointF stop = m_hit ? m_hitPoint : far;
    const QColor line = m_selected ? kSelected : kLine;

    QPen pen(line);
    pen.setWidthF(m_selected ? 2.5 : 1.5);
    pen.setCosmetic(true);
    painter->setPen(pen);
    painter->drawLine(QPointF(0, 0), stop);

    // Past the hit the ray sees nothing, so that stretch is drawn faint --
    // it says how much reach is left over.
    if (m_hit) {
        QPen beyond(kBeyond);
        beyond.setWidthF(1.0);
        beyond.setStyle(Qt::DotLine);
        beyond.setCosmetic(true);
        painter->setPen(beyond);
        painter->drawLine(stop, far);
    }

    // An arrowhead at the far end says which way it looks.
    const qreal radians = qDegreesToRadians(m_angleDegrees);
    const qreal head = 9.0;
    const QPointF tip = far;
    QPolygonF arrow;
    arrow << tip
          << tip - QPointF(qCos(radians - 0.4) * head, qSin(radians - 0.4) * head)
          << tip - QPointF(qCos(radians + 0.4) * head, qSin(radians + 0.4) * head);
    painter->setPen(Qt::NoPen);
    painter->setBrush(line);
    painter->drawPolygon(arrow);

    // Where it starts, and where it struck.
    painter->drawEllipse(QPointF(0, 0), kOriginRadius, kOriginRadius);
    if (m_hit) {
        painter->setBrush(kHit);
        painter->drawEllipse(m_hitPoint, 4.0, 4.0);
    }

    // Selected exactly as a body or an explosion is, from the same settings.
    if (m_selected) {
        if (canvas && !canvas->simulationRunning()) {
            QPen selectionPen(canvas->physicsSelectionColor());
            selectionPen.setWidthF(canvas->physicsSelectionLineWidth());
            selectionPen.setStyle(canvas->physicsSelectionLineStyle());
            selectionPen.setCosmetic(true);
            painter->setPen(selectionPen);
            painter->setBrush(Qt::NoBrush);

            // A contour drawn round the ray, the way a shape gets one -- not a
            // second line laid along it, which only made the ray look thicker.
            QPainterPath line;
            line.moveTo(0, 0);
            line.lineTo(far);
            QPainterPathStroker stroker;
            stroker.setWidth(9.0);
            stroker.setCapStyle(Qt::RoundCap);

            QPainterPath outline = stroker.createStroke(line);
            outline.addEllipse(QPointF(0, 0), kOriginRadius + 3.0, kOriginRadius + 3.0);
            outline.addPolygon(arrow);
            // One contour round the lot, instead of three overlapping ones.
            painter->drawPath(outline.simplified());
        }
    }

    painter->restore();
}

void RayItem::setSelectedForPhysics(bool selected)
{
    if (m_selected == selected)
        return;
    m_selected = selected;
    update();
}
