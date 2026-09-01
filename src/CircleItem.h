#pragma once

#include "ShapeItem.h"

class CircleItem : public ShapeItem
{
public:
    CircleItem();

    static QRectF createdRect() { return QRectF(-75, -75, 150, 150); }
    QRectF defaultRect() const override { return createdRect(); }
    QString typeName() const override { return QStringLiteral("circle"); }

    physics::Geometry physicsGeometry() const override;

    PropertyPane *makePropertyPane() const override;

public:
    void resizeByHandle(HandleId id, const QPointF &localPos) override;

protected:
    void paintShape(QPainter *painter, const QRectF &rect) const override;
    QPainterPath localShapePath() const override;
    std::vector<HandleId> activeHandles() const override;
};
