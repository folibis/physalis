#include "CanvasScene.h"
#include "PhysicsBody.h"
#include "Naming.h"
#include "Joint.h"
#include "EngineRegistry.h"
#include "RectangleItem.h"
#include "CircleItem.h"
#include "ExplosionItem.h"
#include "RayItem.h"
#include "PolygonItem.h"
#include "PropertyPane/FieldPropertyPane.h"

#include <QGraphicsSceneMouseEvent>
#include <QGraphicsSceneWheelEvent>
#include <QGraphicsView>
#include <QScrollBar>
#include <QLineF>
#include <QPainter>
#include <QPainterPathStroker>
#include <QTransform>
#include <QKeyEvent>
#include <QtMath>
#include <QList>
#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace {

qreal distanceToSegment(const QPointF &p, const QPointF &a, const QPointF &b)
{
    const QPointF ab = b - a;
    const qreal lengthSquared = ab.x() * ab.x() + ab.y() * ab.y();
    if (lengthSquared <= 0.0)
        return QLineF(a, p).length();

    const QPointF ap = p - a;
    const qreal t = qBound(0.0, (ap.x() * ab.x() + ap.y() * ab.y()) / lengthSquared, 1.0);
    return QLineF(a + t * ab, p).length();
}

qreal angleAt(const QPointF &origin, const QPointF &pos)
{
    const QPointF d = pos - origin;
    return qRadiansToDegrees(std::atan2(d.y(), d.x()));
}

void applyDefaultStyle(ShapeItem *item, const QColor &borderColor, qreal borderWidth, const QColor &bodyColor)
{
    item->setBorderColor(borderColor);
    item->setBorderWidth(borderWidth);
    item->setBodyColor(bodyColor);
}

} // namespace

CanvasScene::CanvasScene(QObject *parent)
    : QGraphicsScene(parent)
{
    setSceneRect(-m_fieldWidth / 2.0, -m_fieldHeight / 2.0, m_fieldWidth, m_fieldHeight);
}

PropertyPane *CanvasScene::makePropertyPane() const
{
    return new FieldPropertyPane();
}

QSet<QString> CanvasScene::takenNames(const QObject *except) const
{
    QSet<QString> taken;
    for (ShapeItem *shape : shapes()) {
        if (shape != except)
            taken.insert(shape->name());
    }
    for (PhysicsBody *body : m_bodies) {
        if (body != except)
            taken.insert(body->name());
    }
    for (Joint *joint : m_joints) {
        if (joint != except)
            taken.insert(joint->name());
    }
    return taken;
}

QString CanvasScene::uniqueName(const QString &desired, const QObject *except) const
{
    return Naming::makeUnique(desired, takenNames(except));
}

QVector<ShapeItem *> CanvasScene::shapes() const
{
    QVector<ShapeItem *> found;
    const QList<QGraphicsItem *> all = items(Qt::AscendingOrder);
    found.reserve(all.size());
    for (QGraphicsItem *item : all) {
        if (auto *shape = qgraphicsitem_cast<ShapeItem *>(item))
            found.append(shape);
    }
    return found;
}

RectangleItem *CanvasScene::addRectangle(const QPointF &scenePos)
{
    auto *item = new RectangleItem();
    applyDefaultStyle(item, m_defaultBorderColor, m_defaultBorderWidth, m_defaultBodyColor);
    item->setPos(scenePos);
    addItem(item);
    item->setName(uniqueName(item->name(), item));
    emit shapesChanged();
    return item;
}

ExplosionItem *CanvasScene::addExplosion(const QPointF &scenePos)
{
    auto *explosion = new ExplosionItem();
    explosion->setPos(scenePos);
    addItem(explosion);
    explosion->setName(uniqueName(Naming::nextName(ExplosionItem::typeName()), nullptr));
    explosion->setVisible(m_editorMode != EditorMode::Edit);
    m_explosions.append(explosion);
    emit explosionsChanged();
    return explosion;
}

void CanvasScene::removeExplosion(ExplosionItem *explosion)
{
    if (!explosion || !m_explosions.removeOne(explosion))
        return;
    removeItem(explosion);
    delete explosion;
    emit explosionsChanged();
}

RayItem *CanvasScene::addRay(const QPointF &scenePos)
{
    auto *ray = new RayItem();
    ray->setPos(scenePos);
    addItem(ray);
    ray->setName(uniqueName(Naming::nextName(RayItem::typeName()), nullptr));
    ray->setVisible(m_editorMode != EditorMode::Edit);
    m_rays.append(ray);
    emit raysChanged();
    return ray;
}

void CanvasScene::removeRay(RayItem *ray)
{
    if (!ray || !m_rays.removeOne(ray))
        return;
    if (m_selectedRay == ray) {
        m_selectedRay = nullptr;
        emit selectedRayChanged(nullptr);
    }
    if (m_draggedRay == ray)
        m_draggedRay = nullptr;
    removeItem(ray);
    delete ray;
    emit raysChanged();
}

RayItem *CanvasScene::rayNamed(const QString &name) const
{
    for (RayItem *ray : m_rays)
        if (ray->name() == name)
            return ray;
    return nullptr;
}

void CanvasScene::selectRay(RayItem *ray)
{
    if (m_selectedRay == ray)
        return;
    if (ray) {
        clearPhysicsSelection();
        selectJoint(nullptr);
        selectExplosion(nullptr);
    }
    if (m_selectedRay)
        m_selectedRay->setSelectedForPhysics(false);
    m_selectedRay = ray;
    if (m_selectedRay)
        m_selectedRay->setSelectedForPhysics(true);
    emit selectedRayChanged(ray);
}

void CanvasScene::selectExplosion(ExplosionItem *explosion)
{
    if (m_selectedExplosion == explosion)
        return;
    if (explosion) {
        clearPhysicsSelection();
        selectJoint(nullptr);
    }
    if (m_selectedExplosion)
        m_selectedExplosion->setSelectedForPhysics(false);
    m_selectedExplosion = explosion;
    if (m_selectedExplosion)
        m_selectedExplosion->setSelectedForPhysics(true);
    emit selectedExplosionChanged(explosion);
}

ExplosionItem *CanvasScene::explosionNamed(const QString &name) const
{
    for (ExplosionItem *explosion : m_explosions)
        if (explosion->name() == name)
            return explosion;
    return nullptr;
}

CircleItem *CanvasScene::addCircle(const QPointF &scenePos)
{
    auto *item = new CircleItem();
    applyDefaultStyle(item, m_defaultBorderColor, m_defaultBorderWidth, m_defaultBodyColor);
    item->setPos(scenePos);
    addItem(item);
    item->setName(uniqueName(item->name(), item));
    emit shapesChanged();
    return item;
}

void CanvasScene::setFieldSize(qreal width, qreal height)
{
    m_fieldWidth = width;
    m_fieldHeight = height;
    setSceneRect(-width / 2.0, -height / 2.0, width, height);
    emit fieldPropertyChanged();
}

void CanvasScene::setEditorMode(EditorMode mode)
{
    if (m_editorMode == mode)
        return;

    m_editorMode = mode;
    m_dragMode = DragMode::None;

    if (m_polygonDrawing)
        cancelPolygonDrawing();
    setNodeSelection({});
    deactivate();
    clearPhysicsSelection();
    selectJoint(nullptr);
    m_draggedJoint = nullptr;

    selectExplosion(nullptr);
    selectRay(nullptr);
    m_draggedExplosion = nullptr;
    m_draggedRay = nullptr;
    const bool physics = m_editorMode != EditorMode::Edit;
    for (ExplosionItem *explosion : std::as_const(m_explosions))
        explosion->setVisible(physics);
    for (RayItem *ray : std::as_const(m_rays))
        ray->setVisible(physics);

    update();
    emit editorModeChanged(m_editorMode);
}

void CanvasScene::selectForPhysics(ShapeItem *shape, bool additive)
{
    if (!shape) {
        clearPhysicsSelection();
        return;
    }

    // Picking a body is picking something else: the explosion stops being
    // what the panel and the Remove button are pointed at.
    selectExplosion(nullptr);

    if (additive) {
        QVector<ShapeItem *> group;
        if (PhysicsBody *body = shape->body())
            group = body->shapes();
        else
            group.append(shape);

        const bool alreadyPicked = m_physicsSelection.contains(shape);
        for (ShapeItem *member : std::as_const(group)) {
            if (alreadyPicked)
                m_physicsSelection.removeOne(member);
            else if (!m_physicsSelection.contains(member))
                m_physicsSelection.append(member);
        }
    } else if (m_physicsSelection.size() == 1 && m_physicsSelection.first() == shape) {
        return;
    } else {
        m_physicsSelection.clear();
        m_physicsSelection.append(shape);
    }

    update();
    emit physicsSelectionChanged();
}

void CanvasScene::clearPhysicsSelection()
{
    if (m_physicsSelection.isEmpty())
        return;
    m_physicsSelection.clear();
    update();
    emit physicsSelectionChanged();
}

PhysicsBody *CanvasScene::createEmptyBody()
{
    auto *body = new PhysicsBody(this);
    body->setName(Naming::nextName(QStringLiteral("body"), takenNames()));

    connect(body, &PhysicsBody::nameChanged, this, &CanvasScene::renameInRules);
    connect(body, &PhysicsBody::propertyChanged, this, [this] { update(); });
    connect(body, &PhysicsBody::membershipChanged, this, [this] {
        update();

        // Queued, not immediate. removeShape() is emitted from ~ShapeItem, so
        // this runs while a shape is half destroyed -- and destroying a body
        // here would delete its joints and emit three more signals from inside
        // that destructor. Deferring also means a body being rebuilt shape by
        // shape isn't torn down between the first removal and the first add.
        if (m_prunePending)
            return;
        m_prunePending = true;
        QMetaObject::invokeMethod(this, [this] { pruneEmptyBodies(); },
                                  Qt::QueuedConnection);
    });

    m_bodies.append(body);
    emit bodiesChanged();
    return body;
}

