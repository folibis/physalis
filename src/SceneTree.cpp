#include "SceneTree.h"

#include "CanvasScene.h"
#include "ShapeItem.h"
#include "PhysicsBody.h"
#include "Joint.h"
#include "ExplosionItem.h"
#include "RayItem.h"
#include "Icons.h"
#include "ObjectIcons.h"

#include <QHeaderView>
#include <QPainter>
#include <QApplication>
#include <QPainterPath>
#include <QPainterPathStroker>
#include <QPixmap>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QtMath>

namespace {

constexpr int kKindRole = Qt::UserRole;
constexpr int kObjectRole = Qt::UserRole + 1;

QString bodyTypeName(const PhysicsBody *body)
{
    switch (body->props().type) {
    case physics::BodyType::Static:    return QObject::tr("static");
    case physics::BodyType::Kinematic: return QObject::tr("kinematic");
    case physics::BodyType::Dynamic:   return QObject::tr("dynamic");
    }
    return {};
}

} // namespace

SceneTree::SceneTree(QWidget *parent)
    : QWidget(parent)
{
    buildUi();
}

SceneTree::SceneTree(CanvasScene *scene, QWidget *parent)
    : QWidget(parent)
{
    buildUi();
    setScene(scene);
}

void SceneTree::setScene(CanvasScene *scene)
{
    if (m_scene == scene)
        return;
    m_scene = scene;
    connectScene();
    rebuild();
}

void SceneTree::buildUi()
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    m_tree = new QTreeWidget(this);
    m_tree->setHeaderHidden(true);
    m_tree->setColumnCount(1);
    m_tree->setIconSize(QSize(ObjectIcons::size(), ObjectIcons::size()));
    m_tree->setUniformRowHeights(true);
    m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tree->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_tree->setExpandsOnDoubleClick(false);
    layout->addWidget(m_tree);

    connect(m_tree, &QTreeWidget::itemClicked, this,
            [this](QTreeWidgetItem *item, int) { onItemActivated(item); });
    connect(m_tree, &QTreeWidget::currentItemChanged, this,
            [this](QTreeWidgetItem *item, QTreeWidgetItem *) { onItemActivated(item); });
    connect(m_tree, &QTreeWidget::itemDoubleClicked, this,
            [this](QTreeWidgetItem *item, int) {
                onItemActivated(item);
                if (item && kindOf(item) != NodeKind::Group)
                    emit propertiesRequested();
            });

}

void SceneTree::connectScene()
{
    if (m_scene) {
        connect(m_scene, &CanvasScene::shapesChanged, this, &SceneTree::scheduleRebuild);
        connect(m_scene, &CanvasScene::explosionsChanged, this, &SceneTree::scheduleRebuild);
        connect(m_scene, &CanvasScene::raysChanged, this, &SceneTree::scheduleRebuild);
        connect(m_scene, &CanvasScene::bodiesChanged, this, &SceneTree::scheduleRebuild);
        connect(m_scene, &CanvasScene::jointsChanged, this, &SceneTree::scheduleRebuild);

        connect(m_scene, &CanvasScene::simulationRunningChanged, this,
                [this](bool running) { m_tree->setEnabled(!running); });
        m_tree->setEnabled(m_scene->selectionAllowed());

        connect(m_scene, &CanvasScene::activeItemChanged, this, &SceneTree::syncSelection);
        connect(m_scene, &CanvasScene::physicsSelectionChanged, this, &SceneTree::syncSelection);
        connect(m_scene, &CanvasScene::selectedJointChanged, this, &SceneTree::syncSelection);
    }
}

void SceneTree::scheduleRebuild()
{
    if (m_rebuildPending)
        return;
    m_rebuildPending = true;

    // The rows are emptied now, not when the rebuild runs. Something has just
    // been destroyed, and every row holds a raw pointer to an object that may
    // be one of the casualties -- refreshLabels() would read a freed joint
    // before the queued rebuild ever got the chance to replace it.
    m_pendingSelection = m_tree->currentItem() ? objectOf(m_tree->currentItem()) : nullptr;
    {
        const QSignalBlocker blocker(m_tree);
        m_tree->clear();
    }

    QMetaObject::invokeMethod(this, [this] {
        m_rebuildPending = false;
        rebuild();
    }, Qt::QueuedConnection);
}

