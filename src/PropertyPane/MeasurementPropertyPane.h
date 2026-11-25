// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ruslan Muhlinin. See LICENSE.
#pragma once

#include "PropertyPane.h"

#include <QPointer>

class CanvasScene;

// The ruler or the protractor laid on the canvas, typed in rather than placed:
// its points, how long it is from A to B, and its angle at A. Like the
// measurement itself it is not part of the scene, so nothing set here is an
// undo step.
class MeasurementPropertyPane : public PropertyPane
{
    Q_OBJECT

public:
    explicit MeasurementPropertyPane(QObject *parent = nullptr) : PropertyPane(parent) {}

    std::vector<PropertyRow> rows(EditorMode mode) const override;
    void attach(QObject *target) override;

private:
    void write(const QVector<QPointF> &points) const;

    QPointer<CanvasScene> m_scene;
};