void CanvasScene::clearContents()
{
    deactivate();
    clearPhysicsSelection();
    clearWatches();

    m_rules.clear();
    emit rulesChanged();

    qDeleteAll(m_explosions);
    m_explosions.clear();
    emit explosionsChanged();

    m_selectedRay = nullptr;
    m_draggedRay = nullptr;
    qDeleteAll(m_rays);
    m_rays.clear();
    emit raysChanged();


    m_selectedJoint = nullptr;
    qDeleteAll(m_joints);
    m_joints.clear();
    emit selectedJointChanged(nullptr);
    emit jointsChanged();

    qDeleteAll(m_bodies);
    m_bodies.clear();

    const QList<QGraphicsItem *> existing = items();
    for (QGraphicsItem *item : existing) {
        if (auto *shape = qgraphicsitem_cast<ShapeItem *>(item)) {
            removeItem(shape);
            delete shape;
        }
    }

    update();
    emit shapesChanged();
    emit bodiesChanged();
    emit physicsSelectionChanged();
}

PhysicsBody *CanvasScene::createBodyFromSelection()
{
    if (m_physicsSelection.isEmpty())
        return nullptr;

    PhysicsBody *body = createEmptyBody();

    QVector<PhysicsBody *> vacated;
    for (ShapeItem *shape : m_physicsSelection) {
        if (PhysicsBody *previous = shape->body()) {
            if (!vacated.contains(previous))
                vacated.append(previous);
        }
        body->addShape(shape);
    }

    // Through destroyBody(), not a raw delete, so the body's joints go too.
    for (PhysicsBody *previous : vacated) {
        if (previous->isEmpty())
            destroyBody(previous);
    }

    update();
    emit bodiesChanged();
    emit physicsSelectionChanged();
    return body;
}

Joint *CanvasScene::createJoint(const QString &typeId, PhysicsBody *bodyA, PhysicsBody *bodyB,
                                int anchorCount, const QVariantMap &defaultParams)
{
    if (!bodyA || !bodyB || bodyA == bodyB)
        return nullptr;

    auto *joint = new Joint(this);
    joint->setName(Naming::nextName(typeId, takenNames()));
    joint->setTypeId(typeId);
    joint->setBodies(bodyA, bodyB);
    joint->params() = defaultParams;

    joint->setAnchorCount(anchorCount);

    const QPointF centreA = bodyA->centerOfMassScenePos();
    const QPointF centreB = bodyB->centerOfMassScenePos();

    if (anchorCount > 1) {
        joint->setAnchorScenePos(Joint::End::A, centreA);
        joint->setAnchorScenePos(Joint::End::B, centreB);
    } else {
        joint->setAnchorScenePos(Joint::End::A, (centreA + centreB) / 2.0);
    }

    const QPointF span = centreB - centreA;
    if (!qFuzzyIsNull(span.x()) || !qFuzzyIsNull(span.y()))
        joint->setAxisScene(span);

    // Some parameters only make sense measured from the bodies as they stand.
    // Which ones is the engine's business; this just asks for the measurement
    // it names.
    if (auto engine = physics::EngineRegistry::create(describingEngineName())) {
        for (const physics::JointType &type : engine->jointTypes()) {
            if (type.id != typeId)
                continue;
            QTransform intoBodyA;
            intoBodyA.rotate(-bodyA->rotationDegrees());
            const QPointF offset = intoBodyA.map(bodyB->originScenePos() - bodyA->originScenePos());
            const qreal angle = bodyB->rotationDegrees() - bodyA->rotationDegrees();
            for (const physics::JointParam &param : type.params) {
                switch (param.defaultSource) {
                case physics::DefaultSource::Fixed:
                    break;
                case physics::DefaultSource::RelativeX:
                    joint->params().insert(param.key, offset.x());
                    break;
                case physics::DefaultSource::RelativeY:
                    joint->params().insert(param.key, offset.y());
                    break;
                case physics::DefaultSource::RelativeAngleDegrees:
                    joint->params().insert(param.key, angle);
                    break;
                }
            }
            break;
        }
    }

    connect(joint, &Joint::nameChanged, this, &CanvasScene::renameInRules);
    connect(joint, &Joint::propertyChanged, this, [this] { update(); });

    m_joints.append(joint);
    update();
    emit jointsChanged();
    return joint;
}

void CanvasScene::destroyJoint(Joint *joint)
{
    if (!joint || !m_joints.removeOne(joint))
        return;
    if (m_selectedJoint == joint) {
        m_selectedJoint = nullptr;
        emit selectedJointChanged(nullptr);
    }
    delete joint;
    update();
    emit jointsChanged();
}

void CanvasScene::setJointSelectionColor(const QColor &color)
{
    if (m_jointSelectionColor == color || !color.isValid())
        return;
    m_jointSelectionColor = color;
    update();
}

void CanvasScene::setJointSelectionLineWidth(qreal width)
{
    width = qBound(0.5, width, 20.0);
    if (qFuzzyCompare(m_jointSelectionLineWidth, width))
        return;
    m_jointSelectionLineWidth = width;
    update();
}

void CanvasScene::setJointSelectionLineStyle(Qt::PenStyle style)
{
    if (m_jointSelectionLineStyle == style)
        return;
    m_jointSelectionLineStyle = style;
    update();
}

void CanvasScene::setJointTypeColor(const QString &typeId, const QColor &color)
{
    if (typeId.isEmpty())
        return;
    if (m_jointTypeColors.value(typeId) == color)
        return;
    m_jointTypeColors.insert(typeId, color);
    update();
}

void CanvasScene::setJointTypeColors(const QHash<QString, QColor> &colors)
{
    if (m_jointTypeColors == colors)
        return;
    m_jointTypeColors = colors;
    update();
}

QColor CanvasScene::jointTypeColor(const QString &typeId) const
{
    const auto it = m_jointTypeColors.constFind(typeId);
    if (it != m_jointTypeColors.constEnd() && it->isValid())
        return *it;

    if (auto engine = physics::EngineRegistry::create(describingEngineName())) {
        for (const physics::JointType &type : engine->jointTypes()) {
            if (type.id == typeId)
                return type.color;
        }
    }
    return m_jointColor;
}

QString CanvasScene::describingEngineName() const
{
    if (!m_simulationEngineName.isEmpty())
        return m_simulationEngineName;
    // A scene file carries no engine name, so one loaded before the simulation
    // is set up has none. Falling through to "no engine" would silently drop
    // every joint back to a default colour and a default shape.
    const QStringList available = physics::EngineRegistry::availableEngines();
    return available.isEmpty() ? QString() : available.first();
}

physics::JointVisual CanvasScene::jointVisual(const QString &typeId) const
{
    const QString engineName = describingEngineName();
    if (m_jointVisualsEngine != engineName || m_jointVisuals.isEmpty()) {
        m_jointVisualsEngine = engineName;
        m_jointVisuals.clear();
        if (auto engine = physics::EngineRegistry::create(engineName)) {
            for (const physics::JointType &type : engine->jointTypes())
                m_jointVisuals.insert(type.id, type.visual);
        }
    }
    return m_jointVisuals.value(typeId, physics::JointVisual::Pivot);
}

void CanvasScene::setJointColor(const QColor &color)
{
    if (m_jointColor == color)
        return;
    m_jointColor = color;
    update();
}

void CanvasScene::setJointAnchorRadius(qreal radius)
{
    radius = qBound(2.0, radius, 100.0);
    if (qFuzzyCompare(m_jointAnchorRadius, radius))
        return;
    m_jointAnchorRadius = radius;
    update();
}

void CanvasScene::setJointAxisLength(qreal length)
{
    length = qMax(1.0, length);
    if (qFuzzyCompare(m_jointAxisLength, length))
        return;
    m_jointAxisLength = length;
    update();
}

void CanvasScene::setJointWaistWidth(qreal width)
{
    width = qBound(1.0, width, 100.0);
    if (qFuzzyCompare(m_jointWaistWidth, width))
        return;
    m_jointWaistWidth = width;
    update();
}

void CanvasScene::setJointOutlineWidth(qreal width)
{
    width = qMax(0.0, width);
    if (qFuzzyCompare(m_jointOutlineWidth, width))
        return;
    m_jointOutlineWidth = width;
    update();
}

void CanvasScene::setJointOutlineColor(const QColor &color)
{
    if (m_jointOutlineColor == color)
        return;
    m_jointOutlineColor = color;
    update();
}

void CanvasScene::setSimulationEngineName(const QString &name)
{
    if (m_simulationEngineName == name)
        return;
    m_simulationEngineName = name;
    emit jointsChanged();
}

void CanvasScene::selectJoint(Joint *joint)
{
    if (m_selectedJoint == joint)
        return;
    m_selectedJoint = joint;
    update();
    emit selectedJointChanged(m_selectedJoint);
}

Joint *CanvasScene::jointAt(const QPointF &scenePos, int *end) const
{
    const qreal zoom = 100.0 / qMax(1.0, m_currentScale);
    const qreal anchorRadius = qMax(10.0, m_jointAnchorRadius) * zoom;
    const qreal shaftRadius = (m_jointWaistWidth / 2.0 + 4.0) * zoom;

    for (Joint *joint : m_joints) {
        for (int which = 0; which < joint->anchorCount(); ++which) {
            const auto whichEnd = which == 0 ? Joint::End::A : Joint::End::B;
            const QPointF anchor = joint->anchorScenePos(whichEnd);
            if (QLineF(anchor, scenePos).length() <= anchorRadius) {
                if (end)
                    *end = which;
                return joint;
            }
        }
    }

    for (Joint *joint : m_joints) {
        const int anchors = joint->anchorCount();
        if (anchors == 1)
            continue; // a single pin is all anchor and no shaft
        const QPointF a = anchors > 0 ? joint->anchorScenePos(Joint::End::A)
                                      : joint->bodyA()->centerOfMassScenePos();
        const QPointF b = anchors > 0 ? joint->anchorScenePos(Joint::End::B)
                                      : joint->bodyB()->centerOfMassScenePos();
        if (distanceToSegment(scenePos, a, b) <= shaftRadius) {
            if (end)
                *end = kJointShaft;
            return joint;
        }
    }

    return nullptr;
}

void CanvasScene::renameInRules(const QString &previous, const QString &current)
{
    if (previous.isEmpty() || previous == current)
        return;

    bool touched = false;
    for (Rule &rule : m_rules) {
        if (rule.subjectName == previous) {
            rule.subjectName = current;
            touched = true;
        }
        if (rule.isEvent() && rule.conditionValue.toString() == previous) {
            rule.conditionValue = current;
            touched = true;
        }
        if (rule.targetName == previous) {
            rule.targetName = current;
            touched = true;
        }
    }

    Q_UNUSED(touched);
    emit rulesChanged();
}

void CanvasScene::setRules(const QVector<Rule> &rules)
{
    m_rules = rules;
    emit rulesChanged();
}