void SceneTree::rebuild()
{
    if (!m_scene)
        return;

    // Emptied by scheduleRebuild(), so what was selected is remembered there.
    void *current = m_pendingSelection ? m_pendingSelection
                  : (m_tree->currentItem() ? objectOf(m_tree->currentItem()) : nullptr);
    m_pendingSelection = nullptr;

    const QSignalBlocker blocker(m_tree);
    m_tree->clear();

    const QVector<ShapeItem *> shapes = m_scene->shapes();
    const QVector<PhysicsBody *> bodies = m_scene->bodies();
    const QVector<Joint *> joints = m_scene->joints();

    // An empty "Shapes" heading reads as a list that failed to fill.
    QTreeWidgetItem *bodiesGroup = bodies.isEmpty()
        ? nullptr
        : makeItem(nullptr, NodeKind::Group, nullptr, QIcon(),
                   tr("Bodies (%1)").arg(bodies.size()), QString());
    for (PhysicsBody *body : bodies) {
        auto *bodyItem = makeItem(bodiesGroup, NodeKind::Body, body,
                                  ObjectIcons::forBody(m_scene->bodyColor(body->props().type)),
                                  body->name(),
                                  tr("%1 body of %2 shape(s)")
                                      .arg(bodyTypeName(body)).arg(body->shapes().size()));
        for (ShapeItem *shape : body->shapes()) {
            makeItem(bodyItem, NodeKind::Shape, shape, ObjectIcons::forShape(shape),
                     shape->name(), shape->typeName());
        }
    }

    QTreeWidgetItem *jointsGroup = joints.isEmpty()
        ? nullptr
        : makeItem(nullptr, NodeKind::Group, nullptr, QIcon(),
                   tr("Joints (%1)").arg(joints.size()), QString());
    for (Joint *joint : joints) {
        auto *jointItem = makeItem(jointsGroup, NodeKind::Joint, joint,
                                   ObjectIcons::forJoint(m_scene->jointTypeColor(joint->typeId())),
                                   joint->name(),
                                   tr("%1 joint").arg(joint->typeId()));

        // Always A then B, so which is which is readable from the position.
        const QVector<PhysicsBody *> ends { joint->bodyA(), joint->bodyB() };
        for (PhysicsBody *end : ends) {
            if (!end)
                continue;
            makeItem(jointItem, NodeKind::BodyRef, end,
                     ObjectIcons::forBody(m_scene->bodyColor(end->props().type)), end->name(),
                     tr("%1 — connected by %2").arg(end->name(), joint->name()));
        }
    }

    const QVector<ExplosionItem *> explosions = m_scene->explosions();
    QTreeWidgetItem *explosionsGroup = explosions.isEmpty()
        ? nullptr
        : makeItem(nullptr, NodeKind::Group, nullptr, QIcon(),
                   tr("Explosions (%1)").arg(explosions.size()), QString());
    for (ExplosionItem *explosion : explosions) {
        makeItem(explosionsGroup, NodeKind::Explosion, explosion, Icons::explosion(), explosion->name(),
                 tr("A point at %1, %2")
                     .arg(explosion->pos().x(), 0, 'f', 0)
                     .arg(explosion->pos().y(), 0, 'f', 0));
    }

    const QVector<RayItem *> rays = m_scene->rays();
    QTreeWidgetItem *raysGroup = rays.isEmpty()
        ? nullptr
        : makeItem(nullptr, NodeKind::Group, nullptr, QIcon(),
                   tr("Rays (%1)").arg(rays.size()), QString());
    for (RayItem *ray : rays) {
        makeItem(raysGroup, NodeKind::Ray, ray, Icons::ray(), ray->name(),
                 tr("Looks %1 degrees, up to %2")
                     .arg(ray->angleDegrees(), 0, 'f', 0)
                     .arg(ray->length(), 0, 'f', 0));
    }

    // Only the shapes no body claimed; one in a body lives under that body.
    QVector<ShapeItem *> loose;
    for (ShapeItem *shape : shapes) {
        if (!shape->body())
            loose.append(shape);
    }
    QTreeWidgetItem *shapesGroup = loose.isEmpty()
        ? nullptr
        : makeItem(nullptr, NodeKind::Group, nullptr, QIcon(),
                   tr("Shapes with no body (%1)").arg(loose.size()), QString());
    for (ShapeItem *shape : loose) {
        makeItem(shapesGroup, NodeKind::Shape, shape, ObjectIcons::forShape(shape), shape->name(),
                 tr("%1 — not in a body").arg(shape->typeName()));
    }

    m_tree->expandAll();

    if (current) {
        if (QTreeWidgetItem *item = itemFor(current))
            m_tree->setCurrentItem(item);
        updateBoldRows();
    } else {
        syncSelection();
    }
}

