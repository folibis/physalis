// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ruslan Muhlinin. See LICENSE.
#pragma once

#include <QPointer>
#include <QVector>
#include <QWidget>

#include "SceneVariable.h"

class CanvasScene;
class QLabel;
class QLineEdit;
class QTableWidget;
class QToolButton;

// The Variables tab: what the scene carries besides its shapes -- a score, a
// count, a flag. One row each: name, type and the value it starts every run
// at. Rules read and write them through Rule::variables(), and any of them can
// be put in the log.
class VariablesPanel : public QWidget
{
    Q_OBJECT

public:
    // Parent-only, for Designer promotion; the scene arrives after.
    explicit VariablesPanel(QWidget *parent = nullptr);

    void setScene(CanvasScene *scene);

private:
    void buildUi();
    void rebuild();
    void addVariable();
    void removeSelected();
    // Put the selected variable in the log, or take it out again.
    void toggleLog();
    void fillRow(int row, const SceneVariable &variable);
    // The value editor depends on the type, so changing the type replaces it.
    void setValueEditor(int row, const SceneVariable &variable);
    void commit(int row, const SceneVariable &variable);
    void showMenu(int row, const QPoint &globalPos);
    // The name is a caption until it is double-clicked, the way a rule card's
    // title is: a box always in edit mode swallows the click that would select
    // the row and the right-click that would offer the menu.
    void beginRename(int row);
    void finishRename(int row, bool keep);
    bool eventFilter(QObject *watched, QEvent *event) override;
    // Everything the toolbar says depends on what is selected.
    void syncTools();
    int selectedRow() const;
    // Whatever a cell widget was tagged with, since a click arrives at the
    // widget rather than at the table underneath it.
    static int rowOf(const QObject *widget);
    // Tags a cell widget with its row and points its clicks and menu here.
    void adopt(QWidget *widget, int row);

    struct RowWidgets {
        QLabel *name = nullptr;
        QLineEdit *rename = nullptr;
    };

    QPointer<CanvasScene> m_scene;
    QTableWidget *m_table = nullptr;
    QToolButton *m_remove = nullptr;
    QToolButton *m_log = nullptr;
    QVector<RowWidgets> m_rows;
    // Rebuilding recreates every editor, so a handler must not write back
    // while it is happening.
    bool m_building = false;
};