void CanvasScene::pruneEmptyBodies()
{
    m_prunePending = false;

    // Over a copy: destroyBody() removes from m_bodies as it goes.
    const QVector<PhysicsBody *> existing = m_bodies;
    for (PhysicsBody *body : existing) {
        if (body->isEmpty())
            destroyBody(body); // which takes that body's joints with it
    }
}

void CanvasScene::destroyBody(PhysicsBody *body)
{
    if (!body || !m_bodies.removeOne(body))
        return;

    // A joint with one end gone constrains nothing and cannot be drawn.
    const QVector<Joint *> attached = m_joints;
    for (Joint *joint : attached) {
        if (joint->bodyA() == body || joint->bodyB() == body)
            destroyJoint(joint);
    }
    delete body;
    update();
    emit bodiesChanged();
    emit physicsSelectionChanged();
}

PhysicsBody *CanvasScene::commonSelectedBody() const
{
    if (m_physicsSelection.isEmpty())
        return nullptr;

    PhysicsBody *body = m_physicsSelection.first()->body();
    if (!body)
        return nullptr;
    for (ShapeItem *shape : m_physicsSelection) {
        if (shape->body() != body)
            return nullptr;
    }
    return body;
}

QColor CanvasScene::bodyColor(physics::BodyType type) const
{
    switch (type) {
    case physics::BodyType::Static:    return m_bodyStaticColor;
    case physics::BodyType::Kinematic: return m_bodyKinematicColor;
    case physics::BodyType::Dynamic:   return m_bodyDynamicColor;
    }
    return m_bodyDynamicColor;
}

void CanvasScene::setBodyColor(physics::BodyType type, const QColor &color)
{
    QColor *target = nullptr;
    switch (type) {
    case physics::BodyType::Static:    target = &m_bodyStaticColor; break;
    case physics::BodyType::Kinematic: target = &m_bodyKinematicColor; break;
    case physics::BodyType::Dynamic:   target = &m_bodyDynamicColor; break;
    }
    if (!target || *target == color)
        return;
    *target = color;
    update();
}

void CanvasScene::setUnassignedShapeColor(const QColor &color)
{
    if (m_unassignedShapeColor == color)
        return;
    m_unassignedShapeColor = color;
    update();
}

void CanvasScene::setPhysicsBorderWidth(qreal width)
{
    width = qMax(0.0, width);
    if (qFuzzyCompare(m_physicsBorderWidth, width))
        return;
    m_physicsBorderWidth = width;
    update();
}

void CanvasScene::setPhysicsFillAlpha(int alpha)
{
    alpha = qBound(0, alpha, 255);
    if (m_physicsFillAlpha == alpha)
        return;
    m_physicsFillAlpha = alpha;
    update();
}

void CanvasScene::setPhysicsSelectionLineStyle(Qt::PenStyle style)
{
    if (m_physicsSelectionLineStyle == style)
        return;
    m_physicsSelectionLineStyle = style;
    update();
}

void CanvasScene::setPhysicsSelectionLineWidth(qreal width)
{
    width = qMax(0.0, width);
    if (qFuzzyCompare(m_physicsSelectionLineWidth, width))
        return;
    m_physicsSelectionLineWidth = width;
    update();
}

void CanvasScene::setPhysicsSelectionColor(const QColor &color)
{
    if (m_physicsSelectionColor == color)
        return;
    m_physicsSelectionColor = color;
    update();
}

void CanvasScene::addWatch(const Watch &watch)
{
    if (watch.objectName.isEmpty() || watch.propertyKey.isEmpty() || m_watches.contains(watch))
        return;
    m_watches.append(watch);
    emit watchesChanged();
}

void CanvasScene::removeWatch(const QString &objectName, const QString &propertyKey)
{
    const int before = m_watches.size();
    m_watches.removeIf([&](const Watch &w) {
        return w.objectName == objectName && w.propertyKey == propertyKey;
    });
    if (m_watches.size() != before)
        emit watchesChanged();
}

bool CanvasScene::isWatched(const QString &objectName, const QString &propertyKey) const
{
    for (const Watch &w : m_watches) {
        if (w.objectName == objectName && w.propertyKey == propertyKey)
            return true;
    }
    return false;
}

void CanvasScene::clearWatches()
{
    if (m_watches.isEmpty())
        return;
    m_watches.clear();
    emit watchesChanged();
}

void CanvasScene::setWatches(const QVector<Watch> &watches)
{
    m_watches = watches;
    emit watchesChanged();
}

QVariant CanvasScene::readSceneValue(const QString &objectName, const QString &key) const
{
    // A key may be namespaced ("shape.rotation") or bare ("motorSpeed", as the
    // engine names it). Bare keys are resolved against whichever object bears
    // the name, so the log does not care which table a row came from.
    const int dot = key.indexOf(QLatin1Char('.'));
    const QString what = dot < 0 ? key : key.mid(dot + 1);
    const bool anyKind = dot < 0;

    if (anyKind) {
        for (Joint *joint : m_joints) {
            if (joint->name() != objectName)
                continue;
            const auto it = joint->params().constFind(what);
            if (it != joint->params().constEnd())
                return *it;
        }
    }

    if (anyKind || key.startsWith(QLatin1String("shape."))) {
        for (ShapeItem *shape : shapes()) {
            if (shape->name() != objectName)
                continue;
            if (what == QLatin1String("x"))         return shape->pos().x() + shape->rect().x();
            if (what == QLatin1String("y"))         return shape->pos().y() + shape->rect().y();
            if (what == QLatin1String("width"))     return shape->rect().width();
            if (what == QLatin1String("height"))    return shape->rect().height();
            if (what == QLatin1String("rotation"))  return shape->rotation();
            if (what == QLatin1String("originX"))   return shape->origin().x();
            if (what == QLatin1String("originY"))   return shape->origin().y();
            if (what == QLatin1String("borderWidth")) return shape->borderWidth();
            const physics::ShapePart &part = shape->part();
            if (what == QLatin1String("density"))     return part.density;
            if (what == QLatin1String("friction"))    return part.material.friction;
            if (what == QLatin1String("restitution")) return part.material.restitution;
            if (what == QLatin1String("groupIndex"))  return part.filter.groupIndex;
            if (what == QLatin1String("isSensor"))    return part.isSensor;
            if (!anyKind)
                return {};
            break;
        }
    }

    if (anyKind || key.startsWith(QLatin1String("body."))) {
        for (PhysicsBody *body : m_bodies) {
            if (body->name() != objectName)
                continue;
            const physics::BodyDesc &props = body->props();
            if (what == QLatin1String("angle"))      return body->rotationDegrees();
            if (what == QLatin1String("positionX"))  return body->originScenePos().x();
            if (what == QLatin1String("positionY"))  return body->originScenePos().y();
            if (what == QLatin1String("isEnabled"))  return props.isEnabled;
            if (what == QLatin1String("gravityScale")) return props.gravityScale;
            if (what == QLatin1String("linearDamping")) return props.linearDamping;
            if (what == QLatin1String("angularDamping")) return props.angularDamping;
            if (what == QLatin1String("fixedRotation")) return props.fixedRotation;
            if (what == QLatin1String("velocityX"))  return props.linearVelocity.x();
            if (what == QLatin1String("velocityY"))  return props.linearVelocity.y();
            if (what == QLatin1String("angularVelocity")) return props.angularVelocityDegrees;
            if (!anyKind)
                return {};
            break;
        }
    }

    if (key.startsWith(QLatin1String("joint."))) {
        for (Joint *joint : m_joints) {
            if (joint->name() != objectName)
                continue;
            const auto it = joint->params().constFind(what);
            return it == joint->params().constEnd() ? QVariant() : *it;
        }
    }
    return {};
}

void CanvasScene::setDebugView(bool on)
{
    if (m_debugView == on)
        return;
    m_debugView = on;
    for (ShapeItem *shape : shapes())
        shape->update();
    update();
}

void CanvasScene::setShowBodyAxes(bool show)
{
    if (m_showBodyAxes == show)
        return;
    m_showBodyAxes = show;
    update();
}

void CanvasScene::setBodyAxisLength(qreal length)
{
    length = qMax(1.0, length);
    if (qFuzzyCompare(m_bodyAxisLength, length))
        return;
    m_bodyAxisLength = length;
    update();
}

void CanvasScene::setBodyAxisWidth(qreal width)
{
    width = qMax(0.0, width);
    if (qFuzzyCompare(m_bodyAxisWidth, width))
        return;
    m_bodyAxisWidth = width;
    update();
}

void CanvasScene::setBodyAxisXColor(const QColor &color)
{
    if (m_bodyAxisXColor == color)
        return;
    m_bodyAxisXColor = color;
    update();
}

void CanvasScene::setBodyAxisYColor(const QColor &color)
{
    if (m_bodyAxisYColor == color)
        return;
    m_bodyAxisYColor = color;
    update();
}

void CanvasScene::setMaxPolygonVertices(int count)
{
    count = qBound(3, count, 64);
    if (m_maxPolygonVertices == count)
        return;
    m_maxPolygonVertices = count;
}

QStringList CanvasScene::solidBodyProblems(const QVector<ShapeItem *> &shapes) const
{
    QStringList problems;
    for (ShapeItem *shape : shapes) {
        const physics::Geometry geometry = shape->physicsGeometry();

        if (geometry.kind == physics::GeometryKind::Chain) {
            problems << tr("%1 is an open polyline, which encloses no area").arg(shape->name());
        } else if (geometry.kind == physics::GeometryKind::Polygon) {
            if (geometry.points.size() > m_maxPolygonVertices) {
                problems << tr("%1 has %2 points, over the %3 allowed for a solid shape")
                                .arg(shape->name()).arg(geometry.points.size()).arg(m_maxPolygonVertices);
            } else if (!physics::isConvex(geometry.points)) {
                problems << tr("%1 is concave").arg(shape->name());
            }
        }
    }
    return problems;
}

void CanvasScene::setSleepShiftPercent(int percent)
{
    percent = qBound(0, percent, 90);
    if (m_sleepShiftPercent == percent)
        return;
    m_sleepShiftPercent = percent;
    update();
}

void CanvasScene::setSimulationRunning(bool running)
{
    if (m_simulationRunning == running)
        return;
    m_simulationRunning = running;
    update();
    emit simulationRunningChanged(running);
}

bool CanvasScene::selectionIsWholeBody() const
{
    PhysicsBody *body = commonSelectedBody();
    return body && body->shapes().size() == m_physicsSelection.size();
}

