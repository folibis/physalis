// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ruslan Muhlinin. See LICENSE.
#include "SelectionPropertyPane.h"

#include "../CanvasScene.h"
#include "../ShapeItem.h"

void SelectionPropertyPane::attach(QObject *target)
{
    for (const QPointer<ShapeItem> &shape : std::as_const(m_watched)) {
        if (shape)
            disconnect(shape.data(), nullptr, this, nullptr);
    }
    m_watched.clear();

    m_scene = qobject_cast<CanvasScene *>(target);
    if (m_scene) {
        // Live while the mouse is still down, which is how a single shape's
        // table behaves: it listens to the item rather than waiting to be told
        // an edit is finished.
        for (ShapeItem *shape : m_scene->selectedShapes()) {
            if (!shape)
                continue;
            m_watched.append(shape);
            connect(shape, &QGraphicsObject::xChanged, this, &PropertyPane::valueChanged);
            connect(shape, &QGraphicsObject::yChanged, this, &PropertyPane::valueChanged);
            connect(shape, &QGraphicsObject::rotationChanged, this, &PropertyPane::valueChanged);
            connect(shape, &ShapeItem::propertyChanged, this, &PropertyPane::valueChanged);
        }
    }
    emit rowsChanged();
}

std::vector<PropertyRow> SelectionPropertyPane::rows(EditorMode mode) const
{
    Q_UNUSED(mode);

    std::vector<PropertyRow> result;
    if (!m_scene || m_scene->selectedShapes().size() < 2)
        return result;

    CanvasScene *scene = m_scene;
    const QString geometry = QObject::tr("Geometry");

    result.push_back({QObject::tr("Shapes"), PropertyFieldType::String,
        [scene] { return QString::number(scene->selectedShapes().size()); },
        {}, 0.0, 0.0, {}, -1, 0.0, geometry});
    result.back().tooltip = QObject::tr("How many shapes this table is speaking for.");

    for (int axis = 0; axis < 2; ++axis) {
        const bool across = axis == 0;
        result.push_back({across ? QObject::tr("Left") : QObject::tr("Top"),
            PropertyFieldType::Numeric,
            [scene, across] {
                const QRectF box = scene->editSelectionBounds();
                return across ? box.left() : box.top();
            },
            [scene, across](const QVariant &v) {
                const QRectF box = scene->editSelectionBounds();
                const qreal by = v.toDouble() - (across ? box.left() : box.top());
                scene->moveSelectionBy(across ? QPointF(by, 0) : QPointF(0, by));
            },
            -100000.0, 100000.0, {}, -1, 0.0, geometry});
        result.back().tooltip = across
            ? QObject::tr("The left edge of everything picked. Moves all of it.")
            : QObject::tr("The top edge of everything picked. Moves all of it.");
    }

    for (int axis = 0; axis < 2; ++axis) {
        const bool across = axis == 0;
        result.push_back({across ? QObject::tr("Width") : QObject::tr("Height"),
            PropertyFieldType::Numeric,
            [scene, across] {
                const QRectF box = scene->editSelectionBounds();
                return across ? box.width() : box.height();
            },
            [scene, across](const QVariant &v) {
                const QRectF box = scene->editSelectionBounds();
                const qreal was = across ? box.width() : box.height();
                if (was <= 0.0001 || v.toDouble() <= 0.0001)
                    return;
                const qreal by = v.toDouble() / was;
                scene->scaleSelectionBy(across ? by : 1.0, across ? 1.0 : by);
            },
            0.1, 100000.0, {}, -1, 0.0, geometry});
        result.back().tooltip =
            QObject::tr("The box round everything picked. Scales all of it about the pivot,"
                        " so the arrangement keeps its shape.");
    }

    result.push_back({QObject::tr("Rotation"), PropertyFieldType::Numeric,
        [scene] { return CanvasScene::normalisedDegrees(scene->selectionRotation()); },
        [scene](const QVariant &v) { scene->turnSelectionTo(v.toDouble()); },
        -360.0, 360.0, {}, -1, 0.0, geometry});
    result.back().tooltip =
        QObject::tr("Turns the whole selection as one piece about its pivot, the way dragging"
                    " it round does.");

    return result;
}