QTreeWidgetItem *SceneTree::makeItem(QTreeWidgetItem *parent, NodeKind kind, void *object,
                                     const QIcon &icon, const QString &label,
                                     const QString &tooltip)
{
    auto *item = parent ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(m_tree);
    item->setText(0, label);
    if (!icon.isNull())
        item->setIcon(0, icon);
    if (!tooltip.isEmpty())
        item->setToolTip(0, tooltip);
    item->setData(0, kKindRole, static_cast<int>(kind));
    item->setData(0, kObjectRole, QVariant::fromValue(reinterpret_cast<quintptr>(object)));

    if (kind == NodeKind::BodyRef) {
        // Dimmed, so a reference never reads as the body's own row.
        item->setForeground(0, m_tree->palette().color(QPalette::Disabled, QPalette::Text));
    }

    if (kind == NodeKind::Group) {
        QFont font = item->font(0);
        font.setBold(true);
        item->setFont(0, font);
        item->setFlags(Qt::ItemIsEnabled);
    }

    if (kind == NodeKind::Shape) {
        connect(static_cast<ShapeItem *>(object), &ShapeItem::propertyChanged,
                this, &SceneTree::refreshLabels, Qt::UniqueConnection);
    } else if (kind == NodeKind::Body || kind == NodeKind::BodyRef) {
        connect(static_cast<PhysicsBody *>(object), &PhysicsBody::propertyChanged,
                this, &SceneTree::refreshLabels, Qt::UniqueConnection);
        connect(static_cast<PhysicsBody *>(object), &PhysicsBody::membershipChanged,
                this, &SceneTree::scheduleRebuild, Qt::UniqueConnection);
    } else if (kind == NodeKind::Joint) {
        connect(static_cast<Joint *>(object), &Joint::propertyChanged,
                this, &SceneTree::refreshLabels, Qt::UniqueConnection);
    }

    return item;
}

void SceneTree::refreshLabels()
{
    QTreeWidgetItemIterator it(m_tree);
    for (; *it; ++it) {
        QTreeWidgetItem *item = *it;
        void *object = objectOf(item);
        if (!object)
            continue;

        switch (kindOf(item)) {
        case NodeKind::Shape:
            item->setText(0, static_cast<ShapeItem *>(object)->name());
            break;
        case NodeKind::Body:
            item->setText(0, static_cast<PhysicsBody *>(object)->name());
            break;
        case NodeKind::BodyRef:
            item->setText(0, static_cast<PhysicsBody *>(object)->name());
            break;
        case NodeKind::Joint:
            item->setText(0, static_cast<Joint *>(object)->name());
            break;
        case NodeKind::Explosion:
            item->setText(0, static_cast<ExplosionItem *>(object)->name());
            break;
        case NodeKind::Ray:
            item->setText(0, static_cast<RayItem *>(object)->name());
            break;
        case NodeKind::Group:
            break;
        }
    }
}