void CanvasScene::setGravity(const QPointF &gravity)
{
    if (m_world.gravity == gravity)
        return;
    m_world.gravity = gravity;
    emit fieldPropertyChanged();
}

void CanvasScene::setPixelsPerMeter(qreal pixelsPerMeter)
{
    pixelsPerMeter = qMax(1.0, pixelsPerMeter);
    if (qFuzzyCompare(m_world.pixelsPerMeter, pixelsPerMeter))
        return;
    m_world.pixelsPerMeter = pixelsPerMeter;
    emit fieldPropertyChanged();
}

void CanvasScene::setFieldBoundsSolid(bool solid)
{
    if (m_fieldBoundsSolid == solid)
        return;
    m_fieldBoundsSolid = solid;
    emit fieldPropertyChanged();
}

physics::WorldDesc CanvasScene::toWorldDesc() const
{
    return m_world;
}

void CanvasScene::setShowGrid(bool show)
{
    m_showGrid = show;
    update();
    emit fieldPropertyChanged();
}

void CanvasScene::setGridCellSize(qreal size)
{
    m_gridCellSize = qMax(1.0, size);
    update();
    emit fieldPropertyChanged();
}

void CanvasScene::setGridColor(const QColor &color)
{
    m_gridColor = color;
    update();
    emit fieldPropertyChanged();
}

void CanvasScene::setBackgroundColor(const QColor &color)
{
    m_backgroundColor = color;
    update();
    emit fieldPropertyChanged();
}

void CanvasScene::setSnapToGrid(bool snap)
{
    m_snapToGrid = snap;
}

void CanvasScene::setSnapPoint(SnapPoint point)
{
    m_snapPoint = point;
}

void CanvasScene::setSnapStep(qreal step)
{
    m_snapStep = qMax(1.0, step);
    m_snapSensitivity = qMin(m_snapSensitivity, m_snapStep / 2.0);
}

void CanvasScene::setSnapSensitivity(qreal sensitivity)
{
    m_snapSensitivity = qBound(0.0, sensitivity, m_snapStep / 2.0);
}

void CanvasScene::setDefaultBorderColor(const QColor &color)
{
    m_defaultBorderColor = color;
}

void CanvasScene::setDefaultBorderWidth(qreal width)
{
    m_defaultBorderWidth = qMax(0.0, width);
}

void CanvasScene::setDefaultBodyColor(const QColor &color)
{
    m_defaultBodyColor = color;
}

void CanvasScene::setSelectionLineStyle(Qt::PenStyle style)
{
    m_selectionLineStyle = style;
    update();
}

void CanvasScene::setSelectionLineWidth(qreal width)
{
    m_selectionLineWidth = qMax(0.5, width);
    update();
}

void CanvasScene::setSelectionColor(const QColor &color)
{
    m_selectionColor = color;
    update();
}

void CanvasScene::setHandleShape(HandleShape shape)
{
    m_handleShape = shape;
    update();
}

void CanvasScene::setHandleSize(qreal size)
{
    m_handleSize = qMax(2.0, size);
    update();
}

void CanvasScene::setHandleColor(const QColor &color)
{
    m_handleColor = color;
    update();
}

void CanvasScene::setHandleBorderWidth(qreal width)
{
    m_handleBorderWidth = qMax(0.0, width);
    update();
}

void CanvasScene::setHandleBorderColor(const QColor &color)
{
    m_handleBorderColor = color;
    update();
}

void CanvasScene::setCurrentScale(qreal scale)
{
    scale = qBound(m_scaleMin, scale, m_scaleMax);
    if (qFuzzyCompare(m_currentScale, scale))
        return;
    m_currentScale = scale;
    emit scaleChanged(m_currentScale);
    emit fieldPropertyChanged();
}

void CanvasScene::setScaleMin(qreal value)
{
    m_scaleMin = qMax(1.0, value);
    if (m_scaleMin > m_scaleMax)
        m_scaleMax = m_scaleMin;
    setCurrentScale(m_currentScale); // re-clamp against the new bound
}

void CanvasScene::setScaleMax(qreal value)
{
    m_scaleMax = qMax(1.0, value);
    if (m_scaleMax < m_scaleMin)
        m_scaleMin = m_scaleMax;
    setCurrentScale(m_currentScale); // re-clamp against the new bound
}

void CanvasScene::setScaleStep(qreal value)
{
    m_scaleStep = qMax(0.1, value);
}

void CanvasScene::drawBackground(QPainter *painter, const QRectF &rect)
{
    QColor background = m_backgroundColor;
    if (m_editorMode != EditorMode::Edit) {
        const QColor accent = EditorModes::accent(m_editorMode);
        constexpr qreal kTint = 0.06;
        background = QColor::fromRgbF(
            background.redF()   * (1.0 - kTint) + accent.redF()   * kTint,
            background.greenF() * (1.0 - kTint) + accent.greenF() * kTint,
            background.blueF()  * (1.0 - kTint) + accent.blueF()  * kTint,
            background.alphaF());
    }
    painter->fillRect(rect, background);

    if (!m_showGrid)
        return;

    const QRectF gridRect = rect.intersected(sceneRect());
    if (gridRect.isEmpty())
        return;

    const qreal minorStep = m_gridCellSize;
    const qreal majorStep = m_gridCellSize * 5.0;

    QPen minorPen(m_gridColor);
    minorPen.setWidth(0);
    QPen majorPen(m_gridColor.darker(130));
    majorPen.setWidth(0);

    const qreal left = std::floor(gridRect.left() / minorStep) * minorStep;
    const qreal top = std::floor(gridRect.top() / minorStep) * minorStep;

    painter->setPen(minorPen);
    for (qreal x = left; x < gridRect.right(); x += minorStep) {
        if (!qFuzzyIsNull(std::fmod(x, majorStep)))
            painter->drawLine(QPointF(x, gridRect.top()), QPointF(x, gridRect.bottom()));
    }
    for (qreal y = top; y < gridRect.bottom(); y += minorStep) {
        if (!qFuzzyIsNull(std::fmod(y, majorStep)))
            painter->drawLine(QPointF(gridRect.left(), y), QPointF(gridRect.right(), y));
    }

    painter->setPen(majorPen);
    for (qreal x = std::floor(gridRect.left() / majorStep) * majorStep; x < gridRect.right(); x += majorStep)
        painter->drawLine(QPointF(x, gridRect.top()), QPointF(x, gridRect.bottom()));
    for (qreal y = std::floor(gridRect.top() / majorStep) * majorStep; y < gridRect.bottom(); y += majorStep)
        painter->drawLine(QPointF(gridRect.left(), y), QPointF(gridRect.right(), y));

    QPen axisPen(m_gridColor.darker(180));
    axisPen.setWidth(0);
    painter->setPen(axisPen);
    painter->drawLine(QPointF(gridRect.left(), 0), QPointF(gridRect.right(), 0));
    painter->drawLine(QPointF(0, gridRect.top()), QPointF(0, gridRect.bottom()));
}

// Matches the origin marker a single shape draws while it is being rotated.
static constexpr qreal kGroupOriginRadius = 7.0;

void CanvasScene::clearEditSelection()
{
    if (m_editSelection.isEmpty())
        return;
    for (ShapeItem *shape : std::as_const(m_editSelection))
        shape->setCoSelected(false);
    m_editSelection.clear();
    m_groupOriginPlaced = false;
    emit editSelectionChanged();
}

void CanvasScene::addToEditSelection(ShapeItem *shape)
{
    if (!shape || shape->scene() != this || m_editorMode != EditorMode::Edit)
        return;

    if (!m_active) {
        activate(shape);
        return;
    }

    if (shape == m_active) {
        // Dropping the shape that carries the handles hands the role to the
        // next one picked, so the rest of the group stays selected instead of
        // falling away with it.
        QVector<ShapeItem *> rest = m_editSelection;
        ShapeItem *promoted = rest.isEmpty() ? nullptr : rest.takeFirst();
        clearEditSelection();
        if (!promoted) {
            deactivate();
            return;
        }
        activate(promoted);
        for (ShapeItem *other : std::as_const(rest)) {
            m_editSelection.append(other);
            other->setCoSelected(true);
        }
        m_groupOriginPlaced = false;
        refreshGroupOrigin();
        emit editSelectionChanged();
        return;
    }

    if (m_editSelection.removeOne(shape)) {
        shape->setCoSelected(false);
    } else {
        m_editSelection.append(shape);
        shape->setCoSelected(true);
    }
    m_groupOriginPlaced = false;
    refreshGroupOrigin();
    emit editSelectionChanged();
}

void CanvasScene::setEditSelectionOrigin(const QPointF &scenePos)
{
    m_groupOrigin = scenePos;
    m_groupOriginPlaced = true;
    update();
}

void CanvasScene::refreshGroupOrigin()
{
    if (m_editSelection.isEmpty() || !m_active) {
        m_groupOriginPlaced = false;
        return;
    }
    if (m_groupOriginPlaced)
        return;

    // The centre of everything picked, which is where a group is expected to
    // turn about until it is told otherwise.
    QRectF bounds = m_active->sceneBoundingRect();
    for (ShapeItem *shape : std::as_const(m_editSelection))
        bounds = bounds.united(shape->sceneBoundingRect());
    m_groupOrigin = bounds.center();
    update();
}

QVector<ShapeItem *> CanvasScene::selectedShapes() const
{
    QVector<ShapeItem *> all;
    if (m_active)
        all.append(m_active);
    all.append(m_editSelection);
    return all;
}

QRectF CanvasScene::editSelectionBounds() const
{
    if (!m_active || m_editSelection.isEmpty())
        return QRectF();
    QRectF bounds = m_active->mapToScene(m_active->rect()).boundingRect();
    for (ShapeItem *shape : std::as_const(m_editSelection))
        bounds = bounds.united(shape->mapToScene(shape->rect()).boundingRect());
    return bounds;
}

QRectF CanvasScene::editSelectionBox() const
{
    const QRectF bounds = editSelectionBounds();
    if (bounds.isNull())
        return bounds;
    constexpr qreal kClearance = 6.0;
    return bounds.adjusted(-kClearance, -kClearance, kClearance, kClearance);
}

bool CanvasScene::editSelectionRotating() const
{
    return m_active && !m_editSelection.isEmpty()
           && m_active->mode() == ShapeMode::Rotating;
}

