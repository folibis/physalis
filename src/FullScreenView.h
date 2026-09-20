// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ruslan Muhlinin. See LICENSE.
#pragma once

#include <QGraphicsView>

class CanvasScene;
class QLabel;

// The run on its own, filling the screen. It is a second view onto the same
// scene rather than the editor's own view moved somewhere else: nothing is
// reparented, the editor stays as it was underneath, and closing this window
// leaves no trace.
//
// It shows and does not edit -- dragging moves the view, never a body. The
// wheel zooms about the pointer and 0 fits the field again; the keys are the
// transport: space holds the run and lets it go again, the right arrow advances
// one frame, escape ends the run and comes back to the editor.
class FullScreenView : public QGraphicsView
{
    Q_OBJECT

public:
    explicit FullScreenView(CanvasScene *scene, QWidget *parent = nullptr);

signals:
    void holdRequested();
    void stepRequested();
    void closeRequested();

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void showEvent(QShowEvent *event) override;

private:
    // The whole field, as large as it will go without distorting it.
    void fitField();
    void placeHint();

    QLabel *m_hint = nullptr;
    // Once the view has been zoomed or dragged it is the viewer's, and a
    // resize no longer snaps it back to the whole field.
    bool m_moved = false;
};
