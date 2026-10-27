// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ruslan Muhlinin. See LICENSE.
#pragma once

#include "PropertyPane.h"

#include <QPointer>
#include <QVector>

class CanvasScene;
class ShapeItem;

// What several shapes picked together have in common, which is not much: where
// the box round them sits, how big it is, and which way the arrangement faces.
// A corner radius, a circle's radius, a polygon's points -- those belong to one
// shape, and showing the lead shape's while several are picked said the table
// was speaking for all of them when it was speaking for one.
class SelectionPropertyPane : public PropertyPane
{
    Q_OBJECT

public:
    explicit SelectionPropertyPane(QObject *parent = nullptr) : PropertyPane(parent) {}

    std::vector<PropertyRow> rows(EditorMode mode) const override;
    void attach(QObject *target) override;

private:
    CanvasScene *m_scene = nullptr;
    // The shapes whose movements the table is showing. A single shape's pane
    // listens to its item the same way -- without this the rows only caught up
    // when the mouse was let go.
    QVector<QPointer<ShapeItem>> m_watched;
};