QVector<QPointF> CanvasScene::groupHandlePoints() const
{
    const QRectF b = editSelectionBounds();
    if (b.isNull())
        return {};
    // Corners only. A group scales by one factor -- squashing it along one axis
    // cannot be expressed as a rect and an angle once a member is turned.
    return { b.topLeft(), b.topRight(), b.bottomRight(), b.bottomLeft() };
}

int CanvasScene::groupHandleAt(const QPointF &scenePos) const
{
    if (editSelectionRotating())
        return -1;
    const QVector<QPointF> corners = groupHandlePoints();
    const qreal reach = handleSize();
    for (int i = 0; i < corners.size(); ++i)
        if (QLineF(corners.at(i), scenePos).length() <= reach)
            return i;
    return -1;
}

void CanvasScene::beginGroupScale(int handle)
{
    const QVector<QPointF> corners = groupHandlePoints();
    if (handle < 0 || handle >= corners.size())
        return;

    // The corner across the box stays put, so the group grows away from it.
    m_groupScaleAnchor = corners.at((handle + 2) % 4);
    m_groupScaleStart = corners.at(handle);

    m_groupScaleStartRects.clear();
    m_groupScaleStartOrigins.clear();
    m_groupScaleStartOriginScene.clear();
    for (ShapeItem *shape : selectedShapes()) {
        m_groupScaleStartRects.append(shape->rect());
        m_groupScaleStartOrigins.append(shape->origin());
        m_groupScaleStartOriginScene.append(shape->pos() + shape->origin());
    }
}

void CanvasScene::applyGroupScale(qreal factor)
{
    const QVector<ShapeItem *> shapes = selectedShapes();
    if (shapes.size() != m_groupScaleStartRects.size())
        return;

    for (int i = 0; i < shapes.size(); ++i) {
        ShapeItem *shape = shapes.at(i);
        const QRectF r = m_groupScaleStartRects.at(i);
        const QPointF origin = m_groupScaleStartOrigins.at(i) * factor;
        // A shape scales about its own local zero and then moves so that its
        // origin sits where the group scale puts it. Rotation is untouched,
        // which is what keeps a turned shape from shearing.
        shape->applyRect(QRectF(r.topLeft() * factor, r.size() * factor));
        shape->setOrigin(origin);
        shape->setPos(m_groupScaleAnchor
                      + (m_groupScaleStartOriginScene.at(i) - m_groupScaleAnchor) * factor
                      - origin);
    }
    m_groupOriginPlaced = false;
    refreshGroupOrigin();
    update();
}

bool CanvasScene::groupOriginHandleContains(const QPointF &scenePos) const
{
    if (m_editSelection.isEmpty())
        return false;
    return QLineF(m_groupOrigin, scenePos).length() <= kGroupOriginRadius + 3.0;
}

void CanvasScene::beginGroupDrag()
{
    m_groupLeadStart = m_active ? m_active->pos() : QPointF();
    m_groupLeadStartRotation = m_active ? m_active->rotation() : 0.0;
    m_groupStartPositions.clear();
    m_groupStartRotations.clear();
    for (ShapeItem *shape : std::as_const(m_editSelection)) {
        m_groupStartPositions.append(shape->pos());
        m_groupStartRotations.append(shape->rotation());
    }
}

void CanvasScene::activate(ShapeItem *item)
{
    if (m_active == item)
        return;
    // Picking a different shape outright starts a new selection; Shift and
    // Ctrl go through addToEditSelection instead.
    clearEditSelection();
    if (m_active) {
        m_active->setMode(ShapeMode::Idle);
        m_active->setSelectedNodes({});
    }
    m_selectedNodes.clear();
    m_active = item;
    if (m_active)
        m_active->setMode(ShapeMode::Selected);
    emit activeItemChanged(m_active);
}

void CanvasScene::deactivate()
{
    clearEditSelection();
    if (!m_active)
        return;
    m_active->setMode(ShapeMode::Idle);
    m_active->setSelectedNodes({});
    m_selectedNodes.clear();
    m_active = nullptr;
    emit activeItemChanged(nullptr);
}

void CanvasScene::setNodeSelection(const QSet<int> &indices)
{
    m_selectedNodes = indices;
    if (m_active)
        m_active->setSelectedNodes(m_selectedNodes);
}

QPointF CanvasScene::snapScenePoint(const QPointF &scenePoint) const
{
    if (!m_snapToGrid || m_snapSuspended)
        return scenePoint;

    QPointF snapped = scenePoint;

    const qreal roundedX = qRound(scenePoint.x() / m_snapStep) * m_snapStep;
    if (qAbs(roundedX - scenePoint.x()) <= m_snapSensitivity)
        snapped.setX(roundedX);

    const qreal roundedY = qRound(scenePoint.y() / m_snapStep) * m_snapStep;
    if (qAbs(roundedY - scenePoint.y()) <= m_snapSensitivity)
        snapped.setY(roundedY);

    return snapped;
}

void CanvasScene::switchActiveToSelected()
{
    if (m_active && m_active->mode() != ShapeMode::Selected) {
        m_active->setMode(ShapeMode::Selected);
        setNodeSelection({});
        emit activeItemChanged(m_active);
    }
}

void CanvasScene::selectShape(ShapeItem *shape)
{
    if (!shape || shape->scene() != this)
        return;
    activate(shape);
}

void CanvasScene::switchActiveToEditing()
{
    if (!geometryEditingAllowed())
        return;
    if (m_active && m_active->supportsNodeEditing() && m_active->mode() != ShapeMode::Editing) {
        m_active->setMode(ShapeMode::Editing);
        emit activeItemChanged(m_active);
    }
}

void CanvasScene::switchActiveToRotating()
{
    if (!geometryEditingAllowed())
        return;
    if (m_active && m_active->mode() != ShapeMode::Rotating) {
        m_active->setMode(ShapeMode::Rotating);
        setNodeSelection({});
        emit activeItemChanged(m_active);
    }
}

void CanvasScene::deleteActiveItem()
{
    if (!m_active)
        return;

    if (m_active->mode() == ShapeMode::Editing && !m_selectedNodes.isEmpty()) {
        QList<int> indices(m_selectedNodes.begin(), m_selectedNodes.end());
        std::sort(indices.begin(), indices.end(), std::greater<int>());
        for (int index : indices)
            m_active->deleteNode(index);
        setNodeSelection({});
        return;
    }

    // Everything picked goes, not only the shape carrying the handles.
    QVector<ShapeItem *> doomed = m_editSelection;
    doomed.prepend(m_active);
    m_editSelection.clear();

    const QString name = m_active->name(); // read before the shape is destroyed
    const int count = int(doomed.size());
    m_active = nullptr;
    m_dragMode = DragMode::None;
    m_groupClickCandidate = nullptr;
    m_selectedNodes.clear();

    bool wasPicked = false;
    for (ShapeItem *item : std::as_const(doomed)) {
        wasPicked = m_physicsSelection.removeOne(item) || wasPicked;
        removeItem(item);
        delete item;
    }

    emit activeItemChanged(nullptr);
    emit editSelectionChanged();
    emit shapesChanged();
    if (wasPicked)
        emit physicsSelectionChanged();
    notifyEdit(count > 1 ? tr("Delete %n shapes", nullptr, count)
                         : tr("Delete %1").arg(name));
}

void CanvasScene::startPolygonDrawing()
{
    if (!geometryEditingAllowed())
        return;
    deactivate();
    m_dragMode = DragMode::None;
    m_polygonDrawing = true;
    m_polygonScenePoints.clear();
    emit polygonDrawingChanged(true);
    update();
}

void CanvasScene::finishPolygonDrawing(bool closed)
{
    if (m_polygonScenePoints.size() >= 2) {
        const QRectF bounds = m_polygonScenePoints.boundingRect();
        const QPointF itemPos = bounds.topLeft();

        QPolygonF localPoints;
        localPoints.reserve(m_polygonScenePoints.size());
        for (const QPointF &p : std::as_const(m_polygonScenePoints))
            localPoints << (p - itemPos);

        auto *item = new PolygonItem(localPoints, closed);
        applyDefaultStyle(item, m_defaultBorderColor, m_defaultBorderWidth, m_defaultBodyColor);
        item->setPos(itemPos);
        addItem(item);
        item->setName(uniqueName(item->name(), item));
        emit shapesChanged();
        notifyEdit(tr("Add %1").arg(item->name()));
    }

    m_polygonDrawing = false;
    m_polygonScenePoints.clear();
    emit polygonDrawingChanged(false);
    update();
}

void CanvasScene::cancelPolygonDrawing()
{
    m_polygonDrawing = false;
    m_polygonScenePoints.clear();
    emit polygonDrawingChanged(false);
    update();
}

