// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ruslan Muhlinin. See LICENSE.
#pragma once

#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QVector>

class CanvasScene;

class UndoStack : public QObject
{
    Q_OBJECT

public:
    explicit UndoStack(CanvasScene *scene, QObject *parent = nullptr);

    void setCapacity(int actions);
    int capacity() const { return m_capacity; }

    void reset();

    void push(const QString &label, const QString &mergeKey = QString());

    // Marks the state on screen as the one on disk. reset() does it too.
    void markClean();
    bool isClean() const { return m_index >= 0 && m_index == m_cleanIndex && !m_touched; }

    bool canUndo() const { return m_index > 0; }
    bool canRedo() const { return m_index >= 0 && m_index + 1 < m_states.size(); }

    QString undoLabel() const;
    QString redoLabel() const;

    void undo();
    void redo();

    bool isRestoring() const { return m_restoring; }

signals:
    void changed();

private:
    struct State {
        QJsonObject document;
        QString label;
        QString mergeKey;
    };

    void restore(int index);
    void trim();

    CanvasScene *m_scene = nullptr;
    QVector<State> m_states;
    // An edit the document does not show. A rule is left out of the saved
    // file until it is complete, so filling one in serializes to exactly what
    // was there before and records no state -- but the editor has changed and
    // the work is not on disk.
    bool m_touched = false;
    int m_index = -1;
    int m_cleanIndex = -1;
    int m_capacity = 50;
    bool m_restoring = false;
};
