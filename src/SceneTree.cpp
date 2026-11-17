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
#include <QMenu>
#include <functional>
#include <QLineEdit>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <algorithm>
#include <QVBoxLayout>
#include <QtMath>

namespace {

constexpr int kKindRole = Qt::UserRole;
constexpr int kObjectRole = Qt::UserRole + 1;
// Steady across rebuilds, where a row's own text is not: a group's caption
// carries a count, and that count moves whenever the scene does.
constexpr int kKeyRole = Qt::UserRole + 2;

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

    // Rules address objects by name, so a scene is navigated by name: the box
    // narrows the tree as you type rather than jumping to one match, since a
    // name is often a stem shared by a dozen of them.
    m_filter = new QLineEdit(this);
    m_filter->setClearButtonEnabled(true);
    m_filter->setPlaceholderText(tr("Find by name"));
    m_filter->setToolTip(tr("Show only the objects whose name contains this"));
    layout->addWidget(m_filter);

    m_tree = new QTreeWidget(this);
    m_tree->setHeaderHidden(true);
    m_tree->setColumnCount(1);
    m_tree->setIconSize(QSize(ObjectIcons::size(), ObjectIcons::size()));
    m_tree->setUniformRowHeights(true);
    m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tree->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_tree->setExpandsOnDoubleClick(false);
    layout->addWidget(m_tree);

    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_tree, &QTreeWidget::customContextMenuRequested,
            this, &SceneTree::showContextMenu);
    // Folding a row by its own arrow has to be remembered too, or the next
    // change to the scene rebuilds the tree and opens it again.
    connect(m_tree, &QTreeWidget::itemCollapsed, this, [this](QTreeWidgetItem *) {
        rememberExpansion();
    });
    connect(m_tree, &QTreeWidget::itemExpanded, this, [this](QTreeWidgetItem *) {
        rememberExpansion();
    });

    connect(m_tree, &QTreeWidget::itemClicked, this,
            [this](QTreeWidgetItem *item, int) { onItemActivated(item); });
    // Picking a row points the panels at that object, and brings the table
    // that shows it to the front -- otherwise the properties change behind the
    // tree you are still looking at, which reads as the click doing nothing.
    // Selection made from elsewhere comes through syncSelection with the
    // tree's signals blocked, so this only follows a row the user picked.
    connect(m_tree, &QTreeWidget::currentItemChanged, this,
            [this](QTreeWidgetItem *item, QTreeWidgetItem *) {
                onItemActivated(item);
                if (item && kindOf(item) != NodeKind::Group)
                    emit propertiesRequested();
            });
    connect(m_tree, &QTreeWidget::itemDoubleClicked, this,
            [this](QTreeWidgetItem *item, int) {
                onItemActivated(item);
                if (item && kindOf(item) != NodeKind::Group)
                    emit propertiesRequested();
            });

    connect(m_filter, &QLineEdit::textChanged, this, [this](const QString &) {
        applyFilter();
    });
    // Enter on a filter that has found one thing picks it, which is what a
    // find box is for; on several it leaves the list as it is.
    connect(m_filter, &QLineEdit::returnPressed, this, [this] {
        QTreeWidgetItem *only = nullptr;
        int found = 0;
        QTreeWidgetItemIterator it(m_tree, QTreeWidgetItemIterator::NotHidden);
        for (; *it; ++it) {
            if (kindOf(*it) == NodeKind::Group)
                continue;
            ++found;
            only = *it;
        }
        if (found == 1 && only) {
            m_tree->setCurrentItem(only);
            onItemActivated(only);
            emit propertiesRequested();
        }
    });
}

