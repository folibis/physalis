// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ruslan Muhlinin. See LICENSE.
#pragma once

#include <QWidget>

class CanvasScene;
class SimulationController;

// The Plot tab: every watch marked for plotting, drawn against the run's own
// clock. The samples belong to the run -- SimulationController takes one per
// step -- so this widget keeps nothing and only draws what it is shown.
//
// Each series carries its own vertical scale. A spring force in newtons and a
// position in scene units share no axis worth sharing, and squeezing them onto
// one leaves whichever is smaller as a flat line along the bottom.
class PlotPanel : public QWidget
{
    Q_OBJECT

public:
    explicit PlotPanel(QWidget *parent = nullptr);

    void attach(CanvasScene *scene, SimulationController *simulation);
    // Called as the run goes; cheap, since it only asks for a repaint.
    void refresh();

protected:
    void paintEvent(QPaintEvent *event) override;
    // The wheel narrows or widens how much of the run is shown, anchored at
    // the newest sample -- which is the end you are watching while it runs.
    void wheelEvent(QWheelEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    // Dragging sideways walks back through the run.
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    void saveCsv();
    void saveImage();

    CanvasScene *m_scene = nullptr;
    SimulationController *m_simulation = nullptr;
    // Seconds of the run shown at once; zero means all of it. Five of them,
    // scrolling with the run, because a minute squeezed into a tab is a solid
    // block and nothing in it can be read.
    qreal m_shownSeconds = 5.0;
    // How far back from the newest sample the right edge sits, in seconds.
    qreal m_endOffset = 0.0;
    QPoint m_dragFrom;
    qreal m_dragOffsetFrom = 0.0;
    bool m_dragging = false;
    qreal m_secondsPerPixel = 0.0;
};