void CanvasScene::drawForeground(QPainter *painter, const QRectF &)
{
    // The pivot a multi-shape selection turns about. It belongs to no single
    // shape, so the scene draws it rather than any item.
    if (editSelectionRotating() && !simulationRunning()) {
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);
        painter->setPen(QPen(QColor(64, 64, 64), 1));
        painter->setBrush(QColor(76, 175, 80));
        painter->drawEllipse(m_groupOrigin, kGroupOriginRadius, kGroupOriginRadius);

        const qreal reach = kGroupOriginRadius + 4.0;
        painter->setPen(QPen(QColor(220, 50, 50), 1.5)); // X axis, red
        painter->drawLine(m_groupOrigin + QPointF(-reach, 0),
                          m_groupOrigin + QPointF(reach, 0));
        painter->setPen(QPen(QColor(40, 160, 60), 1.5)); // Y axis, green
        painter->drawLine(m_groupOrigin + QPointF(0, -reach),
                          m_groupOrigin + QPointF(0, reach));
        painter->restore();
    }

    // Moving or scaling: the box round everything picked, with a handle at each
    // corner. It stands in for the handles a single shape would draw.
    if (!m_editSelection.isEmpty() && m_active && !editSelectionRotating()
        && m_editorMode == EditorMode::Edit && !simulationRunning()) {
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);

        QPen boxPen(m_selectionColor);
        boxPen.setWidthF(1.0);
        boxPen.setStyle(m_selectionLineStyle);
        boxPen.setCosmetic(true);
        painter->setPen(boxPen);
        painter->setBrush(Qt::NoBrush);
        painter->drawRect(editSelectionBox());

        painter->setPen(QPen(m_handleBorderColor, m_handleBorderWidth));
        painter->setBrush(m_handleColor);
        const qreal size = m_handleSize;
        for (const QPointF &corner : groupHandlePoints()) {
            const QRectF r(corner.x() - size / 2, corner.y() - size / 2, size, size);
            if (m_handleShape == HandleShape::Circle)
                painter->drawEllipse(r);
            else
                painter->drawRect(r);
        }
        painter->restore();
    }

    if (m_editorMode != EditorMode::Edit && !m_joints.isEmpty()
        && (!simulationRunning() || m_debugView)) {
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);

        for (Joint *joint : std::as_const(m_joints)) {
            // Broken by a rule: gone from the world, so it is gone from the
            // picture too. It comes back when the run ends.
            if (joint->isBroken())
                continue;
            const bool selected = joint == m_selectedJoint && !simulationRunning();

            const int anchors = joint->anchorCount();
            const QPointF a = anchors > 0 ? joint->anchorScenePos(Joint::End::A)
                                          : joint->bodyA()->centerOfMassScenePos();
            const QPointF b = anchors > 1 ? joint->anchorScenePos(Joint::End::B)
                              : anchors > 0 ? a
                                            : joint->bodyB()->centerOfMassScenePos();

            // A bone: a ring at each anchor, joined by a waisted shaft.
            const qreal ring = m_jointAnchorRadius;
            const qreal waist = m_jointWaistWidth / 2.0;

            QPainterPath bone;
            QPainterPath ringA;
            ringA.addEllipse(a, ring, ring);
            bone = ringA;

            const auto markSelected = [&](const QPainterPath &shape) {
                QPainterPathStroker grow;
                grow.setWidth(m_jointSelectionLineWidth * 2.0 + 4.0);
                grow.setJoinStyle(Qt::RoundJoin);
                grow.setCapStyle(Qt::RoundCap);
                const QPainterPath halo = grow.createStroke(shape).united(shape).simplified();

                QPen selectionPen(m_jointSelectionColor);
                selectionPen.setWidthF(m_jointSelectionLineWidth);
                selectionPen.setStyle(m_jointSelectionLineStyle);
                selectionPen.setCosmetic(true);
                painter->setPen(selectionPen);
                painter->setBrush(Qt::NoBrush);
                painter->drawPath(halo);
            };

            // A sliding joint is the same bone every other joint draws --
            // two anchors joined by a shaft -- with the axis it slides along
            // drawn behind it. Only the axis is special; the anchors and the
            // connection between them are not.
            if (jointVisual(joint->typeId()) == physics::JointVisual::Axis) {
                const QPointF along = joint->axisScene();
                const QPointF across(-along.y(), along.x());

                const QVariantMap &params = joint->params();
                qreal lower = params.value(QStringLiteral("lowerTranslation")).toDouble();
                qreal upper = params.value(QStringLiteral("upperTranslation")).toDouble();
                if (lower > upper)
                    std::swap(lower, upper);
                const bool limited = params.value(QStringLiteral("enableLimit")).toBool()
                    && !qFuzzyCompare(lower, upper);
                if (!limited) {
                    // Unlimited travel has no length to draw, so this is a
                    // direction explosion rather than a measurement -- the same
                    // thing the body axis cross is, and sized the same way,
                    // from a setting rather than from anything in the scene.
                    upper = m_jointAxisLength;
                    lower = -upper;
                }

                const QPointF from = a + along * lower;
                const QPointF to = a + along * upper;

                // Darker than the joint's own colour and as thick as its shaft:
                // the pale type colour at outline width disappears against a
                // body fill, which is exactly what it is drawn on top of.
                const QColor axisColor = jointTypeColor(joint->typeId()).darker(160);
                QPen axisPen(axisColor);
                axisPen.setWidthF(qMax(waist * 2.0, 1.5));
                axisPen.setCapStyle(Qt::FlatCap);
                if (!limited)
                    axisPen.setDashPattern({ 3.0, 2.0 });
                painter->setPen(axisPen);
                painter->setBrush(Qt::NoBrush);
                painter->drawLine(from, to);

                if (limited) {
                    QPen stopPen(axisColor);
                    stopPen.setWidthF(qMax(waist * 2.5, 1.5));
                    stopPen.setCapStyle(Qt::RoundCap);
                    painter->setPen(stopPen);
                    painter->drawLine(from - across * ring, from + across * ring);
                    painter->drawLine(to - across * ring, to + across * ring);
                }

                const qreal head = ring * 1.3;
                const qreal inset = limited ? waist * 2.5 + head * 0.3 : 0.0;
                const auto arrowAt = [&](const QPointF &tip, const QPointF &direction) {
                    const QPointF back = tip - direction * head;
                    const QPointF side(-direction.y() * head * 0.55, direction.x() * head * 0.55);
                    QPolygonF arrow;
                    arrow << tip << back + side << back - side;
                    // Outlined like the anchors, so it stays legible wherever
                    // it happens to land.
                    painter->setPen(QPen(m_jointOutlineColor, m_jointOutlineWidth));
                    painter->setBrush(axisColor);
                    painter->drawPolygon(arrow);
                };
                arrowAt(to - along * inset, along);
                arrowAt(from + along * inset, -along);
            }

            if (anchors == 0) {
                QPainterPath link;
                link.moveTo(a);
                link.lineTo(b);

                QPen linkPen(jointTypeColor(joint->typeId()));
                linkPen.setWidthF(m_jointOutlineWidth);
                linkPen.setStyle(Qt::DashLine);
                painter->setPen(linkPen);
                painter->setBrush(Qt::NoBrush);
                painter->drawPath(link);

                if (selected) {
                    QPainterPathStroker body;
                    body.setWidth(qMax(m_jointOutlineWidth, 1.0));
                    markSelected(body.createStroke(link));
                }
                continue;
            }

            const QLineF span(a, b);
            if (span.length() > 0.01) {
                const QPointF along = (b - a) / span.length();
                const QPointF across(-along.y() * waist, along.x() * waist);

                QPainterPath neck;
                neck.moveTo(a + across);
                neck.lineTo(b + across);
                neck.lineTo(b - across);
                neck.lineTo(a - across);
                neck.closeSubpath();
                bone = bone.united(neck);

                QPainterPath ringB;
                ringB.addEllipse(b, ring, ring);
                bone = bone.united(ringB);
            }

            QPen outlinePen(m_jointOutlineColor);
            outlinePen.setWidthF(m_jointOutlineWidth);
            outlinePen.setJoinStyle(Qt::RoundJoin);
            painter->setPen(outlinePen);
            painter->setBrush(jointTypeColor(joint->typeId()));
            painter->drawPath(bone);

            painter->setBrush(Qt::NoBrush);
            painter->setPen(QPen(m_jointOutlineColor, m_jointOutlineWidth));
            painter->drawEllipse(a, ring * 0.55, ring * 0.55);
            if (span.length() > 0.01)
                painter->drawEllipse(b, ring * 0.55, ring * 0.55);

            if (selected)
                markSelected(bone);
        }
        painter->restore();
    }

    if (m_editorMode == EditorMode::Physics && m_showBodyAxes
        && (!simulationRunning() || m_debugView)) {
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);

        for (PhysicsBody *body : std::as_const(m_bodies)) {
            // Removed by a rule: gone from the world, so its axes go with it.
            // They come back when the run ends.
            if (body->isEmpty() || body->isRemoved())
                continue;

            const QPointF origin = body->centerOfMassScenePos();
            QTransform toScene;
            toScene.translate(origin.x(), origin.y());
            toScene.rotate(body->rotationDegrees());

            const QPointF xTip = toScene.map(QPointF(m_bodyAxisLength, 0.0));
            const QPointF yTip = toScene.map(QPointF(0.0, m_bodyAxisLength));

            QPen axisPen;
            axisPen.setWidthF(m_bodyAxisWidth);
            axisPen.setCosmetic(true);
            axisPen.setCapStyle(Qt::RoundCap);

            axisPen.setColor(m_bodyAxisXColor);
            painter->setPen(axisPen);
            painter->drawLine(origin, xTip);

            axisPen.setColor(m_bodyAxisYColor);
            painter->setPen(axisPen);
            painter->drawLine(origin, yTip);

            painter->setPen(Qt::NoPen);
            painter->setBrush(m_bodyAxisXColor);
            painter->drawEllipse(origin, m_bodyAxisWidth, m_bodyAxisWidth);
        }
        painter->restore();
    }

    if (!m_polygonDrawing || m_polygonScenePoints.isEmpty())
        return;

    painter->setRenderHint(QPainter::Antialiasing, true);

    QPen linePen(QColor(80, 130, 220));
    linePen.setWidth(0);
    painter->setPen(linePen);
    painter->drawPolyline(m_polygonScenePoints);

    QPen previewPen(QColor(150, 170, 210));
    previewPen.setStyle(Qt::DashLine);
    previewPen.setWidth(0);
    painter->setPen(previewPen);
    painter->drawLine(m_polygonScenePoints.last(), m_polygonCursorScenePos);

    painter->setPen(QPen(QColor(40, 70, 160), 1));
    painter->setBrush(QColor(80, 130, 220));
    constexpr qreal kPointRadius = 4.0;
    for (const QPointF &p : std::as_const(m_polygonScenePoints))
        painter->drawEllipse(p, kPointRadius, kPointRadius);
}

void CanvasScene::keyPressEvent(QKeyEvent *event)
{
    if (m_polygonDrawing) {
        if (event->key() == Qt::Key_Escape) {
            cancelPolygonDrawing();
            event->accept();
            return;
        }
        if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
            finishPolygonDrawing(event->modifiers() & Qt::ControlModifier);
            event->accept();
            return;
        }
        event->accept();
        return;
    }

    if (m_active && m_active->mode() == ShapeMode::Editing
        && (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter)) {
        handleEditModeEnter();
        event->accept();
        return;
    }

    QGraphicsScene::keyPressEvent(event);
}

void CanvasScene::handleEditModeEnter()
{
    if (!m_active || m_selectedNodes.size() != 2)
        return;

    QList<int> indices(m_selectedNodes.begin(), m_selectedNodes.end());
    std::sort(indices.begin(), indices.end());
    const int i = indices[0];
    const int j = indices[1];
    const int count = m_active->nodeCount();

    const bool linearAdjacent = (j == i + 1);
    const bool wrapAdjacent = m_active->isClosed() && i == 0 && j == count - 1;
    const bool openEndpoints = !m_active->isClosed() && i == 0 && j == count - 1 && !linearAdjacent;

    if (openEndpoints)
        m_active->closeShape();
    else if (linearAdjacent || wrapAdjacent)
        m_active->insertNodeBetween(i, j);
    else
        return;

    setNodeSelection({});
}