void SceneTree::applyFilter()
{
    if (!m_tree || !m_filter)
        return;

    const QString wanted = m_filter->text().trimmed();
    QTreeWidgetItemIterator all(m_tree);
    if (wanted.isEmpty()) {
        for (; *all; ++all)
            (*all)->setHidden(false);
        applyExpansion();
        return;
    }

    // Leaves first, then the groups: a group is kept only for what survived
    // under it, so it has to be judged after its children.
    QVector<QTreeWidgetItem *> groups;
    for (; *all; ++all) {
        QTreeWidgetItem *row = *all;
        if (kindOf(row) == NodeKind::Group) {
            groups.append(row);
            continue;
        }
        row->setHidden(!row->text(0).contains(wanted, Qt::CaseInsensitive));
    }
    // Deepest first, so a group of groups sees its own children settled.
    std::sort(groups.begin(), groups.end(), [](QTreeWidgetItem *a, QTreeWidgetItem *b) {
        const auto depth = [](QTreeWidgetItem *row) {
            int d = 0;
            for (QTreeWidgetItem *up = row->parent(); up; up = up->parent())
                ++d;
            return d;
        };
        return depth(a) > depth(b);
    });
    for (QTreeWidgetItem *group : groups) {
        bool keep = false;
        for (int i = 0; i < group->childCount(); ++i) {
            if (!group->child(i)->isHidden()) {
                keep = true;
                break;
            }
        }
        group->setHidden(!keep);
        if (keep)
            group->setExpanded(true); // a match two levels down is no use folded
    }
}

void SceneTree::connectScene()
{
    if (m_scene) {
        connect(m_scene, &CanvasScene::shapesChanged, this, &SceneTree::scheduleRebuild);
        connect(m_scene, &CanvasScene::explosionsChanged, this, &SceneTree::scheduleRebuild);
        connect(m_scene, &CanvasScene::raysChanged, this, &SceneTree::scheduleRebuild);
        connect(m_scene, &CanvasScene::bodiesChanged, this, &SceneTree::scheduleRebuild);
        connect(m_scene, &CanvasScene::jointsChanged, this, &SceneTree::scheduleRebuild);

        // Deliberately live through a run. The tree changes nothing -- its rows
        // point the panels at an object and its menu folds them -- and picking
        // one mid-run is how you read an object's live values, which is exactly
        // when they are worth reading. The canvas cannot do it: a press there
        // pans the field while a run is going. See onItemActivated(), which
        // holds back the mode switch rather than the selection.
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
                   tr("Bodies (%1)").arg(bodies.size()), QString(), QStringLiteral("group:bodies"));
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
                   tr("Joints (%1)").arg(joints.size()), QString(), QStringLiteral("group:joints"));
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
                   tr("Explosions (%1)").arg(explosions.size()), QString(),
                   QStringLiteral("group:explosions"));
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
                   tr("Rays (%1)").arg(rays.size()), QString(), QStringLiteral("group:rays"));
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
                   tr("Shapes with no body (%1)").arg(loose.size()), QString(), QStringLiteral("group:looseShapes"));
    for (ShapeItem *shape : loose) {
        makeItem(shapesGroup, NodeKind::Shape, shape, ObjectIcons::forShape(shape), shape->name(),
                 tr("%1 — not in a body").arg(shape->typeName()));
    }

    applyExpansion();
    // The rows are new, and know nothing of what is typed in the box.
    applyFilter();

    if (current) {
        if (QTreeWidgetItem *item = itemFor(current))
            m_tree->setCurrentItem(item);
        updateBoldRows();
    } else {
        syncSelection();
    }
}

void SceneTree::rememberExpansion()
{
    m_collapsedKeys.clear();
    std::function<void(QTreeWidgetItem *)> walk = [&](QTreeWidgetItem *parent) {
        for (int i = 0; i < parent->childCount(); ++i) {
            QTreeWidgetItem *child = parent->child(i);
            if (child->childCount() > 0 && !child->isExpanded())
                m_collapsedKeys.insert(keyOf(child));
            walk(child);
        }
    };
    walk(m_tree->invisibleRootItem());
}

void SceneTree::applyExpansion()
{
    // Open by default -- a tree that hides what is in it is no use as a list
    // of the scene -- and then folded back wherever it was left folded.
    m_tree->expandAll();
    if (m_collapsedKeys.isEmpty())
        return;

    std::function<void(QTreeWidgetItem *)> walk = [&](QTreeWidgetItem *parent) {
        for (int i = 0; i < parent->childCount(); ++i) {
            QTreeWidgetItem *child = parent->child(i);
            walk(child);   // innermost first, or folding the parent hides it
            if (child->childCount() > 0 && m_collapsedKeys.contains(keyOf(child)))
                child->setExpanded(false);
        }
    };
    walk(m_tree->invisibleRootItem());
}

