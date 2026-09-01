#include "UndoStack.h"

#include "CanvasScene.h"
#include "SceneSerializer.h"

UndoStack::UndoStack(CanvasScene *scene, QObject *parent)
    : QObject(parent)
    , m_scene(scene)
{
    reset();
}

void UndoStack::setCapacity(int actions)
{
    m_capacity = qBound(1, actions, 1000);
    trim();
    emit changed();
}

void UndoStack::reset()
{
    m_states.clear();
    m_index = -1;
    if (!m_scene)
        return;

    m_states.append({SceneSerializer::save(m_scene), QString(), QString()});
    m_index = 0;
    m_cleanIndex = 0;
    emit changed();
}

void UndoStack::markClean()
{
    m_cleanIndex = m_index;
    emit changed();
}

void UndoStack::push(const QString &label, const QString &mergeKey)
{
    if (!m_scene || m_restoring || m_index < 0)
        return;

    const QJsonObject document = SceneSerializer::save(m_scene);

    // Nothing actually changed. Recording it would put a do-nothing step in
    // the history that has to be undone twice to get past.
    if (document == m_states[m_index].document)
        return;

    // The user has taken a different branch; keeping the old future would let
    // redo jump to a state that never followed from this one.
    if (m_index + 1 < m_states.size()) {
        m_states.remove(m_index + 1, m_states.size() - m_index - 1);
        // The saved state was on the branch just discarded.
        if (m_cleanIndex > m_index)
            m_cleanIndex = -1;
    }

    if (!mergeKey.isEmpty() && m_index > 0 && m_states[m_index].mergeKey == mergeKey) {
        m_states[m_index].document = document;
        if (m_cleanIndex == m_index)
            m_cleanIndex = -1;
        emit changed();
        return;
    }

    m_states.append({document, label, mergeKey});
    ++m_index;
    trim();
    emit changed();
}

QString UndoStack::undoLabel() const
{
    return canUndo() ? m_states[m_index].label : QString();
}

QString UndoStack::redoLabel() const
{
    return canRedo() ? m_states[m_index + 1].label : QString();
}

void UndoStack::undo()
{
    if (canUndo())
        restore(m_index - 1);
}

void UndoStack::redo()
{
    if (canRedo())
        restore(m_index + 1);
}

void UndoStack::restore(int index)
{
    if (!m_scene || index < 0 || index >= m_states.size())
        return;

    m_restoring = true;
    QString error;
    if (SceneSerializer::load(m_scene, m_states[index].document, &error))
        m_index = index;
    m_restoring = false;

    emit changed();
}

void UndoStack::trim()
{
    // +1 because entry 0 is the starting state rather than an action.
    const int limit = m_capacity + 1;
    if (m_states.size() <= limit)
        return;

    const int excess = m_states.size() - limit;
    m_states.remove(0, excess);
    m_index = qMax(0, m_index - excess);
    m_cleanIndex = m_cleanIndex >= excess ? m_cleanIndex - excess : -1;
}