void CanvasScene::mousePressEvent(QGraphicsSceneMouseEvent *event)
{
    m_snapSuspended = event->modifiers().testFlag(Qt::ShiftModifier);

    if (m_polygonDrawing) {
        if (event->button() == Qt::LeftButton) {
            m_polygonScenePoints << event->scenePos();
            update();
        }
        return;
    }

    if (event->button() != Qt::LeftButton) {
        QGraphicsScene::mousePressEvent(event);
        return;
    }

    const QPointF scenePos = event->scenePos();

    if (!selectionAllowed()) {
        // A run blocks selection, not navigation: without this the field
        // freezes exactly when there is something moving to follow across it.
        if (event->button() == Qt::LeftButton) {
            m_dragMode = DragMode::PanField;
            m_panLastScreenPos = event->screenPos();
        }
        event->accept();
        return;
    }

    if (m_editorMode == EditorMode::Physics) {
        int end = 0;
        if (Joint *joint = jointAt(scenePos, &end)) {
            selectJoint(joint);
            if (end != kJointShaft) {
                m_draggedJoint = joint;
                m_draggedJointEnd = end;
            }
            event->accept();
            return;
        }
        selectJoint(nullptr);
    }

    if (m_editorMode != EditorMode::Edit) {
        // A ray or explosion sits above the shapes and is picked first, so
        // one dropped on top of something can still be grabbed.
        for (QGraphicsItem *candidate : items(scenePos)) {
            auto *ray = qgraphicsitem_cast<RayItem *>(candidate);
            if (!ray)
                continue;
            selectRay(ray);
            if (event->button() == Qt::LeftButton) {
                m_bodyDragShapes.clear();
                m_bodyDragStartPositions.clear();
                m_draggedRay = ray;
                m_dragMode = DragMode::MoveBody;
                m_lastScenePos = scenePos;
                m_moveDragVirtualPos = ray->pos();
                m_bodyDragStartPositions.append(ray->pos());
                m_bodyDragLabel = ray->name();
            }
            event->accept();
            return;
        }
        selectRay(nullptr);

        for (QGraphicsItem *candidate : items(scenePos)) {
            auto *explosion = qgraphicsitem_cast<ExplosionItem *>(candidate);
            if (!explosion)
                continue;
            selectExplosion(explosion);
            if (event->button() == Qt::LeftButton) {
                m_bodyDragShapes.clear();
                m_bodyDragStartPositions.clear();
                m_draggedExplosion = explosion;
                m_dragMode = DragMode::MoveBody;
                m_lastScenePos = scenePos;
                m_moveDragVirtualPos = explosion->pos();
                m_bodyDragStartPositions.append(explosion->pos());
                m_bodyDragLabel = explosion->name();
            }
            event->accept();
            return;
        }
        selectExplosion(nullptr);

        ShapeItem *hit = nullptr;
        for (QGraphicsItem *candidate : items(scenePos)) {
            if (auto *shape = qgraphicsitem_cast<ShapeItem *>(candidate)) {
                hit = shape;
                break;
            }
        }
        selectForPhysics(hit, event->modifiers().testFlag(Qt::ControlModifier));
        // Dragging empty field pans it, the same as in Edit mode. Without this
        // a click outside every shape only cleared the selection, so the field
        // could not be moved at all once out of Edit mode.
        if (!hit) {
            m_dragMode = DragMode::PanField;
            m_panLastScreenPos = event->screenPos();
        } else if (event->button() == Qt::LeftButton) {
            // Dragging a shape moves it, as in Edit mode -- but a whole body at
            // once, since moving one shape out of a body would deform it.
            m_bodyDragShapes.clear();
            m_bodyDragStartPositions.clear();
            if (PhysicsBody *body = hit->body()) {
                m_bodyDragShapes = body->shapes();
                m_bodyDragLabel = body->name();
            } else {
                m_bodyDragShapes = { hit };
                m_bodyDragLabel = hit->name();
            }
            for (ShapeItem *shape : std::as_const(m_bodyDragShapes))
                m_bodyDragStartPositions.append(shape->pos());
            m_dragMode = DragMode::MoveBody;
            m_lastScenePos = scenePos;
            // Tracked against the leading piece, since that is what the offset
            // is measured from -- the clicked shape may not be that one.
            m_moveDragVirtualPos = m_bodyDragStartPositions.value(0);
        }
        event->accept();
        return;
    }

    if (m_active) {
        const QPointF local = m_active->mapFromScene(scenePos);

        // Shift or Ctrl adds a shape to the selection instead of replacing it.
        // Node editing already spends Ctrl on picking vertices, so it is left
        // alone there.
        if ((event->modifiers().testFlag(Qt::ShiftModifier)
             || event->modifiers().testFlag(Qt::ControlModifier))
            && m_active->mode() != ShapeMode::Editing) {
            ShapeItem *picked = nullptr;
            for (QGraphicsItem *candidate : items(scenePos)) {
                if (auto *shape = qgraphicsitem_cast<ShapeItem *>(candidate)) {
                    picked = shape;
                    break;
                }
            }
            if (picked) {
                addToEditSelection(picked);
                event->accept();
                return;
            }
        }

        // The group's own handles sit on top of the shapes, so they are tested
        // before anything under them.
        const int corner = groupHandleAt(scenePos);
        if (corner >= 0) {
            m_dragMode = DragMode::GroupScale;
            beginGroupScale(corner);
            event->accept();
            return;
        }

        // The pivot sits on top of the shapes too, for the same reason.
        if (groupOriginHandleContains(scenePos)) {
            m_dragMode = DragMode::GroupOrigin;
            event->accept();
            return;
        }

        // A press inside any of the picked shapes drags or turns the set, so a
        // group can be grabbed anywhere in it and not only by its lead.
        m_pressScenePos = scenePos;
        m_groupClickCandidate = nullptr;
        const auto pressedInGroup = [&] {
            if (m_editSelection.isEmpty())
                return false;
            if (m_active->shapeContains(local)) {
                m_groupClickCandidate = m_active;
                return true;
            }
            for (ShapeItem *other : std::as_const(m_editSelection)) {
                if (other->shapeContains(other->mapFromScene(scenePos))) {
                    m_groupClickCandidate = other;
                    return true;
                }
            }
            return false;
        };

        if (m_active->mode() == ShapeMode::Rotating) {
            if (m_active->originHandleContains(local)) {
                m_dragMode = DragMode::Origin;
                return;
            }
            if (m_active->shapeContains(local) || pressedInGroup()) {
                m_dragMode = DragMode::Rotate;
                // A group turns about its own pivot; a single shape about its
                // origin handle.
                m_dragOriginScene = m_editSelection.isEmpty()
                                        ? m_active->mapToScene(m_active->origin())
                                        : m_groupOrigin;
                m_rotateStartAngle = angleAt(m_dragOriginScene, scenePos);
                m_itemStartRotation = m_active->rotation();
                beginGroupDrag();
                return;
            }
        } else if (m_active->mode() == ShapeMode::Editing) {
            const int node = m_active->nodeAt(local);
            if (node >= 0) {
                if (event->modifiers() & Qt::ControlModifier) {
                    QSet<int> selection = m_selectedNodes;
                    if (selection.contains(node))
                        selection.remove(node);
                    else
                        selection.insert(node);
                    setNodeSelection(selection);
                    return;
                }
                if (!m_selectedNodes.contains(node))
                    setNodeSelection({node});

                m_dragMode = DragMode::EditNode;
                m_editNodeIndex = node;
                m_editDragNodeStart.clear();
                for (int idx : std::as_const(m_selectedNodes))
                    m_editDragNodeStart[idx] = m_active->nodePosition(idx);
                return;
            }
            if (m_active->shapeContains(local)) {
                setNodeSelection({});
                return;
            }
        } else if (m_active->mode() == ShapeMode::Selected) {
            const HandleId handle = geometryEditingAllowed() ? m_active->handleAt(local)
                                                             : HandleId::None;
            if (handle != HandleId::None) {
                m_dragMode = DragMode::Scale;
                m_activeHandle = handle;
                return;
            }
            if (m_active->shapeContains(local) || pressedInGroup()) {
                m_dragMode = DragMode::Move;
                m_lastScenePos = scenePos;
                m_moveDragVirtualPos = m_active->pos();
                beginGroupDrag();
                return;
            }
        }

        deactivate();
    }

    QGraphicsItem *hit = itemAt(scenePos, QTransform());
    if (auto *shape = qgraphicsitem_cast<ShapeItem *>(hit)) {
        activate(shape);
        m_dragMode = DragMode::Move;
        m_lastScenePos = scenePos;
        m_moveDragVirtualPos = shape->pos();
        beginGroupDrag();
        return;
    }

    m_dragMode = DragMode::PanField;
    m_panLastScreenPos = event->screenPos();
}

