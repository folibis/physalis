#pragma once

#include "ShapeItem.h"
#include "ShapePropertyPaneBase.h"

class RectanglePropertyPane : public ShapePropertyPaneBase
{
public:
    explicit RectanglePropertyPane(QObject *parent = nullptr) : ShapePropertyPaneBase(parent) {}

protected:
    // Width and height, plus the corner radius. Only rectangles carry one:
    // Box2D rounds a box without growing it, which is not true of an
    // arbitrary polygon.
    std::vector<PropertyRow> sizeRows(ShapeItem *item, const QString &section) const override
    {
        std::vector<PropertyRow> result = ShapePropertyPaneBase::sizeRows(item, section);

        PropertyRow radius;
        radius.label = QObject::tr("Corner Radius");
        radius.type = PropertyFieldType::Numeric;
        radius.getter = [item] { return item->cornerRadius(); };
        radius.setter = [item](const QVariant &v) { item->setCornerRadius(v.toDouble()); };
        radius.minValue = 0.0;
        radius.maxValue = 100000.0;
        radius.step = 1.0;
        radius.section = section;
        radius.defaultValue = 0.0;
        radius.key = QStringLiteral("shape.cornerRadius");
        radius.tooltip = QObject::tr(
            "Rounds the corners without making the shape any bigger. At half "
            "the shorter side it becomes a capsule, which slides over joins "
            "in the ground instead of catching on them.");
        result.push_back(std::move(radius));

        return result;
    }
};
