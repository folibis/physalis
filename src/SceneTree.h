#pragma once

#include <QPointer>
#include <QWidget>

class CanvasScene;
class ShapeItem;
class PhysicsBody;
class Joint;

class QTreeWidget;
class QTreeWidgetItem;

class SceneTree : public QWidget
{
    Q_OBJECT

public:
    // Parent-only, for Designer promotion; the scene arrives after.
    explicit SceneTree(QWidget *parent = nullptr);
    SceneTree(CanvasScene *scene, QWidget *parent = nullptr);

    void setScene(CanvasScene *scene);

    bool isDrivingSelection() const { return m_selecting; }

signals:
    void propertiesRequested();

private:
    enum class NodeKind { Group, Shape, Body, BodyRef, Joint, Explosion, Ray };

    void rebuild();
    // Queued, not immediate. Loading a scene fires a structural signal per
    // object, and a scene being destroyed fires them as it comes apart --
    // rebuilding then would read bodies that are already freed. A queued call
    // is dropped once the event loop is gone.
    void scheduleRebuild();
    void refreshLabels();
    void syncSelection();
    void updateBoldRows();
    void *selectedObject() const;
    void onItemActivated(QTreeWidgetItem *item);
    void buildUi();
    void connectScene();

    QTreeWidgetItem *makeItem(QTreeWidgetItem *parent, NodeKind kind, void *object,
                              const QIcon &icon, const QString &label, const QString &tooltip);
    QTreeWidgetItem *itemFor(void *object) const;

    static NodeKind kindOf(const QTreeWidgetItem *item);
    static void *objectOf(const QTreeWidgetItem *item);

    QPointer<CanvasScene> m_scene;
    QTreeWidget *m_tree = nullptr;

    bool m_selecting = false;
    bool m_rebuildPending = false;
    void *m_pendingSelection = nullptr;
};