void SceneTree::onItemActivated(QTreeWidgetItem *item)
{
    if (!item || !m_scene || m_selecting)
        return;
    if (!m_scene->selectionAllowed())
        return; // a run owns the scene; picking would fight it

    void *object = objectOf(item);
    if (!object)
        return;

    m_selecting = true;

    switch (kindOf(item)) {
    case NodeKind::Shape: {
        auto *shape = static_cast<ShapeItem *>(object);
        m_scene->setEditorMode(EditorMode::Edit);
        m_scene->selectShape(shape);
        break;
    }
    case NodeKind::BodyRef:
    case NodeKind::Body: {
        auto *body = static_cast<PhysicsBody *>(object);
        m_scene->setEditorMode(EditorMode::Physics);
        m_scene->selectJoint(nullptr);
        m_scene->clearPhysicsSelection();
        if (!body->shapes().isEmpty())
            m_scene->selectForPhysics(body->shapes().first(), true);
        break;
    }
    case NodeKind::Joint: {
        m_scene->setEditorMode(EditorMode::Physics);
        m_scene->clearPhysicsSelection();
        m_scene->selectExplosion(nullptr);
        m_scene->selectJoint(static_cast<Joint *>(object));
        break;
    }
    case NodeKind::Ray: {
        m_scene->setEditorMode(EditorMode::Physics);
        m_scene->clearPhysicsSelection();
        m_scene->selectJoint(nullptr);
        m_scene->selectRay(static_cast<RayItem *>(object));
        break;
    }
    case NodeKind::Explosion: {
        m_scene->setEditorMode(EditorMode::Physics);
        m_scene->clearPhysicsSelection();
        m_scene->selectJoint(nullptr);
        m_scene->selectExplosion(static_cast<ExplosionItem *>(object));
        break;
    }
    case NodeKind::Group:
        break;
    }

    m_selecting = false;
    updateBoldRows();
}

void *SceneTree::selectedObject() const
{
    if (!m_scene)
        return nullptr;

    if (Joint *joint = m_scene->selectedJoint())
        return joint;
    if (m_scene->editorMode() == EditorMode::Edit && m_scene->activeItem())
        return m_scene->activeItem();
    if (!m_scene->physicsSelection().isEmpty())
        return m_scene->physicsSelection().first()->body();
    return nullptr;
}

void SceneTree::updateBoldRows()
{
    void *target = selectedObject();

    QTreeWidgetItemIterator it(m_tree);
    for (; *it; ++it) {
        QTreeWidgetItem *item = *it;
        if (kindOf(item) == NodeKind::Group)
            continue; // headings are bold already, and stand for nothing

        const bool selected = target && objectOf(item) == target;
        QFont font = item->font(0);
        if (font.bold() == selected)
            continue;
        font.setBold(selected);
        item->setFont(0, font);
    }
}

void SceneTree::syncSelection()
{
    if (!m_scene || m_selecting)
        return;

    void *target = selectedObject();

    const QSignalBlocker blocker(m_tree);
    if (!target)
        m_tree->setCurrentItem(nullptr);
    else if (QTreeWidgetItem *item = itemFor(target))
        m_tree->setCurrentItem(item);

    updateBoldRows();
}

QTreeWidgetItem *SceneTree::itemFor(void *object) const
{
    if (!object)
        return nullptr;

    QTreeWidgetItemIterator it(const_cast<QTreeWidget *>(m_tree));
    for (; *it; ++it) {
        const NodeKind kind = kindOf(*it);
        if (kind == NodeKind::BodyRef)
            continue; // a mention of a body, not the body's own row
        if (objectOf(*it) == object)
            return *it;
    }
    return nullptr;
}

SceneTree::NodeKind SceneTree::kindOf(const QTreeWidgetItem *item)
{
    return static_cast<NodeKind>(item->data(0, kKindRole).toInt());
}

void *SceneTree::objectOf(const QTreeWidgetItem *item)
{
    return reinterpret_cast<void *>(item->data(0, kObjectRole).value<quintptr>());
}
