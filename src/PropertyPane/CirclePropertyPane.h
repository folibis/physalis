// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ruslan Muhlinin. See LICENSE.
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
                // Half of the smallest side a rectangle may have, for the
                // same reason: below that a solver cannot keep it apart from
                // what it touches.
                const qreal radius = qMax(0.5, v.toDouble());
                const QPointF centre = item->rect().center();
                item->setRect(QRectF(centre.x() - radius, centre.y() - radius,
                                     radius * 2.0, radius * 2.0));
            },
            0.5, 100000.0, {}, -1, 0.0, section});
        result.back().tooltip = QObject::tr("How big the circle is, measured from its centre."
                                            " It grows and shrinks about that centre.");
        return result;
    }

    QVector<QPair<QString, QVariant>> sizeDefaults(const QRectF &created) const override
    {
        return {{QObject::tr("Radius"), created.width() / 2.0}};
    }
};
