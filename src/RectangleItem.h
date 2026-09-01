#pragma once

#include "ShapeItem.h"

class RectangleItem : public ShapeItem
{
public:
    RectangleItem();

    static QRectF createdRect() { return QRectF(-100, -50, 200, 100); }
    QRectF defaultRect() const override { return createdRect(); }
    QString typeName() const override { return QStringLiteral("rectangle"); }

    physics::Geometry physicsGeometry() const override;

    PropertyPane *makePropertyPane() const override;
};
