#pragma once

#include "ShapePropertyPaneBase.h"

class CirclePropertyPane : public ShapePropertyPaneBase
{
public:
    explicit CirclePropertyPane(QObject *parent = nullptr) : ShapePropertyPaneBase(parent) {}

protected:
    std::vector<PropertyRow> sizeRows(ShapeItem *item, const QString &section) const override
    {
        std::vector<PropertyRow> result;
        result.push_back({QObject::tr("Radius"), PropertyFieldType::Numeric,
            [item] { return item->rect().width() / 2.0; },
            [item](const QVariant &v) {
                const qreal radius = qMax(5.0, v.toDouble());
                const QPointF centre = item->rect().center();
                item->setRect(QRectF(centre.x() - radius, centre.y() - radius,
                                     radius * 2.0, radius * 2.0));
            },
            5.0, 100000.0, {}, -1, 0.0, section});
        return result;
    }

    QVector<QPair<QString, QVariant>> sizeDefaults(const QRectF &created) const override
    {
        return {{QObject::tr("Radius"), created.width() / 2.0}};
    }
};
