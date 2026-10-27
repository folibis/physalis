// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ruslan Muhlinin. See LICENSE.
#pragma once

#include <QPointer>
#include <QWidget>

#include "SceneControl.h"

class CanvasScene;
class QVBoxLayout;

// The Controls tab: the things a scene is worked with while it runs -- a box
// to tick, a button, a slider. One card each, its fields on rows of their own
// the way a rule card lays its own out: its id, which is how a rule addresses
// it; its caption, which is only what it says; what kind it is, where it
// starts, and a slider's bounds, which are how it is declared rather than
// anything a run changes.
//
// What a control is set to is not shown here: that is run state and lives on
// the canvas, where the control itself is.
class ControlsPanel : public QWidget
{
    Q_OBJECT

public:
    explicit ControlsPanel(QWidget *parent = nullptr);

    void setScene(CanvasScene *scene);

private:
    void buildUi();
    void rebuild();
    void addControl();
    void removeControl(int row);
    QWidget *buildCard(int row, const SceneControl &control);
    void commit(int row, const SceneControl &control);

    QPointer<CanvasScene> m_scene;
    QVBoxLayout *m_cards = nullptr;
    bool m_building = false;
};