void CanvasScene::mouseMoveEvent(QGraphicsSceneMouseEvent *event)
{
    m_snapSuspended = event->modifiers().testFlag(Qt::ShiftModifier);

    if (m_draggedJoint) {
        m_draggedJoint->setAnchorScenePos(
            m_draggedJointEnd == 0 ? Joint::End::A : Joint::End::B,
            snapScenePoint(event->scenePos()));
        event->accept();
        return;
    }

    if (m_polygonDrawing) {
        m_polygonCursorScenePos = event->scenePos();
        if (!m_polygonScenePoints.isEmpty())
            update();
        return;
    }

    if (m_dragMode == DragMode::PanField) {
        const QPoint delta = event->screenPos() - m_panLastScreenPos;
        m_panLastScreenPos = event->screenPos();
        for (QGraphicsView *view : views()) {
            view->horizontalScrollBar()->setValue(view->horizontalScrollBar()->value() - delta.x());
            view->verticalScrollBar()->setValue(view->verticalScrollBar()->value() - delta.y());
        }
        return;
    }

    // Ahead of the m_active guard below: a Physics-mode drag has no active
    // item, so it would never reach the switch.
    if (m_dragMode == DragMode::MoveBody) {
        const QPointF scenePos = event->scenePos();

        const QPointF delta = scenePos - m_lastScenePos;
        m_moveDragVirtualPos += delta;

        QPointF shift = m_moveDragVirtualPos;
        if (m_snapToGrid && !m_bodyDragShapes.isEmpty()) {
            ShapeItem *lead = m_bodyDragShapes.first();
            const QPointF localReference = (m_snapPoint == SnapPoint::Position)
                                               ? lead->rect().topLeft()
                                               : lead->origin();
            const QPointF referenceScene = shift + localReference;
            shift += snapScenePoint(referenceScene) - referenceScene;
        }

        // Every piece moves by what the leading one moved, so their relative
        // layout -- and the joints anchored to them -- survives the drag.
        const QPointF applied = shift - m_bodyDragStartPositions.value(0);
        if (m_draggedExplosion)
            m_draggedExplosion->setPos(m_bodyDragStartPositions.value(0) + applied);
        if (m_draggedRay)
            m_draggedRay->setPos(m_bodyDragStartPositions.value(0) + applied);
        for (int i = 0; i < m_bodyDragShapes.size(); ++i)
            m_bodyDragShapes[i]->setPos(m_bodyDragStartPositions[i] + applied);

        m_lastScenePos = scenePos;
        update();
        return;
    }

    if (m_dragMode == DragMode::None || !m_active) {
        QGraphicsScene::mouseMoveEvent(event);
        return;
    }

    const QPointF scenePos = event->scenePos();

    switch (m_dragMode) {
    case DragMode::Move: {
        // Accumulate the true, unsnapped drag position from raw mouse deltas.
        const QPointF delta = scenePos - m_lastScenePos;
        m_moveDragVirtualPos += delta;

        QPointF displayPos = m_moveDragVirtualPos;
        if (m_snapToGrid) {
            const QPointF localReference =
                (m_snapPoint == SnapPoint::Position) ? m_active->rect().topLeft() : m_active->origin();
            const QPointF referenceScene = displayPos + localReference;
            displayPos += snapScenePoint(referenceScene) - referenceScene;
        }
        const QPointF previous = m_active->pos();
        m_active->setPos(displayPos);
        // The rest of the selection moves by exactly what the lead moved, so
        // the spacing within the group survives the drag.
        const QPointF applied = m_active->pos() - m_groupLeadStart;
        for (int i = 0; i < m_editSelection.size()
                        && i < m_groupStartPositions.size(); ++i)
            m_editSelection[i]->setPos(m_groupStartPositions.at(i) + applied);
        // The pivot travels with the shapes it belongs to, wherever it was put.
        if (!m_editSelection.isEmpty()) {
            m_groupOrigin += m_active->pos() - previous;
            update();
        }
        m_lastScenePos = scenePos;
        break;
    }
    case DragMode::Scale: {
        const QPointF local = m_active->mapFromScene(snapScenePoint(scenePos));
        m_active->resizeByHandle(m_activeHandle, local);
        break;
    }
    case DragMode::Rotate: {
        const qreal angle = angleAt(m_dragOriginScene, scenePos);
        const qreal turned = angle - m_rotateStartAngle;

        if (m_editSelection.isEmpty()) {
            m_active->setRotation(m_itemStartRotation + turned);
            break;
        }

        // Every shape gains the same angle and orbits the one pivot, the lead
        // included -- so the group turns as a single piece rather than each
        // shape spinning about an origin of its own.
        QTransform orbit;
        orbit.translate(m_dragOriginScene.x(), m_dragOriginScene.y());
        orbit.rotate(turned);
        orbit.translate(-m_dragOriginScene.x(), -m_dragOriginScene.y());

        const auto turn = [&](ShapeItem *shape, const QPointF &startPos,
                              qreal startRotation) {
            // A shape turns about its own origin, which leaves that origin at
            // pos + origin whatever the rotation -- so the orbit is applied
            // there and the position follows from it.
            const QPointF originScene = startPos + shape->origin();
            shape->setRotation(startRotation + turned);
            shape->setPos(orbit.map(originScene) - shape->origin());
        };

        turn(m_active, m_groupLeadStart, m_groupLeadStartRotation);
        for (int i = 0; i < m_editSelection.size()
                        && i < m_groupStartPositions.size(); ++i)
            turn(m_editSelection.at(i), m_groupStartPositions.at(i),
                 m_groupStartRotations.at(i));
        break;
    }
    case DragMode::GroupScale: {
        const QPointF from = m_groupScaleStart - m_groupScaleAnchor;
        const qreal span = QPointF::dotProduct(from, from);
        if (span > 0.0) {
            // The cursor is read along the box's diagonal, so the group keeps
            // its proportions however the mouse wanders off the line.
            const QPointF to = snapScenePoint(scenePos) - m_groupScaleAnchor;
            applyGroupScale(qMax(0.05, QPointF::dotProduct(to, from) / span));
        }
        break;
    }
    case DragMode::GroupOrigin: {
        setEditSelectionOrigin(snapScenePoint(scenePos));
        break;
    }
    case DragMode::Origin: {
        const QPointF local = m_active->mapFromScene(snapScenePoint(scenePos));
        m_active->setOrigin(local);
        break;
    }
    case DragMode::EditNode: {
        const QPointF grabbedStart = m_editDragNodeStart.value(m_editNodeIndex);
        const QPointF grabbedNew = m_active->mapFromScene(snapScenePoint(scenePos));
        const QPointF delta = grabbedNew - grabbedStart;
        for (auto it = m_editDragNodeStart.constBegin(); it != m_editDragNodeStart.constEnd(); ++it)
            m_active->moveNode(it.key(), it.value() + delta);
        break;
    }
    case DragMode::None:
        break;
    }
}

void CanvasScene::mouseReleaseEvent(QGraphicsSceneMouseEvent *event)
{
    m_snapSuspended = false;

    if (m_draggedJoint) {
        notifyEdit(tr("Move %1 anchor").arg(m_draggedJoint->name()));
        m_draggedJoint = nullptr;
        event->accept();
        return;
    }

    if (m_dragMode != DragMode::None) {
        const DragMode finished = m_dragMode;
        m_dragMode = DragMode::None;

        // Physics-mode drags have no m_active to hang the undo label on, and
        // the joints anchored to what moved need their cached ends refreshed.
        if (finished == DragMode::MoveBody) {
            const QPointF now = m_draggedRay
                                    ? m_draggedRay->pos()
                              : m_draggedExplosion
                                    ? m_draggedExplosion->pos()
                                    : (m_bodyDragShapes.isEmpty()
                                           ? m_bodyDragStartPositions.value(0)
                                           : m_bodyDragShapes.first()->pos());
            const bool moved = now != m_bodyDragStartPositions.value(0);
            m_draggedExplosion = nullptr;
            m_draggedRay = nullptr;
            m_bodyDragShapes.clear();
            m_bodyDragStartPositions.clear();
            if (moved) {
                emit bodiesChanged();
                notifyEdit(tr("Move %1").arg(m_bodyDragLabel));
            }
            update();
            return;
        }

        // A click inside the group -- pressed and released without moving --
        // narrows the selection to the one shape, which is what a click
        // without a modifier means everywhere else.
        if (finished == DragMode::Move && m_groupClickCandidate
            && QLineF(m_pressScenePos, event->scenePos()).length() < 3.0) {
            ShapeItem *only = m_groupClickCandidate;
            m_groupClickCandidate = nullptr;
            clearEditSelection();
            activate(only);
            update();
            return;
        }
        m_groupClickCandidate = nullptr;

        if (m_active)
            m_active->update();
        m_activeHandle = HandleId::None;
        m_editNodeIndex = -1;
        m_editDragNodeStart.clear();
        if (m_active) {
            switch (finished) {
            case DragMode::Move:
                notifyEdit(m_editSelection.isEmpty()
                               ? tr("Move %1").arg(m_active->name())
                               : tr("Move %n shapes", nullptr,
                                    int(m_editSelection.size()) + 1));
                break;
            case DragMode::Rotate:
                notifyEdit(m_editSelection.isEmpty()
                               ? tr("Rotate %1").arg(m_active->name())
                               : tr("Rotate %n shapes", nullptr,
                                    int(m_editSelection.size()) + 1));
                break;
            case DragMode::Origin: notifyEdit(tr("Move %1 origin").arg(m_active->name())); break;
            case DragMode::Scale:  notifyEdit(tr("Resize %1").arg(m_active->name())); break;
            case DragMode::EditNode: notifyEdit(tr("Edit %1").arg(m_active->name())); break;
            case DragMode::GroupScale:
                notifyEdit(tr("Resize %n shapes", nullptr,
                              int(m_editSelection.size()) + 1));
                break;
            case DragMode::GroupOrigin:
            case DragMode::PanField:
            case DragMode::None:     break;
            }
        }
        return;
    }
    QGraphicsScene::mouseReleaseEvent(event);
}

void CanvasScene::mouseDoubleClickEvent(QGraphicsSceneMouseEvent *event)
{
    // Deliberately never forwarded to QGraphicsScene::mouseDoubleClickEvent().
    if (m_editorMode != EditorMode::Edit) {
        if (event->button() == Qt::LeftButton && selectionAllowed()) {
            ShapeItem *hit = nullptr;
            for (QGraphicsItem *candidate : items(event->scenePos())) {
                if (auto *shape = qgraphicsitem_cast<ShapeItem *>(candidate)) {
                    hit = shape;
                    break;
                }
            }
            if (hit && hit->body()) {
                selectJoint(nullptr);
                clearPhysicsSelection();
                selectForPhysics(hit, true);
            } else if (hit) {
                if (!isSelectedForPhysics(hit))
                    selectForPhysics(hit);
                emit createBodyRequested();
            }
        }
        event->accept();
        return;
    }

    if (event->button() == Qt::LeftButton && m_active && geometryEditingAllowed()) {
        const QPointF local = m_active->mapFromScene(event->scenePos());
        if (m_active->shapeContains(local)) {
            if (m_active->supportsNodeEditing()) {
                switch (m_active->mode()) {
                case ShapeMode::Selected:
                    switchActiveToEditing();
                    break;
                case ShapeMode::Editing:
                    switchActiveToRotating();
                    break;
                case ShapeMode::Rotating:
                    switchActiveToSelected();
                    break;
                default:
                    break;
                }
            } else if (m_active->mode() == ShapeMode::Selected) {
                switchActiveToRotating();
            } else if (m_active->mode() == ShapeMode::Rotating) {
                switchActiveToSelected();
            }
        }
    }
    event->accept();
}

void CanvasScene::wheelEvent(QGraphicsSceneWheelEvent *event)
{
    if (event->modifiers() & Qt::ShiftModifier) {
        const qreal notches = event->delta() / 120.0;
        setCurrentScale(m_currentScale + notches * m_scaleStep);
        event->accept();
        return;
    }
    QGraphicsScene::wheelEvent(event);
}