void SceneTree::showContextMenu(const QPoint &pos)
{
    QTreeWidgetItem *item = m_tree->itemAt(pos);
    QMenu menu(this);

    // What was clicked, when it holds anything: a group, a body with shapes, a
    // joint with the bodies it holds.
    if (item && item->childCount() > 0) {
        const bool open = item->isExpanded();
        const QString name = item->text(0);
        menu.addAction(open ? tr("Collapse %1").arg(name) : tr("Expand %1").arg(name),
                       this, [this, item, open] {
                           item->setExpanded(!open);
                           rememberExpansion();
                       });
        menu.addSeparator();
    }

    menu.addAction(tr("Expand All"), this, [this] {
        m_collapsedKeys.clear();
        m_tree->expandAll();
    });
    menu.addAction(tr("Collapse All"), this, [this] {
        m_tree->collapseAll();
        rememberExpansion();
    });

    menu.exec(m_tree->viewport()->mapToGlobal(pos));
}

QString SceneTree::keyOf(const QTreeWidgetItem *item)
{
    return item ? item->data(0, kKeyRole).toString() : QString();
}

QTreeWidgetItem *SceneTree::makeItem(QTreeWidgetItem *parent, NodeKind kind, void *object,
                                     const QIcon &icon, const QString &label,
                                     const QString &tooltip, const QString &key)
{
    auto *item = parent ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(m_tree);
    item->setText(0, label);
    if (!icon.isNull())
        item->setIcon(0, icon);
    if (!tooltip.isEmpty())
        item->setToolTip(0, tooltip);
    item->setData(0, kKindRole, static_cast<int>(kind));
    item->setData(0, kObjectRole, QVariant::fromValue(reinterpret_cast<quintptr>(object)));
    // A group is told what to call itself; everything else is named after the
    // object it stands for, with its kind alongside -- a joint lists the
    // bodies it holds, and those rows carry the same names as the bodies' own.
    item->setData(0, kKeyRole,
                  key.isEmpty() ? QStringLiteral("%1:%2").arg(int(kind)).arg(label) : key);

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
    void *object = objectOf(item);
    if (!object)
        return;

    m_selecting = true;

    // A run owns the canvas, but picking a row is not editing it -- it points
    // the panels at an object so its live values can be read while the scene
    // moves. What a run does hold back is the mode switch: the mode buttons
    // are disabled for the length of one, and the tree has no business going
    // round them.
    const bool running = !m_scene->selectionAllowed();
    const auto enterMode = [this, running](EditorMode mode) {
        if (!running)
            m_scene->setEditorMode(mode);
    };
    // So a run shows a shape wherever the mode it was started in can show it:
    // on its own in Edit, as part of its body in Physics. Pointed at the
    // physics selection in Edit mode it would have landed in a table the panel
    // was not showing, and the click would have read as doing nothing.
    const auto showWhileRunning = [this](ShapeItem *shape, bool wholeBody) {
        if (m_scene->editorMode() == EditorMode::Edit) {
            m_scene->selectShape(shape);
            return;
        }
        m_scene->selectJoint(nullptr);
        m_scene->clearPhysicsSelection();
        m_scene->selectForPhysics(shape, wholeBody);
    };

    switch (kindOf(item)) {
    case NodeKind::Shape: {
        auto *shape = static_cast<ShapeItem *>(object);
        if (running) {
            showWhileRunning(shape, false);
            break;
        }
        enterMode(EditorMode::Edit);
        m_scene->selectShape(shape);
        break;
    }
    case NodeKind::BodyRef:
    case NodeKind::Body: {
        auto *body = static_cast<PhysicsBody *>(object);
        if (running) {
            if (!body->shapes().isEmpty())
                showWhileRunning(body->shapes().first(), true);
            break;
        }
        enterMode(EditorMode::Physics);
        m_scene->selectJoint(nullptr);
        m_scene->clearPhysicsSelection();
        if (!body->shapes().isEmpty())
            m_scene->selectForPhysics(body->shapes().first(), true);
        break;
    }
    case NodeKind::Joint: {
        enterMode(EditorMode::Physics);
        m_scene->clearPhysicsSelection();
        m_scene->selectExplosion(nullptr);
        m_scene->selectJoint(static_cast<Joint *>(object));
        break;
    }
    case NodeKind::Ray: {
        enterMode(EditorMode::Physics);
        m_scene->clearPhysicsSelection();
        m_scene->selectJoint(nullptr);
        m_scene->selectRay(static_cast<RayItem *>(object));
        break;
    }
    case NodeKind::Explosion: {
        enterMode(EditorMode::Physics);
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
