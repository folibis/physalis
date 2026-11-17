// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ruslan Muhlinin. See LICENSE.
#pragma once

#include <QWidget>

#include "Units.h"

class CanvasScene;
class QGraphicsView;

class RulerWidget : public QWidget
{
    Q_OBJECT

public:
    enum class Orientation { Horizontal, Vertical };

    static constexpr int kThickness = 26;

    // Parent-only, for Designer promotion; the rest is set after setupUi().
    explicit RulerWidget(QWidget *parent = nullptr);
    RulerWidget(Orientation orientation, QGraphicsView *view, QWidget *parent = nullptr);

    void setOrientation(Orientation orientation);
    void setView(QGraphicsView *view);

    // What the numbers along it are written in. Scene units by default, which
    // is what the ruler has always shown.
    Units::Unit unit() const { return m_unit; }
    void setUnit(Units::Unit unit);

    QSize sizeHint() const override;

signals:
    // Picked from the ruler's own menu. The window owns the setting -- both
    // rulers show the same unit and it outlives the window -- so the ruler
    // asks rather than deciding.
    void unitPicked(Units::Unit unit);

protected:
    void paintEvent(QPaintEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;

private:
    // The scene behind the view, for its pixelsPerMeter: scene units are only
    // a real distance through the scale, and nothing else knows it.
    CanvasScene *scene() const;
    // Device pixels per scene unit, which is what decides how far apart the
    // ticks can afford to be.
    qreal viewScale() const;

    Orientation m_orientation = Orientation::Horizontal;
    QGraphicsView *m_view = nullptr;
    Units::Unit m_unit = Units::Unit::Scene;
};
