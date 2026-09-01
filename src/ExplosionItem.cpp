#include "ExplosionItem.h"

#include "CanvasScene.h"

#include <QPainter>
#include <QtMath>
#include <QStyleOptionGraphicsItem>

namespace {
// Its own colour, so a explosion is never mistaken for something solid.
const QColor kFill(0xFE, 0x54, 0x0F, 0x40);
const QColor kLine(0xFE, 0x54, 0x0F);
const QColor kSelected(0x1E, 0x6F, 0xD9);
}

ExplosionItem::ExplosionItem(QGraphicsItem *parent)
    : QGraphicsObject(parent)
{
    setZValue(50); // above shapes: it is a handle, not scenery
}

void ExplosionItem::setName(const QString &name)
{
    if (m_name == name)
        return;
    m_name = name;
    update();
    emit propertyChanged();
}

void ExplosionItem::setParam(const QString &key, const QVariant &value)
{
    if (m_params.value(key) == value)
        return;
    m_params.insert(key, value);
    emit propertyChanged();
}

void ExplosionItem::setSelectedForPhysics(bool selected)
{
    if (m_selected == selected)
        return;
    m_selected = selected;
    update();
}

QRectF ExplosionItem::boundingRect() const
{
    const qreal r = radius() + 2.0;
    return QRectF(-r, -r, r * 2.0, r * 2.0);
}

QPainterPath ExplosionItem::shape() const
{
    QPainterPath path;
    path.addEllipse(QPointF(0, 0), radius(), radius());
    return path;
}

void ExplosionItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *option,
                       QWidget *widget)
{
    Q_UNUSED(option);
    Q_UNUSED(widget);

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);

    const qreal r = radius();
    QPen pen(kLine);
    pen.setWidthF(1.5);
    pen.setCosmetic(true);
    painter->setPen(pen);
    painter->setBrush(kFill);

    // A ragged starburst: alternating long and short spikes around the point.
    QPolygonF burst;
    const int spikes = 10;
    for (int i = 0; i < spikes * 2; ++i) {
        const qreal angle = i * M_PI / spikes;
        const qreal reach = (i % 2 == 0) ? r : r * 0.55;
        burst << QPointF(qCos(angle) * reach, qSin(angle) * reach);
    }
    painter->drawPolygon(burst);

    // The exact coordinate the blast happens at.
    painter->setBrush(kLine);
    painter->setPen(Qt::NoPen);
    painter->drawEllipse(QPointF(0, 0), 2.0, 2.0);

    // Selected exactly as a body is -- same colour, width and dash, from the
    // same settings, so one cannot drift from the other.
    if (m_selected) {
        const auto *canvas = qobject_cast<const CanvasScene *>(scene());
        if (canvas && !canvas->simulationRunning()) {
            QPen selectionPen(canvas->physicsSelectionColor());
            selectionPen.setWidthF(canvas->physicsSelectionLineWidth());
            selectionPen.setStyle(canvas->physicsSelectionLineStyle());
            selectionPen.setCosmetic(true);
            painter->setPen(selectionPen);
            painter->setBrush(Qt::NoBrush);
            const qreal halo = r + 4.0;
            painter->drawEllipse(QPointF(0, 0), halo, halo);
        }
    }

    painter->restore();
}
