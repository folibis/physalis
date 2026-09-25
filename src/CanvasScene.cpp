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
#include "SceneSerializer.h"

#include <QJsonArray>
#include <QJsonObject>
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

namespace
{

qreal distanceToSegment(const QPointF &p, const QPointF &a, const QPointF &b)
{
    const QPointF ab = b - a;
    const qreal lengthSquared = ab.x() * ab.x() + ab.y() * ab.y();
    if (lengthSquared <= 0.0)
    {
        return QLineF(a, p).length();
    }

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

CanvasScene::CanvasScene(QObject *parent) : QGraphicsScene(parent)
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
    for (ShapeItem *shape : shapes())
    {
        if (shape != except)
        {
            taken.insert(shape->name());
        }
    }
    for (PhysicsBody *body : m_bodies)
    {
        if (body != except)
        {
            taken.insert(body->name());
        }
    }
    for (Joint *joint : m_joints)
    {
        if (joint != except)
        {
            taken.insert(joint->name());
        }
    }
    // Rays and explosions too: a rule names them in the same field it names a
    // body in, so a second ray called what the first one is makes that rule
    // ambiguous -- and left out of here, every ray was called ray_1.
    for (RayItem *ray : m_rays)
    {
        if (ray != except)
        {
            taken.insert(ray->name());
        }
    }
    for (ExplosionItem *explosion : m_explosions)
    {
        if (explosion != except)
        {
            taken.insert(explosion->name());
        }
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
    for (QGraphicsItem *item : all)
    {
        if (auto *shape = qgraphicsitem_cast<ShapeItem *>(item))
        {
            found.append(shape);
        }
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

ShapeItem *CanvasScene::convertToPolygon(ShapeItem *shape)
{
    if (!shape || shape->scene() != this)
    {
        return nullptr;
    }
    // Only a rectangle has four corners to become. A circle would need an
    // arbitrary number of them, which is a different question with a
    // different answer for every scene.
    if (!dynamic_cast<RectangleItem *>(shape))
    {
        return nullptr;
    }

    // Through the same JSON a duplicate goes through, so the two cannot drift
    // apart in what they carry across.
    QJsonObject json = SceneSerializer::shapeToJson(shape);
    const QRectF r = shape->rect();
    QJsonArray corners;
    for (const QPointF &p : {r.topLeft(), r.topRight(), r.bottomRight(), r.bottomLeft()})
    {
        corners.append(QJsonObject{{"x", p.x()}, {"y", p.y()}});
    }
    json.insert(QStringLiteral("type"), QStringLiteral("polygon"));
    json.insert(QStringLiteral("closed"), true);
    json.insert(QStringLiteral("points"), corners);
    // Rectangles alone carry one: Box2D rounds a box without growing it, and
    // there is no such rounding for an arbitrary polygon. The corners come
    // out square, so the number goes rather than lingering with no effect.
    json.insert(QStringLiteral("cornerRadius"), 0.0);

    ShapeItem *polygon = SceneSerializer::shapeFromJson(json);
    if (!polygon)
    {
        return nullptr;
    }

    polygon->setZValue(shape->zValue());
    addItem(polygon);

    PhysicsBody *body = shape->body();
    if (body)
    {
        body->replaceShape(shape, polygon);
    }

    // Wherever the old shape was being held, the new one takes its place.
    const bool wasActive = (m_active == shape);
    const bool wasPicked = m_physicsSelection.contains(shape);
    if (wasActive)
    {
        m_active = nullptr;
        m_selectedNodes.clear();
    }
    const int inEditSelection = m_editSelection.indexOf(shape);
    if (inEditSelection >= 0)
    {
        m_editSelection[inEditSelection] = polygon;
    }
    const int inPhysicsSelection = m_physicsSelection.indexOf(shape);
    if (inPhysicsSelection >= 0)
    {
        m_physicsSelection[inPhysicsSelection] = polygon;
    }
    m_bodyDragShapes.removeAll(shape);
    if (m_groupClickCandidate == shape)
    {
        m_groupClickCandidate = nullptr;
    }
    m_dragMode = DragMode::None;

    removeItem(shape);
    delete shape;

    emit shapesChanged();
    if (body)
    {
        emit bodiesChanged();
    }
    if (wasActive)
    {
        activate(polygon);
    }
    if (wasPicked)
    {
        emit physicsSelectionChanged();
    }
    if (inEditSelection >= 0)
    {
        emit editSelectionChanged();
    }

    return polygon;
}

ExplosionItem *CanvasScene::addExplosion(const QPointF &scenePos)
{
    auto *explosion = new ExplosionItem();
    explosion->setPos(scenePos);
    addItem(explosion);
    explosion->setName(Naming::nextName(ExplosionItem::typeName(), takenNames(explosion)));
    explosion->setVisible(m_editorMode != EditorMode::Edit);
    m_explosions.append(explosion);
    emit explosionsChanged();
    return explosion;
}

void CanvasScene::removeExplosion(ExplosionItem *explosion)
{
    if (!explosion || !m_explosions.removeOne(explosion))
    {
        return;
    }
    removeItem(explosion);
    delete explosion;
    emit explosionsChanged();
}

RayItem *CanvasScene::addRay(const QPointF &scenePos)
{
    auto *ray = new RayItem();
    ray->setPos(scenePos);
    addItem(ray);
    ray->setName(Naming::nextName(RayItem::typeName(), takenNames(ray)));
    ray->setVisible(m_editorMode != EditorMode::Edit);
    m_rays.append(ray);
    emit raysChanged();
    return ray;
}

void CanvasScene::removeRay(RayItem *ray)
{
    if (!ray || !m_rays.removeOne(ray))
    {
        return;
    }
    if (m_selectedRay == ray)
    {
        m_selectedRay = nullptr;
        emit selectedRayChanged(nullptr);
    }
    if (m_draggedRay == ray)
    {
        m_draggedRay = nullptr;
    }
    removeItem(ray);
    delete ray;
    emit raysChanged();
}

RayItem *CanvasScene::rayNamed(const QString &name) const
{
    for (RayItem *ray : m_rays)
    {
        if (ray->name() == name)
        {
            return ray;
        }
    }
    return nullptr;
}

void CanvasScene::selectRay(RayItem *ray)
{
    if (m_selectedRay == ray)
    {
        return;
    }
    if (ray)
    {
        clearPhysicsSelection();
        selectJoint(nullptr);
        selectExplosion(nullptr);
    }
    if (m_selectedRay)
    {
        m_selectedRay->setSelectedForPhysics(false);
    }
    m_selectedRay = ray;
    if (m_selectedRay)
    {
        m_selectedRay->setSelectedForPhysics(true);
    }
    emit selectedRayChanged(ray);
}

void CanvasScene::selectExplosion(ExplosionItem *explosion)
{
    if (m_selectedExplosion == explosion)
    {
        return;
    }
    if (explosion)
    {
        clearPhysicsSelection();
        selectJoint(nullptr);
    }
    if (m_selectedExplosion)
    {
        m_selectedExplosion->setSelectedForPhysics(false);
    }
    m_selectedExplosion = explosion;
    if (m_selectedExplosion)
    {
        m_selectedExplosion->setSelectedForPhysics(true);
    }
    emit selectedExplosionChanged(explosion);
}

ExplosionItem *CanvasScene::explosionNamed(const QString &name) const
{
    for (ExplosionItem *explosion : m_explosions)
    {
        if (explosion->name() == name)
        {
            return explosion;
        }
    }
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
    {
        return;
    }

    m_editorMode = mode;
    m_dragMode = DragMode::None;

    if (m_polygonDrawing)
    {
        cancelPolygonDrawing();
    }
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
    {
        explosion->setVisible(physics);
    }
    for (RayItem *ray : std::as_const(m_rays))
    {
        ray->setVisible(physics);
    }

    update();
    emit editorModeChanged(m_editorMode);
}

void CanvasScene::selectForPhysics(ShapeItem *shape, bool additive)
{
    if (!shape)
    {
        clearPhysicsSelection();
        return;
    }

    // Picking a body is picking something else: the explosion stops being
    // what the panel and the Remove button are pointed at.
    selectExplosion(nullptr);

    if (additive)
    {
        QVector<ShapeItem *> group;
        if (PhysicsBody *body = shape->body())
        {
            group = body->shapes();
        }
        else
        {
            group.append(shape);
        }

        const bool alreadyPicked = m_physicsSelection.contains(shape);
        for (ShapeItem *member : std::as_const(group))
        {
            if (alreadyPicked)
            {
                m_physicsSelection.removeOne(member);
            }
            else if (!m_physicsSelection.contains(member))
            {
                m_physicsSelection.append(member);
            }
        }
    }
    else if (m_physicsSelection.size() == 1 && m_physicsSelection.first() == shape)
    {
        return;
    }
    else
    {
        m_physicsSelection.clear();
        m_physicsSelection.append(shape);
    }

    update();
    emit physicsSelectionChanged();
}

void CanvasScene::clearPhysicsSelection()
{
    if (m_physicsSelection.isEmpty())
    {
        return;
    }
    m_physicsSelection.clear();
    update();
    emit physicsSelectionChanged();
}

PhysicsBody *CanvasScene::createEmptyBody(bool announce)
{
    auto *body = new PhysicsBody(this);
    body->setName(Naming::nextName(QStringLiteral("body"), takenNames()));

    connect(body, &PhysicsBody::nameChanged, this, &CanvasScene::objectRenamed);
    connect(body, &PhysicsBody::propertyChanged, this, [this] {
        update();
    });
    connect(body, &PhysicsBody::membershipChanged, this, [this] {
        update();

        // Queued, not immediate. removeShape() is emitted from ~ShapeItem, so
        // this runs while a shape is half destroyed -- and destroying a body
        // here would delete its joints and emit three more signals from inside
        // that destructor. Deferring also means a body being rebuilt shape by
        // shape isn't torn down between the first removal and the first add.
        if (m_prunePending)
        {
            return;
        }
        m_prunePending = true;
        QMetaObject::invokeMethod(
                this,
                [this] {
            pruneEmptyBodies();
                },
                Qt::QueuedConnection);
    });

    m_bodies.append(body);
    if (announce)
    {
        emit bodiesChanged();
    }
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
    for (QGraphicsItem *item : existing)
    {
        if (auto *shape = qgraphicsitem_cast<ShapeItem *>(item))
        {
            removeItem(shape);
            delete shape;
        }
    }

    update();
    emit shapesChanged();
    emit bodiesChanged();
    emit physicsSelectionChanged();
}

ShapeItem *CanvasScene::looseShapeAt(const QPointF &scenePos) const
{
    ShapeItem *found = nullptr;
    for (ShapeItem *shape : shapesAt(scenePos))
    {
        if (shape->body())
        {
            continue;
        }
        // One already selected wins over the one on top, so a shape reached
        // with Ctrl+click is still the one a right-click means.
        if (isSelectedForPhysics(shape))
        {
            return shape;
        }
        if (!found)
        {
            found = shape;
        }
    }
    return found;
}

PhysicsBody *CanvasScene::createBodyFromSelection()
{
    if (m_physicsSelection.isEmpty())
    {
        return nullptr;
    }

    PhysicsBody *body = createEmptyBody();

    QVector<PhysicsBody *> vacated;
    for (ShapeItem *shape : m_physicsSelection)
    {
        if (PhysicsBody *previous = shape->body())
        {
            if (!vacated.contains(previous))
            {
                vacated.append(previous);
            }
        }
        body->addShape(shape);
    }

    // Through destroyBody(), not a raw delete, so the body's joints go too.
    for (PhysicsBody *previous : vacated)
    {
        if (previous->isEmpty())
        {
            destroyBody(previous);
        }
    }

    // Only a filled shape has area, and only area gives mass. A body made of
    // outlines cannot be dynamic: it would be dropped at the start of every
    // run as having none. Kinematic is the closest thing that still works --
    // it can be driven by a velocity, it just is not pushed around -- so that
    // is what such a body starts as rather than a dynamic one that is thrown
    // away. Whether a run then accepts it is the engine's answer, and a body
    // it refuses is already reported as skipped.
    bool anyInterior = false;
    for (ShapeItem *shape : body->shapes())
    {
        anyInterior = anyInterior || shape->hasInterior();
    }
    if (!anyInterior && !body->shapes().isEmpty())
    {
        body->props().type = physics::BodyType::Kinematic;
    }

    update();
    emit bodiesChanged();
    emit physicsSelectionChanged();
    return body;
}

Joint *CanvasScene::createJoint(const QString &typeId, PhysicsBody *bodyA, PhysicsBody *bodyB, int anchorCount,
                                const QVariantMap &defaultParams)
{
    // bodyB may be absent: a joint that holds a body to a point in the world
    // has no second body to name.
    if (!bodyA || bodyA == bodyB)
    {
        return nullptr;
    }

    auto *joint = new Joint(this);
    joint->setName(Naming::nextName(typeId, takenNames()));
    joint->setTypeId(typeId);
    joint->setBodies(bodyA, bodyB);
    joint->params() = defaultParams;

    joint->setAnchorCount(anchorCount);

    const QPointF centreA = bodyA->centerOfMassScenePos();
    // With no second body the anchor starts on the body itself, so a target
    // left where it was put pulls towards where the body already is rather
    // than dragging it somewhere the moment the run begins.
    const QPointF centreB = bodyB ? bodyB->centerOfMassScenePos() : centreA;

    if (anchorCount > 1)
    {
        joint->setAnchorScenePos(Joint::End::A, centreA);
        joint->setAnchorScenePos(Joint::End::B, centreB);
    }
    else
    {
        joint->setAnchorScenePos(Joint::End::A, (centreA + centreB) / 2.0);
    }

    const QPointF span = centreB - centreA;
    if (!qFuzzyIsNull(span.x()) || !qFuzzyIsNull(span.y()))
    {
        joint->setAxisScene(span);
    }

    // Some parameters only make sense measured from the bodies as they stand.
    // Which ones is the engine's business; this just asks for the measurement
    // it names.
    if (auto engine = physics::EngineRegistry::create(describingEngineName()))
    {
        for (const physics::JointType &type : engine->jointTypes())
        {
            if (type.id != typeId)
            {
                continue;
            }
            QTransform intoBodyA;
            intoBodyA.rotate(-bodyA->rotationDegrees());
            // Measured between the two bodies. With only one there is nothing
            // to measure against, and every such default is zero -- which is
            // what "no offset from where it is" means anyway.
            const QPointF offset = bodyB ? intoBodyA.map(bodyB->originScenePos() - bodyA->originScenePos()) : QPointF();
            const qreal angle = bodyB ? bodyB->rotationDegrees() - bodyA->rotationDegrees() : 0.0;
            for (const physics::JointParam &param : type.params)
            {
                switch (param.defaultSource)
                {
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

    connect(joint, &Joint::nameChanged, this, &CanvasScene::objectRenamed);
    connect(joint, &Joint::propertyChanged, this, [this] {
        update();
    });

    m_joints.append(joint);
    update();
    emit jointsChanged();
    return joint;
}

void CanvasScene::destroyJoint(Joint *joint)
{
    if (!joint || !m_joints.removeOne(joint))
    {
        return;
    }
    if (m_selectedJoint == joint)
    {
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
    {
        return;
    }
    m_jointSelectionColor = color;
    update();
}

void CanvasScene::setJointSelectionLineWidth(qreal width)
{
    width = qBound(0.5, width, 20.0);
    if (qFuzzyCompare(m_jointSelectionLineWidth, width))
    {
        return;
    }
    m_jointSelectionLineWidth = width;
    update();
}

void CanvasScene::setJointSelectionLineStyle(Qt::PenStyle style)
{
    if (m_jointSelectionLineStyle == style)
    {
        return;
    }
    m_jointSelectionLineStyle = style;
    update();
}

QVector<physics::JointVisual> CanvasScene::jointKinds()
{
    using physics::JointVisual;
    return {JointVisual::Pivot, JointVisual::Segment, JointVisual::Axis, JointVisual::Rigid, JointVisual::Link};
}

QString CanvasScene::jointKindKey(physics::JointVisual kind)
{
    switch (kind)
    {
    case physics::JointVisual::Pivot:
        return QStringLiteral("pivot");
    case physics::JointVisual::Segment:
        return QStringLiteral("segment");
    case physics::JointVisual::Axis:
        return QStringLiteral("axis");
    case physics::JointVisual::Rigid:
        return QStringLiteral("rigid");
    case physics::JointVisual::Link:
        return QStringLiteral("link");
    }
    return QStringLiteral("pivot");
}

QString CanvasScene::jointKindLabel(physics::JointVisual kind)
{
    switch (kind)
    {
    case physics::JointVisual::Pivot:
        return tr("Turning");
    case physics::JointVisual::Segment:
        return tr("Holding a length");
    case physics::JointVisual::Axis:
        return tr("Sliding");
    case physics::JointVisual::Rigid:
        return tr("Fixed");
    case physics::JointVisual::Link:
        return tr("Anchorless");
    }
    return QString();
}

QString CanvasScene::jointKindDescription(physics::JointVisual kind)
{
    switch (kind)
    {
    case physics::JointVisual::Pivot:
        return tr("Joints that turn about a point -- a hinge.");
    case physics::JointVisual::Segment:
        return tr("Joints between two points that hold a distance -- rods, ropes and springs.");
    case physics::JointVisual::Axis:
        return tr("Joints that travel along a line -- sliders and suspensions.");
    case physics::JointVisual::Rigid:
        return tr("Joints that hold two bodies fixed to each other -- welds.");
    case physics::JointVisual::Link:
        return tr("Joints with no anchor at all, drawn as a connector"
                  " between the two bodies -- motors, gears, filters.");
    }
    return QString();
}

QString CanvasScene::jointStyleLabel(JointStyle style)
{
    switch (style)
    {
    case JointStyle::Rod:
        return tr("Rod");
    case JointStyle::Solid:
        return tr("Solid line");
    case JointStyle::Dashed:
        return tr("Dashed line");
    case JointStyle::Dotted:
        return tr("Dotted line");
    case JointStyle::DashDot:
        return tr("Dash-dot line");
    }
    return QString();
}

QVector<JointStyle> CanvasScene::jointStyles()
{
    return {JointStyle::Rod, JointStyle::Solid, JointStyle::Dashed, JointStyle::Dotted, JointStyle::DashDot};
}

// What each kind is drawn in until someone chooses otherwise.
QColor CanvasScene::defaultJointKindColor(physics::JointVisual kind)
{
    switch (kind)
    {
    case physics::JointVisual::Pivot:
        return QColor(0xE8, 0xC4, 0x6A); // amber
    case physics::JointVisual::Segment:
        return QColor(0x6A, 0xB0, 0xE8); // blue
    case physics::JointVisual::Axis:
        return QColor(0x6A, 0xD1, 0xA8); // green
    case physics::JointVisual::Rigid:
        return QColor(0xB4, 0x8A, 0xE8); // violet
    case physics::JointVisual::Link:
        return QColor(0x9E, 0x9E, 0x9E); // grey
    }
    return QColor(0xE8, 0xC4, 0x6A);
}

JointStyle CanvasScene::defaultJointKindStyle(physics::JointVisual kind)
{
    // A joint with two ends is a rod; one with no anchors has nothing to be a
    // rod between, and has always been drawn as a dashed connector.
    return kind == physics::JointVisual::Link ? JointStyle::Dashed : JointStyle::Rod;
}

QColor CanvasScene::jointKindColor(physics::JointVisual kind) const
{
    const auto it = m_jointKindColors.constFind(static_cast<int>(kind));
    if (it != m_jointKindColors.constEnd() && it->isValid())
    {
        return *it;
    }
    return defaultJointKindColor(kind);
}

void CanvasScene::setJointKindColor(physics::JointVisual kind, const QColor &color)
{
    if (m_jointKindColors.value(static_cast<int>(kind)) == color)
    {
        return;
    }
    m_jointKindColors.insert(static_cast<int>(kind), color);
    update();
}

JointStyle CanvasScene::jointKindStyle(physics::JointVisual kind) const
{
    return m_jointKindStyles.value(static_cast<int>(kind), defaultJointKindStyle(kind));
}

void CanvasScene::setJointKindStyle(physics::JointVisual kind, JointStyle style)
{
    if (jointKindStyle(kind) == style)
    {
        return;
    }
    m_jointKindStyles.insert(static_cast<int>(kind), style);
    update();
}

void CanvasScene::setJointKindColors(const QHash<int, QColor> &colors)
{
    if (m_jointKindColors == colors)
    {
        return;
    }
    m_jointKindColors = colors;
    update();
}

void CanvasScene::setJointKindStyles(const QHash<int, JointStyle> &styles)
{
    if (m_jointKindStyles == styles)
    {
        return;
    }
    m_jointKindStyles = styles;
    update();
}

QColor CanvasScene::jointTypeColor(const QString &typeId) const
{
    return jointKindColor(jointVisual(typeId));
}

void CanvasScene::loadRoleKeys() const
{
    const QString engineName = describingEngineName();
    if (m_roleKeysEngine == engineName)
    {
        return;
    }
    m_roleKeysEngine = engineName;
    m_sensorKey.clear();
    m_densityKey.clear();
    if (auto engine = physics::EngineRegistry::create(engineName))
    {
        const physics::PropertyList shapes = engine->shapeProperties();
        m_sensorKey = physics::keyForRole(shapes, physics::PropertyRole::Sensor);
        m_densityKey = physics::keyForRole(shapes, physics::PropertyRole::Density);
    }
}

bool CanvasScene::hasSensorProperty() const
{
    loadRoleKeys();
    return !m_sensorKey.isEmpty();
}

QString CanvasScene::sensorPropertyKey() const
{
    loadRoleKeys();
    return m_sensorKey;
}

bool CanvasScene::isSensorShape(const ShapeItem *shape) const
{
    loadRoleKeys();
    return shape && !m_sensorKey.isEmpty() && shape->part().params.value(m_sensorKey).toBool();
}

void CanvasScene::setSensorShape(ShapeItem *shape, bool sensor)
{
    loadRoleKeys();
    if (!shape || m_sensorKey.isEmpty())
    {
        return;
    }
    shape->part().params.insert(m_sensorKey, sensor);
}

qreal CanvasScene::shapeDensity(const ShapeItem *shape) const
{
    loadRoleKeys();
    if (!shape || m_densityKey.isEmpty())
    {
        return 1.0;
    }
    return shape->part().params.value(m_densityKey, 1.0).toDouble();
}

QString CanvasScene::describingEngineName() const
{
    if (!m_simulationEngineName.isEmpty())
    {
        return m_simulationEngineName;
    }
    // A scene file carries no engine name, so one loaded before the simulation
    // is set up has none. Falling through to "no engine" would silently drop
    // every joint back to a default colour and a default shape.
    const QStringList available = physics::EngineRegistry::availableEngines();
    return available.isEmpty() ? QString() : available.first();
}

physics::JointVisual CanvasScene::jointVisual(const QString &typeId) const
{
    const QString engineName = describingEngineName();
    if (m_jointVisualsEngine != engineName || m_jointVisuals.isEmpty())
    {
        m_jointVisualsEngine = engineName;
        m_jointVisuals.clear();
        if (auto engine = physics::EngineRegistry::create(engineName))
        {
            for (const physics::JointType &type : engine->jointTypes())
            {
                m_jointVisuals.insert(type.id, type.visual);
            }
        }
    }
    return m_jointVisuals.value(typeId, physics::JointVisual::Pivot);
}

void CanvasScene::setJointColor(const QColor &color)
{
    if (m_jointColor == color)
    {
        return;
    }
    m_jointColor = color;
    update();
}

void CanvasScene::setJointAnchorRadius(qreal radius)
{
    radius = qBound(2.0, radius, 100.0);
    if (qFuzzyCompare(m_jointAnchorRadius, radius))
    {
        return;
    }
    m_jointAnchorRadius = radius;
    update();
}

void CanvasScene::setJointAxisLength(qreal length)
{
    length = qMax(1.0, length);
    if (qFuzzyCompare(m_jointAxisLength, length))
    {
        return;
    }
    m_jointAxisLength = length;
    update();
}

void CanvasScene::setJointWaistWidth(qreal width)
{
    width = qBound(1.0, width, 100.0);
    if (qFuzzyCompare(m_jointWaistWidth, width))
    {
        return;
    }
    m_jointWaistWidth = width;
    update();
}

void CanvasScene::setJointOutlineWidth(qreal width)
{
    width = qMax(0.0, width);
    if (qFuzzyCompare(m_jointOutlineWidth, width))
    {
        return;
    }
    m_jointOutlineWidth = width;
    update();
}

void CanvasScene::setJointOutlineColor(const QColor &color)
{
    if (m_jointOutlineColor == color)
    {
        return;
    }
    m_jointOutlineColor = color;
    update();
}

void CanvasScene::setSimulationEngineName(const QString &name)
{
    if (m_simulationEngineName == name)
    {
        return;
    }
    m_simulationEngineName = name;
    emit jointsChanged();
}

void CanvasScene::selectJoint(Joint *joint)
{
    if (m_selectedJoint == joint)
    {
        return;
    }
    m_selectedJoint = joint;
    update();
    emit selectedJointChanged(m_selectedJoint);
}

Joint *CanvasScene::jointAt(const QPointF &scenePos, int *end) const
{
    const qreal zoom = 100.0 / qMax(1.0, m_currentScale);
    const qreal anchorRadius = qMax(10.0, m_jointAnchorRadius) * zoom;
    const qreal shaftRadius = (m_jointWaistWidth / 2.0 + 4.0) * zoom;

    for (Joint *joint : m_joints)
    {
        // Last first: a joint whose two anchors start on top of each other
        // hands you the second, which is the one that is moved. The first is
        // where it grips, and is left alone far more often.
        for (int which = joint->anchorCount() - 1; which >= 0; --which)
        {
            const auto whichEnd = which == 0 ? Joint::End::A : Joint::End::B;
            const QPointF anchor = joint->anchorScenePos(whichEnd);
            if (QLineF(anchor, scenePos).length() <= anchorRadius)
            {
                if (end)
                {
                    *end = which;
                }
                return joint;
            }
        }
    }

    for (Joint *joint : m_joints)
    {
        const int anchors = joint->anchorCount();

        // A joint that holds one body to a point draws a leader from the point
        // back to the body, and what is drawn is what can be picked -- a line
        // you can see and cannot click on is the odd one out.
        if (!joint->bodyB() && joint->bodyA())
        {
            const QPointF target = joint->anchorScenePos(Joint::End::A);
            const QPointF centre = anchors > 1 ? joint->anchorScenePos(Joint::End::B)
                                               : joint->bodyA()->centerOfMassScenePos();
            if (distanceToSegment(scenePos, target, centre) <= shaftRadius)
            {
                if (end)
                {
                    *end = kJointShaft;
                }
                return joint;
            }
            continue;
        }

        if (anchors == 1)
        {
            continue; // a single pin is all anchor and no shaft
        }
        const QPointF a = anchors > 0 ? joint->anchorScenePos(Joint::End::A) : joint->bodyA()->centerOfMassScenePos();
        const QPointF b = anchors > 0 || !joint->bodyB() ? joint->anchorScenePos(Joint::End::B)
                                                         : joint->bodyB()->centerOfMassScenePos();
        if (distanceToSegment(scenePos, a, b) <= shaftRadius)
        {
            if (end)
            {
                *end = kJointShaft;
            }
            return joint;
        }
    }

    return nullptr;
}

void CanvasScene::objectRenamed(const QString &previous, const QString &current)
{
    if (previous.isEmpty() || previous == current)
    {
        return;
    }

    // A run must not write to the document. Nothing in it renames anything a
    // rule could mean -- the only naming it does is giving a clone's copies
    // names of their own, and a clone is run state that no rule may name -- so
    // anything arriving here mid-run is that, wearing the name it was copied
    // from.
    if (m_simulationRunning)
    {
        return;
    }

    bool inRules = false;
    for (Rule &rule : m_rules)
    {
        for (RuleCondition &condition : rule.conditions)
        {
            if (condition.subjectName == previous)
            {
                condition.subjectName = current;
                inRules = true;
            }
            // An event's value is the other object it has to have happened
            // with, so it is a name too.
            if (condition.isEvent() && condition.conditionValue.toString() == previous)
            {
                condition.conditionValue = current;
                inRules = true;
            }
        }
        for (RuleAction &action : rule.actions)
        {
            if (action.targetName == previous)
            {
                action.targetName = current;
                inRules = true;
            }
            // Where the value comes from, for a rule reading one object onto
            // another.
            if (action.sourceObject == previous)
            {
                action.sourceObject = current;
                inRules = true;
            }
        }
    }
    if (inRules)
    {
        emit rulesChanged();
    }

    bool inLog = false;
    for (Watch &watch : m_watches)
    {
        if (watch.objectName == previous)
        {
            watch.objectName = current;
            inLog = true;
        }
    }
    if (inLog)
    {
        emit watchesChanged();
    }
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
    for (PhysicsBody *body : existing)
    {
        if (body->isEmpty())
        {
            destroyBody(body); // which takes that body's joints with it
        }
    }
}

void CanvasScene::destroyBody(PhysicsBody *body, bool announce)
{
    if (!body || !m_bodies.removeOne(body))
    {
        return;
    }

    // A joint with one end gone constrains nothing and cannot be drawn.
    const QVector<Joint *> attached = m_joints;
    for (Joint *joint : attached)
    {
        if (joint->bodyA() == body || joint->bodyB() == body)
        {
            destroyJoint(joint);
        }
    }
    delete body;
    update();
    if (announce)
    {
        emit bodiesChanged();
        emit physicsSelectionChanged();
    }
}

PhysicsBody *CanvasScene::commonSelectedBody() const
{
    if (m_physicsSelection.isEmpty())
    {
        return nullptr;
    }

    PhysicsBody *body = m_physicsSelection.first()->body();
    if (!body)
    {
        return nullptr;
    }
    for (ShapeItem *shape : m_physicsSelection)
    {
        if (shape->body() != body)
        {
            return nullptr;
        }
    }
    return body;
}

QColor CanvasScene::bodyColor(physics::BodyType type) const
{
    switch (type)
    {
    case physics::BodyType::Static:
        return m_bodyStaticColor;
    case physics::BodyType::Kinematic:
        return m_bodyKinematicColor;
    case physics::BodyType::Dynamic:
        return m_bodyDynamicColor;
    }
    return m_bodyDynamicColor;
}

void CanvasScene::setBodyColor(physics::BodyType type, const QColor &color)
{
    QColor *target = nullptr;
    switch (type)
    {
    case physics::BodyType::Static:
        target = &m_bodyStaticColor;
        break;
    case physics::BodyType::Kinematic:
        target = &m_bodyKinematicColor;
        break;
    case physics::BodyType::Dynamic:
        target = &m_bodyDynamicColor;
        break;
    }
    if (!target || *target == color)
    {
        return;
    }
    *target = color;
    update();
}

void CanvasScene::setUnassignedShapeColor(const QColor &color)
{
    if (m_unassignedShapeColor == color)
    {
        return;
    }
    m_unassignedShapeColor = color;
    update();
}

void CanvasScene::setPhysicsBorderWidth(qreal width)
{
    width = qMax(0.0, width);
    if (qFuzzyCompare(m_physicsBorderWidth, width))
    {
        return;
    }
    m_physicsBorderWidth = width;
    update();
}

void CanvasScene::setPhysicsFillAlpha(int alpha)
{
    alpha = qBound(0, alpha, 255);
    if (m_physicsFillAlpha == alpha)
    {
        return;
    }
    m_physicsFillAlpha = alpha;
    update();
}

void CanvasScene::setPhysicsSelectionLineStyle(Qt::PenStyle style)
{
    if (m_physicsSelectionLineStyle == style)
    {
        return;
    }
    m_physicsSelectionLineStyle = style;
    update();
}

void CanvasScene::setPhysicsSelectionLineWidth(qreal width)
{
    width = qMax(0.0, width);
    if (qFuzzyCompare(m_physicsSelectionLineWidth, width))
    {
        return;
    }
    m_physicsSelectionLineWidth = width;
    update();
}

void CanvasScene::setPhysicsSelectionColor(const QColor &color)
{
    if (m_physicsSelectionColor == color)
    {
        return;
    }
    m_physicsSelectionColor = color;
    update();
}

void CanvasScene::addWatch(const Watch &watch)
{
    if (watch.objectName.isEmpty() || watch.propertyKey.isEmpty() || m_watches.contains(watch))
    {
        return;
    }
    m_watches.append(watch);
    emit watchesChanged();
}

void CanvasScene::removeWatch(const QString &objectName, const QString &propertyKey)
{
    const int before = m_watches.size();
    m_watches.erase(std::remove_if(m_watches.begin(), m_watches.end(),
                                   [&](const Watch &w) {
                                       return w.objectName == objectName
                                              && w.propertyKey == propertyKey;
                                   }),
                    m_watches.end());
    if (m_watches.size() != before)
    {
        emit watchesChanged();
    }
}

bool CanvasScene::isWatched(const QString &objectName, const QString &propertyKey) const
{
    for (const Watch &w : m_watches)
    {
        if (w.objectName == objectName && w.propertyKey == propertyKey)
        {
            return true;
        }
    }
    return false;
}

void CanvasScene::clearWatches()
{
    if (m_watches.isEmpty())
    {
        return;
    }
    m_watches.clear();
    emit watchesChanged();
}

void CanvasScene::setWatches(const QVector<Watch> &watches)
{
    m_watches = watches;
    emit watchesChanged();
}

void CanvasScene::setVariables(const QVector<SceneVariable> &variables)
{
    m_variables = variables;
    emit variablesChanged();
}

void CanvasScene::renameVariableInRules(const QString &previous, const QString &current)
{
    if (previous == current)
    {
        return;
    }

    bool touched = false;
    for (Rule &rule : m_rules)
    {
        for (RuleCondition &condition : rule.conditions)
        {
            if (condition.subjectName == Rule::variables() && condition.conditionKey == previous)
            {
                condition.conditionKey = current;
                touched = true;
            }
        }
        for (RuleAction &action : rule.actions)
        {
            if (action.targetName == Rule::variables() && action.propertyKey == previous)
            {
                action.propertyKey = current;
                touched = true;
            }
            if (action.sourceObject == Rule::variables() && action.sourceProperty == previous)
            {
                action.sourceProperty = current;
                touched = true;
            }
        }
    }
    if (touched)
    {
        emit rulesChanged();
    }

    for (Watch &watch : m_watches)
    {
        if (watch.objectName == Rule::variables() && watch.propertyKey == previous)
        {
            watch.propertyKey = current;
            if (watch.label == previous)
            {
                watch.label = current;
            }
            emit watchesChanged();
        }
    }
}

const SceneVariable *CanvasScene::variableNamed(const QString &name) const
{
    for (const SceneVariable &variable : m_variables)
    {
        if (variable.name == name)
        {
            return &variable;
        }
    }
    return nullptr;
}

QString CanvasScene::uniqueVariableName(const QString &desired, int except) const
{
    const QString base = desired.isEmpty() ? QStringLiteral("variable") : desired;
    QString name = base;
    int suffix = 1;
    const auto taken = [this, except](const QString &candidate) {
        for (int i = 0; i < m_variables.size(); ++i)
        {
            if (i != except && m_variables.at(i).name == candidate)
            {
                return true;
            }
        }
        return false;
    };
    while (taken(name))
    {
        name = base + QString::number(++suffix);
    }
    return name;
}

QVariant CanvasScene::readSceneValue(const QString &objectName, const QString &key) const
{
    // Before a run, a variable reads as whatever it starts at. During one the
    // simulation answers instead, since that is where it has been counting.
    if (objectName == Rule::variables())
    {
        const SceneVariable *variable = variableNamed(key);
        return variable ? variable->value() : QVariant();
    }

    // A key may be namespaced ("shape.rotation") or bare ("motorSpeed", as the
    // engine names it). Bare keys are resolved against whichever object bears
    // the name, so the log does not care which table a row came from.
    const int dot = key.indexOf(QLatin1Char('.'));
    const QString what = dot < 0 ? key : key.mid(dot + 1);
    const bool anyKind = dot < 0;

    if (anyKind)
    {
        for (Joint *joint : m_joints)
        {
            if (joint->name() != objectName)
            {
                continue;
            }
            const auto it = joint->params().constFind(what);
            if (it != joint->params().constEnd())
            {
                return *it;
            }
        }
    }

    if (anyKind || key.startsWith(QLatin1String("shape.")))
    {
        for (ShapeItem *shape : shapes())
        {
            if (shape->name() != objectName)
            {
                continue;
            }
            if (what == QLatin1String("x"))
            {
                return shape->pos().x() + shape->rect().x();
            }
            if (what == QLatin1String("y"))
            {
                return shape->pos().y() + shape->rect().y();
            }
            if (what == QLatin1String("width"))
            {
                return shape->rect().width();
            }
            if (what == QLatin1String("height"))
            {
                return shape->rect().height();
            }
            if (what == QLatin1String("rotation"))
            {
                return shape->rotation();
            }
            if (what == QLatin1String("originX"))
            {
                return shape->origin().x();
            }
            if (what == QLatin1String("originY"))
            {
                return shape->origin().y();
            }
            if (what == QLatin1String("borderWidth"))
            {
                return shape->borderWidth();
            }
            const physics::ShapePart &part = shape->part();
            // Whatever the engine said a shape has, under the name it gave
            // it. Nothing here knows what any of them mean.
            if (part.params.contains(what))
            {
                return part.params.value(what);
            }
            if (!anyKind)
            {
                return {};
            }
            break;
        }
    }

    if (anyKind || key.startsWith(QLatin1String("body.")))
    {
        for (PhysicsBody *body : m_bodies)
        {
            if (body->name() != objectName)
            {
                continue;
            }
            const physics::BodyDesc &props = body->props();
            if (what == QLatin1String("angle"))
            {
                return body->rotationDegrees();
            }
            if (what == QLatin1String("positionX"))
            {
                return body->originScenePos().x();
            }
            if (what == QLatin1String("positionY"))
            {
                return body->originScenePos().y();
            }
            if (what == QLatin1String("isEnabled"))
            {
                return props.isEnabled;
            }
            // Whatever the engine said a body has, under its own name.
            if (props.params.contains(what))
            {
                return props.params.value(what);
            }
            if (!anyKind)
            {
                return {};
            }
            break;
        }
    }

    if (key.startsWith(QLatin1String("joint.")))
    {
        for (Joint *joint : m_joints)
        {
            if (joint->name() != objectName)
            {
                continue;
            }
            const auto it = joint->params().constFind(what);
            return it == joint->params().constEnd() ? QVariant() : *it;
        }
    }
    return {};
}

void CanvasScene::setRunLayer(RunLayer layer, bool on)
{
    bool &stored = m_runLayers[static_cast<int>(layer)];
    if (stored == on)
    {
        return;
    }
    stored = on;

    // Only a run is affected, so only a run needs repainting -- but the items
    // paint themselves and have to be told.
    if (layer == RunLayer::Rays)
    {
        for (RayItem *ray : std::as_const(m_rays))
        {
            ray->update();
        }
    }
    else if (layer == RunLayer::Explosions)
    {
        for (ExplosionItem *explosion : std::as_const(m_explosions))
        {
            explosion->update();
        }
    }
    else if (layer == RunLayer::SleepShading)
    {
        for (ShapeItem *shape : shapes())
        {
            shape->update();
        }
    }
    update();
}

void CanvasScene::setShowBodyAxes(bool show)
{
    if (m_showBodyAxes == show)
    {
        return;
    }
    m_showBodyAxes = show;
    update();
}

void CanvasScene::setBodyAxisLength(qreal length)
{
    length = qMax(1.0, length);
    if (qFuzzyCompare(m_bodyAxisLength, length))
    {
        return;
    }
    m_bodyAxisLength = length;
    update();
}

void CanvasScene::setBodyAxisWidth(qreal width)
{
    width = qMax(0.0, width);
    if (qFuzzyCompare(m_bodyAxisWidth, width))
    {
        return;
    }
    m_bodyAxisWidth = width;
    update();
}

void CanvasScene::setBodyAxisXColor(const QColor &color)
{
    if (m_bodyAxisXColor == color)
    {
        return;
    }
    m_bodyAxisXColor = color;
    update();
}

void CanvasScene::setBodyAxisYColor(const QColor &color)
{
    if (m_bodyAxisYColor == color)
    {
        return;
    }
    m_bodyAxisYColor = color;
    update();
}

void CanvasScene::setMaxPolygonVertices(int count)
{
    count = qBound(3, count, 64);
    if (m_maxPolygonVertices == count)
    {
        return;
    }
    m_maxPolygonVertices = count;
}

QStringList CanvasScene::solidBodyProblems(const QVector<ShapeItem *> &shapes) const
{
    QStringList problems;
    for (ShapeItem *shape : shapes)
    {
        const physics::Geometry geometry = shape->physicsGeometry();

        if (geometry.kind == physics::GeometryKind::Chain)
        {
            problems << tr("%1 is an open polyline, which encloses no area").arg(shape->name());
        }
        else if (geometry.kind == physics::GeometryKind::Polygon)
        {
            if (geometry.points.size() > m_maxPolygonVertices)
            {
                problems << tr("%1 has %2 points, over the %3 allowed for a solid shape")
                                    .arg(shape->name())
                                    .arg(geometry.points.size())
                                    .arg(m_maxPolygonVertices);
            }
            else if (!physics::isConvex(geometry.points))
            {
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
    {
        return;
    }
    m_sleepShiftPercent = percent;
    update();
}

void CanvasScene::setSimulationRunning(bool running)
{
    if (m_simulationRunning == running)
    {
        return;
    }
    m_simulationRunning = running;
    update();
    emit simulationRunningChanged(running);
}

bool CanvasScene::selectionIsWholeBody() const
{
    PhysicsBody *body = commonSelectedBody();
    return body && body->shapes().size() == m_physicsSelection.size();
}

void CanvasScene::setPixelsPerMeter(qreal pixelsPerMeter)
{
    pixelsPerMeter = qMax(1.0, pixelsPerMeter);
    if (qFuzzyCompare(m_world.pixelsPerMeter, pixelsPerMeter))
    {
        return;
    }
    m_world.pixelsPerMeter = pixelsPerMeter;
    emit fieldPropertyChanged();
}

void CanvasScene::setFieldBoundsSolid(bool solid)
{
    if (m_fieldBoundsSolid == solid)
    {
        return;
    }
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
    {
        return;
    }
    m_currentScale = scale;
    emit scaleChanged(m_currentScale);
    emit fieldPropertyChanged();
}

void CanvasScene::setScaleMin(qreal value)
{
    m_scaleMin = qMax(1.0, value);
    if (m_scaleMin > m_scaleMax)
    {
        m_scaleMax = m_scaleMin;
    }
    setCurrentScale(m_currentScale); // re-clamp against the new bound
}

void CanvasScene::setScaleMax(qreal value)
{
    m_scaleMax = qMax(1.0, value);
    if (m_scaleMax < m_scaleMin)
    {
        m_scaleMin = m_scaleMax;
    }
    setCurrentScale(m_currentScale); // re-clamp against the new bound
}

void CanvasScene::setScaleStep(qreal value)
{
    m_scaleStep = qMax(0.1, value);
}

void CanvasScene::drawBackground(QPainter *painter, const QRectF &rect)
{
    QColor background = m_backgroundColor;
    if (m_editorMode != EditorMode::Edit)
    {
        const QColor accent = EditorModes::accent(m_editorMode);
        constexpr qreal kTint = 0.06;
        background = QColor::fromRgbF(background.redF() * (1.0 - kTint) + accent.redF() * kTint,
                                      background.greenF() * (1.0 - kTint) + accent.greenF() * kTint,
                                      background.blueF() * (1.0 - kTint) + accent.blueF() * kTint, background.alphaF());
    }
    painter->fillRect(rect, background);

    if (!m_showGrid || !layerVisible(RunLayer::Grid))
    {
        return;
    }

    const QRectF gridRect = rect.intersected(sceneRect());
    if (gridRect.isEmpty())
    {
        return;
    }

    const qreal minorStep = m_gridCellSize;
    const qreal majorStep = m_gridCellSize * 5.0;

    QPen minorPen(m_gridColor);
    minorPen.setWidth(0);
    QPen majorPen(m_gridColor.darker(130));
    majorPen.setWidth(0);

    const qreal left = std::floor(gridRect.left() / minorStep) * minorStep;
    const qreal top = std::floor(gridRect.top() / minorStep) * minorStep;

    painter->setPen(minorPen);
    for (qreal x = left; x < gridRect.right(); x += minorStep)
    {
        if (!qFuzzyIsNull(std::fmod(x, majorStep)))
        {
            painter->drawLine(QPointF(x, gridRect.top()), QPointF(x, gridRect.bottom()));
        }
    }
    for (qreal y = top; y < gridRect.bottom(); y += minorStep)
    {
        if (!qFuzzyIsNull(std::fmod(y, majorStep)))
        {
            painter->drawLine(QPointF(gridRect.left(), y), QPointF(gridRect.right(), y));
        }
    }

    painter->setPen(majorPen);
    for (qreal x = std::floor(gridRect.left() / majorStep) * majorStep; x < gridRect.right(); x += majorStep)
    {
        painter->drawLine(QPointF(x, gridRect.top()), QPointF(x, gridRect.bottom()));
    }
    for (qreal y = std::floor(gridRect.top() / majorStep) * majorStep; y < gridRect.bottom(); y += majorStep)
    {
        painter->drawLine(QPointF(gridRect.left(), y), QPointF(gridRect.right(), y));
    }

    // The field's own edges, dark on all four sides. The loops above stop short
    // of the far edges -- and a field whose size is not a multiple of the major
    // step has no major line there at all -- so the grid used to end in a pale
    // line on the right and at the bottom. A line on the very edge would fall
    // on the pixel just outside, so the far ones are drawn a pixel in.
    const QRectF field = sceneRect();
    const qreal pixel = 1.0 / qMax(painter->transform().m11(), 1e-6);
    const auto edge = [&](const QPointF &from, const QPointF &to) {
        painter->drawLine(from, to);
    };
    if (rect.left() <= field.left() && field.left() <= rect.right())
    {
        edge(QPointF(field.left(), gridRect.top()), QPointF(field.left(), gridRect.bottom()));
    }
    if (rect.left() <= field.right() - pixel && field.right() - pixel <= rect.right())
    {
        edge(QPointF(field.right() - pixel, gridRect.top()), QPointF(field.right() - pixel, gridRect.bottom()));
    }
    if (rect.top() <= field.top() && field.top() <= rect.bottom())
    {
        edge(QPointF(gridRect.left(), field.top()), QPointF(gridRect.right(), field.top()));
    }
    if (rect.top() <= field.bottom() - pixel && field.bottom() - pixel <= rect.bottom())
    {
        edge(QPointF(gridRect.left(), field.bottom() - pixel), QPointF(gridRect.right(), field.bottom() - pixel));
    }

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
    {
        return;
    }
    for (ShapeItem *shape : std::as_const(m_editSelection))
    {
        shape->setCoSelected(false);
    }
    m_editSelection.clear();
    m_groupOriginPlaced = false;
    emit editSelectionChanged();
}

void CanvasScene::addToEditSelection(ShapeItem *shape)
{
    if (!shape || shape->scene() != this || m_editorMode != EditorMode::Edit)
    {
        return;
    }

    if (!m_active)
    {
        activate(shape);
        return;
    }

    if (shape == m_active)
    {
        // Dropping the shape that carries the handles hands the role to the
        // next one picked, so the rest of the group stays selected instead of
        // falling away with it.
        QVector<ShapeItem *> rest = m_editSelection;
        ShapeItem *promoted = rest.isEmpty() ? nullptr : rest.takeFirst();
        clearEditSelection();
        if (!promoted)
        {
            deactivate();
            return;
        }
        activate(promoted);
        for (ShapeItem *other : std::as_const(rest))
        {
            m_editSelection.append(other);
            other->setCoSelected(true);
        }
        m_groupOriginPlaced = false;
        refreshGroupOrigin();
        emit editSelectionChanged();
        return;
    }

    if (m_editSelection.removeOne(shape))
    {
        shape->setCoSelected(false);
    }
    else
    {
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
    if (m_editSelection.isEmpty() || !m_active)
    {
        m_groupOriginPlaced = false;
        return;
    }
    if (m_groupOriginPlaced)
    {
        return;
    }

    // The centre of everything picked, which is where a group is expected to
    // turn about until it is told otherwise.
    QRectF bounds = m_active->sceneBoundingRect();
    for (ShapeItem *shape : std::as_const(m_editSelection))
    {
        bounds = bounds.united(shape->sceneBoundingRect());
    }
    m_groupOrigin = bounds.center();
    update();
}

QVector<ShapeItem *> CanvasScene::selectedShapes() const
{
    QVector<ShapeItem *> all;
    if (m_active)
    {
        all.append(m_active);
    }
    all.append(m_editSelection);
    return all;
}

QRectF CanvasScene::editSelectionBounds() const
{
    if (!m_active || m_editSelection.isEmpty())
    {
        return QRectF();
    }
    QRectF bounds = m_active->mapToScene(m_active->rect()).boundingRect();
    for (ShapeItem *shape : std::as_const(m_editSelection))
    {
        bounds = bounds.united(shape->mapToScene(shape->rect()).boundingRect());
    }
    return bounds;
}

QRectF CanvasScene::contentBounds() const
{
    // Joints are left out: their anchors sit on the shapes they hold.
    QRectF bounds;
    for (ShapeItem *shape : shapes())
    {
        bounds = bounds.united(shape->mapToScene(shape->rect()).boundingRect());
    }
    for (RayItem *ray : m_rays)
    {
        bounds = bounds.united(ray->sceneBoundingRect());
    }
    for (ExplosionItem *explosion : m_explosions)
    {
        bounds = bounds.united(explosion->sceneBoundingRect());
    }
    return bounds;
}

QRectF CanvasScene::editSelectionBox() const
{
    const QRectF bounds = editSelectionBounds();
    if (bounds.isNull())
    {
        return bounds;
    }
    constexpr qreal kClearance = 6.0;
    return bounds.adjusted(-kClearance, -kClearance, kClearance, kClearance);
}

bool CanvasScene::editSelectionRotating() const
{
    return m_active && !m_editSelection.isEmpty() && m_active->mode() == ShapeMode::Rotating;
}

QVector<QPointF> CanvasScene::groupHandlePoints() const
{
    const QRectF b = editSelectionBounds();
    if (b.isNull())
    {
        return {};
    }
    // Corners only. A group scales by one factor -- squashing it along one axis
    // cannot be expressed as a rect and an angle once a member is turned.
    return {b.topLeft(), b.topRight(), b.bottomRight(), b.bottomLeft()};
}

int CanvasScene::groupHandleAt(const QPointF &scenePos) const
{
    if (editSelectionRotating())
    {
        return -1;
    }
    const QVector<QPointF> corners = groupHandlePoints();
    const qreal reach = handleSize();
    for (int i = 0; i < corners.size(); ++i)
    {
        if (QLineF(corners.at(i), scenePos).length() <= reach)
        {
            return i;
        }
    }
    return -1;
}

void CanvasScene::beginGroupScale(int handle)
{
    const QVector<QPointF> corners = groupHandlePoints();
    if (handle < 0 || handle >= corners.size())
    {
        return;
    }

    // The corner across the box stays put, so the group grows away from it.
    m_groupScaleAnchor = corners.at((handle + 2) % 4);
    m_groupScaleStart = corners.at(handle);

    m_groupScaleStartRects.clear();
    m_groupScaleStartOrigins.clear();
    m_groupScaleStartOriginScene.clear();
    for (ShapeItem *shape : selectedShapes())
    {
        m_groupScaleStartRects.append(shape->rect());
        m_groupScaleStartOrigins.append(shape->origin());
        m_groupScaleStartOriginScene.append(shape->pos() + shape->origin());
    }
}

void CanvasScene::applyGroupScale(qreal factor)
{
    const QVector<ShapeItem *> shapes = selectedShapes();
    if (shapes.size() != m_groupScaleStartRects.size())
    {
        return;
    }

    for (int i = 0; i < shapes.size(); ++i)
    {
        ShapeItem *shape = shapes.at(i);
        const QRectF r = m_groupScaleStartRects.at(i);
        const QPointF origin = m_groupScaleStartOrigins.at(i) * factor;
        // A shape scales about its own local zero and then moves so that its
        // origin sits where the group scale puts it. Rotation is untouched,
        // which is what keeps a turned shape from shearing.
        shape->applyRect(QRectF(r.topLeft() * factor, r.size() * factor));
        shape->setOrigin(origin);
        shape->setPos(m_groupScaleAnchor + (m_groupScaleStartOriginScene.at(i) - m_groupScaleAnchor) * factor - origin);
    }
    m_groupOriginPlaced = false;
    refreshGroupOrigin();
    update();
}

bool CanvasScene::groupOriginHandleContains(const QPointF &scenePos) const
{
    if (m_editSelection.isEmpty())
    {
        return false;
    }
    return QLineF(m_groupOrigin, scenePos).length() <= kGroupOriginRadius + 3.0;
}

void CanvasScene::beginGroupDrag()
{
    m_groupLeadStart = m_active ? m_active->pos() : QPointF();
    m_groupLeadStartRotation = m_active ? m_active->rotation() : 0.0;
    m_groupStartPositions.clear();
    m_groupStartRotations.clear();
    for (ShapeItem *shape : std::as_const(m_editSelection))
    {
        m_groupStartPositions.append(shape->pos());
        m_groupStartRotations.append(shape->rotation());
    }
}

void CanvasScene::activate(ShapeItem *item)
{
    if (m_active == item)
    {
        return;
    }
    // Picking a different shape outright starts a new selection; Shift and
    // Ctrl go through addToEditSelection instead.
    clearEditSelection();
    if (m_active)
    {
        m_active->setMode(ShapeMode::Idle);
        m_active->setSelectedNodes({});
    }
    m_selectedNodes.clear();
    m_active = item;
    if (m_active)
    {
        m_active->setMode(ShapeMode::Selected);
    }
    emit activeItemChanged(m_active);
}

void CanvasScene::deactivate()
{
    clearEditSelection();
    if (!m_active)
    {
        return;
    }
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
    {
        m_active->setSelectedNodes(m_selectedNodes);
    }
}

QPointF CanvasScene::snapScenePoint(const QPointF &scenePoint) const
{
    if (!m_snapToGrid || m_snapSuspended)
    {
        return scenePoint;
    }

    QPointF snapped = scenePoint;

    const qreal roundedX = qRound(scenePoint.x() / m_snapStep) * m_snapStep;
    if (qAbs(roundedX - scenePoint.x()) <= m_snapSensitivity)
    {
        snapped.setX(roundedX);
    }

    const qreal roundedY = qRound(scenePoint.y() / m_snapStep) * m_snapStep;
    if (qAbs(roundedY - scenePoint.y()) <= m_snapSensitivity)
    {
        snapped.setY(roundedY);
    }

    return snapped;
}

void CanvasScene::switchActiveToSelected()
{
    if (m_active && m_active->mode() != ShapeMode::Selected)
    {
        m_active->setMode(ShapeMode::Selected);
        setNodeSelection({});
        emit activeItemChanged(m_active);
    }
}

void CanvasScene::selectShape(ShapeItem *shape)
{
    if (!shape || shape->scene() != this)
    {
        return;
    }
    activate(shape);
}

void CanvasScene::switchActiveToEditing()
{
    if (!geometryEditingAllowed())
    {
        return;
    }
    if (m_active && m_active->supportsNodeEditing() && m_active->mode() != ShapeMode::Editing)
    {
        m_active->setMode(ShapeMode::Editing);
        emit activeItemChanged(m_active);
    }
}

void CanvasScene::switchActiveToRotating()
{
    if (!geometryEditingAllowed())
    {
        return;
    }
    if (m_active && m_active->mode() != ShapeMode::Rotating)
    {
        m_active->setMode(ShapeMode::Rotating);
        setNodeSelection({});
        emit activeItemChanged(m_active);
    }
}

void CanvasScene::deleteActiveItem()
{
    if (!m_active)
    {
        return;
    }

    if (m_active->mode() == ShapeMode::Editing && !m_selectedNodes.isEmpty())
    {
        QList<int> indices(m_selectedNodes.begin(), m_selectedNodes.end());
        std::sort(indices.begin(), indices.end(), std::greater<int>());
        for (int index : indices)
        {
            m_active->deleteNode(index);
        }
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
    for (ShapeItem *item : std::as_const(doomed))
    {
        wasPicked = m_physicsSelection.removeOne(item) || wasPicked;
        removeItem(item);
        delete item;
    }

    emit activeItemChanged(nullptr);
    emit editSelectionChanged();
    emit shapesChanged();
    if (wasPicked)
    {
        emit physicsSelectionChanged();
    }
    notifyEdit(count > 1 ? tr("Delete %n shapes", nullptr, count) : tr("Delete %1").arg(name));
}

void CanvasScene::startPolygonDrawing()
{
    if (!geometryEditingAllowed())
    {
        return;
    }
    deactivate();
    m_dragMode = DragMode::None;
    m_polygonDrawing = true;
    m_polygonScenePoints.clear();
    emit polygonDrawingChanged(true);
    update();
}

void CanvasScene::finishPolygonDrawing(bool closed)
{
    if (m_polygonScenePoints.size() >= 2)
    {
        const QRectF bounds = m_polygonScenePoints.boundingRect();
        const QPointF itemPos = bounds.topLeft();

        QPolygonF localPoints;
        localPoints.reserve(m_polygonScenePoints.size());
        for (const QPointF &p : std::as_const(m_polygonScenePoints))
        {
            localPoints << (p - itemPos);
        }

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
    if (editSelectionRotating() && !simulationRunning())
    {
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);
        painter->setPen(QPen(QColor(64, 64, 64), 1));
        painter->setBrush(QColor(76, 175, 80));
        painter->drawEllipse(m_groupOrigin, kGroupOriginRadius, kGroupOriginRadius);

        const qreal reach = kGroupOriginRadius + 4.0;
        painter->setPen(QPen(QColor(220, 50, 50), 1.5)); // X axis, red
        painter->drawLine(m_groupOrigin + QPointF(-reach, 0), m_groupOrigin + QPointF(reach, 0));
        painter->setPen(QPen(QColor(40, 160, 60), 1.5)); // Y axis, green
        painter->drawLine(m_groupOrigin + QPointF(0, -reach), m_groupOrigin + QPointF(0, reach));
        painter->restore();
    }

    // Moving or scaling: the box round everything picked, with a handle at each
    // corner. It stands in for the handles a single shape would draw.
    if (!m_editSelection.isEmpty() && m_active && !editSelectionRotating() && m_editorMode == EditorMode::Edit
        && !simulationRunning())
    {
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);

        QPen boxPen(m_selectionColor);
        boxPen.setWidthF(1.0);
        boxPen.setStyle(m_selectionLineStyle);
        boxPen.setCosmetic(true);
        painter->setPen(boxPen);
        painter->setBrush(Qt::NoBrush);
        painter->drawRect(editSelectionBox());

        painter->setPen(QPen(throughHandle(m_handleBorderColor), m_handleBorderWidth));
        painter->setBrush(throughHandle(m_handleColor));
        const qreal size = m_handleSize;
        for (const QPointF &corner : groupHandlePoints())
        {
            const QRectF r(corner.x() - size / 2, corner.y() - size / 2, size, size);
            if (m_handleShape == HandleShape::Circle)
            {
                painter->drawEllipse(r);
            }
            else
            {
                painter->drawRect(r);
            }
        }
        painter->restore();
    }

    if (m_editorMode != EditorMode::Edit && !m_joints.isEmpty() && layerVisible(RunLayer::Joints))
    {
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);

        for (Joint *joint : std::as_const(m_joints))
        {
            // Broken by a rule: gone from the world, so it is gone from the
            // picture too. It comes back when the run ends.
            if (joint->isBroken())
            {
                continue;
            }
            const bool selected = joint == m_selectedJoint && !simulationRunning();

            // Colour and line style belong to the joint's kind, which every
            // engine tags its types with.
            const physics::JointVisual kind = jointVisual(joint->typeId());
            const QColor kindColor = jointKindColor(kind);
            const JointStyle kindStyle = jointKindStyle(kind);
            const auto penStyle = [](JointStyle style) {
                switch (style)
                {
                case JointStyle::Dashed:
                    return Qt::DashLine;
                case JointStyle::Dotted:
                    return Qt::DotLine;
                case JointStyle::DashDot:
                    return Qt::DashDotLine;
                case JointStyle::Rod:
                case JointStyle::Solid:
                    break;
                }
                return Qt::SolidLine;
            };

            const int anchors = joint->anchorCount();
            const QPointF a = anchors > 0 ? joint->anchorScenePos(Joint::End::A)
                                          : joint->bodyA()->centerOfMassScenePos();
            const QPointF b = anchors > 1                      ? joint->anchorScenePos(Joint::End::B)
                              : anchors > 0 || !joint->bodyB() ? a
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

            // A joint with one body pulls it towards a point, and that point
            // is nowhere near the body it acts on -- that is the whole use of
            // it. Drawn on its own it is a ring floating in empty space with
            // nothing saying what it belongs to, so a dotted leader runs back
            // to the body: dotted because it is a reach, not a linkage, and
            // nothing is attached along it.
            if (!joint->bodyB() && joint->bodyA())
            {
                QPen leader(kindColor.darker(180));
                leader.setWidthF(1.4);
                leader.setCosmetic(true);
                leader.setStyle(Qt::DotLine);
                painter->setPen(leader);
                painter->setBrush(Qt::NoBrush);
                painter->drawLine(a, b);
            }

            // A sliding joint is the same bone every other joint draws --
            // two anchors joined by a shaft -- with the axis it slides along
            // drawn behind it. Only the axis is special; the anchors and the
            // connection between them are not.
            if (kind == physics::JointVisual::Axis)
            {
                const QPointF along = joint->axisScene();
                const QPointF across(-along.y(), along.x());

                const QVariantMap &params = joint->params();
                qreal lower = params.value(QStringLiteral("lowerTranslation")).toDouble();
                qreal upper = params.value(QStringLiteral("upperTranslation")).toDouble();
                if (lower > upper)
                {
                    std::swap(lower, upper);
                }
                const bool limited = params.value(QStringLiteral("enableLimit")).toBool()
                                     && !qFuzzyCompare(lower, upper);
                if (!limited)
                {
                    // Unlimited travel has no length to draw, so this is a
                    // direction explosion rather than a measurement -- the same
                    // thing the body axis cross is, and sized the same way,
                    // from a setting rather than from anything in the scene.
                    upper = m_jointAxisLength;
                    lower = -upper;
                }

                // Measured from the second anchor, not the first. A sliding
                // joint's travel is where end B stands relative to end A, and
                // it reads zero where the joint was made -- so zero is at
                // anchor B, wherever that was dropped. Hanging the band off
                // anchor A instead drew the whole range in the wrong place the
                // moment the two anchors were not on the same point: a lift
                // whose rail runs up and away had its limits drawn running
                // down and away, off the picture entirely.
                const QPointF from = b + along * lower;
                const QPointF to = b + along * upper;

                // Darker than the joint's own colour and as thick as its shaft:
                // the pale type colour at outline width disappears against a
                // body fill, which is exactly what it is drawn on top of.
                const QColor axisColor = kindColor.darker(160);
                QPen axisPen(axisColor);
                axisPen.setWidthF(qMax(waist * 2.0, 1.5));
                axisPen.setCapStyle(Qt::FlatCap);
                if (!limited)
                {
                    axisPen.setDashPattern({3.0, 2.0});
                }
                painter->setPen(axisPen);
                painter->setBrush(Qt::NoBrush);
                painter->drawLine(from, to);

                if (limited)
                {
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

            if (anchors == 0)
            {
                QPainterPath link;
                link.moveTo(a);
                link.lineTo(b);

                QPen linkPen(kindColor);
                linkPen.setWidthF(m_jointOutlineWidth);
                linkPen.setStyle(penStyle(kindStyle));
                painter->setPen(linkPen);
                painter->setBrush(Qt::NoBrush);
                painter->drawPath(link);

                if (selected)
                {
                    QPainterPathStroker body;
                    body.setWidth(qMax(m_jointOutlineWidth, 1.0));
                    markSelected(body.createStroke(link));
                }
                continue;
            }

            const QLineF span(a, b);
            QPainterPath shaft;
            if (span.length() > 0.01)
            {
                if (kindStyle == JointStyle::Rod)
                {
                    const QPointF along = (b - a) / span.length();
                    const QPointF across(-along.y() * waist, along.x() * waist);

                    QPainterPath neck;
                    neck.moveTo(a + across);
                    neck.lineTo(b + across);
                    neck.lineTo(b - across);
                    neck.lineTo(a - across);
                    neck.closeSubpath();
                    bone = bone.united(neck);
                }
                else
                {
                    // Drawn as a line rather than cut out of the shape, so a
                    // dashed or dotted joint reads as one line and not as a
                    // row of filled slivers.
                    shaft.moveTo(a);
                    shaft.lineTo(b);
                }

                QPainterPath ringB;
                ringB.addEllipse(b, ring, ring);
                bone = bone.united(ringB);
            }

            if (!shaft.isEmpty())
            {
                QPen shaftPen(kindColor);
                shaftPen.setWidthF(qMax(m_jointWaistWidth, 1.0));
                shaftPen.setStyle(penStyle(kindStyle));
                shaftPen.setCapStyle(Qt::FlatCap);
                painter->setPen(shaftPen);
                painter->setBrush(Qt::NoBrush);
                painter->drawPath(shaft);
            }

            // The anchors, outline and all, at the opacity the settings ask
            // for: they sit on top of the shapes they join, and an opaque ring
            // hides exactly the thing it is attached to.
            const qreal fullOpacity = painter->opacity();
            painter->setOpacity(fullOpacity * m_jointAnchorOpacity / 100.0);

            QPen outlinePen(m_jointOutlineColor);
            outlinePen.setWidthF(m_jointOutlineWidth);
            outlinePen.setJoinStyle(Qt::RoundJoin);
            painter->setPen(outlinePen);
            QColor fill = kindColor;
            fill.setAlpha(qMin(fill.alpha(), m_jointFillAlpha));
            painter->setBrush(fill);
            painter->drawPath(bone);

            painter->setBrush(Qt::NoBrush);
            painter->setPen(QPen(m_jointOutlineColor, m_jointOutlineWidth));
            painter->drawEllipse(a, ring * 0.55, ring * 0.55);
            if (span.length() > 0.01)
            {
                painter->drawEllipse(b, ring * 0.55, ring * 0.55);
            }
            painter->setOpacity(fullOpacity);

            if (selected)
            {
                // The halo follows what was actually drawn, line included.
                QPainterPath marked = bone;
                if (!shaft.isEmpty())
                {
                    QPainterPathStroker line;
                    line.setWidth(qMax(m_jointWaistWidth, 1.0));
                    marked = marked.united(line.createStroke(shaft));
                }
                markSelected(marked);
            }
        }
        painter->restore();
    }

    if (m_editorMode == EditorMode::Physics && m_showBodyAxes && layerVisible(RunLayer::BodyAxes))
    {
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);

        for (PhysicsBody *body : std::as_const(m_bodies))
        {
            // Removed by a rule: gone from the world, so its axes go with it.
            // They come back when the run ends.
            if (body->isEmpty() || body->isRemoved())
            {
                continue;
            }

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

    if (isAimingShot())
    {
        drawShot(painter);
    }

    if (!m_polygonDrawing || m_polygonScenePoints.isEmpty())
    {
        return;
    }

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
    {
        painter->drawEllipse(p, kPointRadius, kPointRadius);
    }
}

bool CanvasScene::beginShot(const QPointF &scenePos)
{
    if (!simulationRunning())
    {
        return false;
    }
    // The topmost body under the pointer that can be shot, looking through any
    // that cannot: a ball lying under a table is still a ball to shoot.
    for (ShapeItem *shape : shapesAt(scenePos))
    {
        PhysicsBody *body = shape->body();
        if (!body || body->isRemoved() || !body->shot().enabled || body->props().type != physics::BodyType::Dynamic)
        {
            continue;
        }
        m_shotBody = body;
        m_shotCursor = scenePos;
        update();
        return true;
    }
    return false;
}

QVector<ShapeItem *> CanvasScene::shapesAt(const QPointF &scenePos) const
{
    QVector<ShapeItem *> under;
    for (QGraphicsItem *item : items(scenePos))
    {
        if (auto *shape = qgraphicsitem_cast<ShapeItem *>(item))
        {
            under.append(shape);
        }
    }
    return under;
}

void CanvasScene::aimShot(const QPointF &scenePos)
{
    if (!isAimingShot())
    {
        return;
    }
    m_shotCursor = scenePos;
    update();
}

QPointF CanvasScene::shotImpulse() const
{
    if (!isAimingShot())
    {
        return {};
    }
    const ShotSettings &shot = m_shotBody->shot();
    const QPointF pull = m_shotCursor - m_shotBody->centerOfMassScenePos();
    const qreal length = std::hypot(pull.x(), pull.y());
    if (length < 1e-6 || shot.maxPull <= 0.0)
    {
        return {};
    }
    const qreal power = qMin(length / shot.maxPull, 1.0);
    // Pulled back, flung forward: the push points away from the pull.
    return -pull / length * power * shot.fullImpulse;
}

void CanvasScene::setShotLineColors(const QColor &light, const QColor &full)
{
    m_shotLightColor = light;
    m_shotFullColor = full;
    update();
}

void CanvasScene::setShotLineWidth(qreal width)
{
    m_shotLineWidth = width;
    update();
}

void CanvasScene::setShotLineStyle(Qt::PenStyle style)
{
    m_shotLineStyle = style;
    update();
}

void CanvasScene::releaseShot()
{
    if (!isAimingShot())
    {
        return;
    }
    PhysicsBody *body = m_shotBody;
    const QPointF impulse = shotImpulse();
    m_shotBody = nullptr;
    update();
    if (m_bodies.contains(body) && !impulse.isNull())
    {
        emit shotReleased(body, impulse);
    }
}

void CanvasScene::cancelShot()
{
    if (!m_shotBody)
    {
        return;
    }
    m_shotBody = nullptr;
    update();
}

void CanvasScene::drawShot(QPainter *painter) const
{
    const ShotSettings &shot = m_shotBody->shot();
    const QPointF centre = m_shotBody->centerOfMassScenePos();
    const QPointF pull = m_shotCursor - centre;
    const qreal length = std::hypot(pull.x(), pull.y());
    if (length < 1e-6 || shot.maxPull <= 0.0)
    {
        return;
    }
    const qreal power = qMin(length / shot.maxPull, 1.0);

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);

    // One line from the body to the pointer, coloured by the force it would
    // apply: the light-pull colour with little in it, the full-pull colour at
    // full power, and the shades between.
    const auto mix = [power](int light, int full) { return qRound(light + (full - light) * power); };
    const QColor colour(mix(m_shotLightColor.red(), m_shotFullColor.red()),
                        mix(m_shotLightColor.green(), m_shotFullColor.green()),
                        mix(m_shotLightColor.blue(), m_shotFullColor.blue()),
                        mix(m_shotLightColor.alpha(), m_shotFullColor.alpha()));
    QPen line(colour);
    line.setCosmetic(true);
    line.setWidthF(m_shotLineWidth);
    line.setStyle(m_shotLineStyle);
    line.setCapStyle(Qt::RoundCap);
    painter->setPen(line);
    painter->drawLine(centre, m_shotCursor);

    painter->restore();
}

void CanvasScene::keyPressEvent(QKeyEvent *event)
{
    if (isAimingShot() && event->key() == Qt::Key_Escape)
    {
        cancelShot();
        event->accept();
        return;
    }

    if (m_polygonDrawing)
    {
        if (event->key() == Qt::Key_Escape)
        {
            cancelPolygonDrawing();
            event->accept();
            return;
        }
        if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter)
        {
            finishPolygonDrawing(event->modifiers() & Qt::ShiftModifier);
            event->accept();
            return;
        }
        event->accept();
        return;
    }

    if (m_active && m_active->mode() == ShapeMode::Editing
        && (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter))
    {
        handleEditModeEnter();
        event->accept();
        return;
    }

    // One scene unit an arrow press, deliberately under the grid: nudging is
    // for the placement a drag cannot land on, so it never snaps.
    QPointF step;
    switch (event->key())
    {
    case Qt::Key_Left:
        step = QPointF(-1.0, 0.0);
        break;
    case Qt::Key_Right:
        step = QPointF(1.0, 0.0);
        break;
    case Qt::Key_Up:
        step = QPointF(0.0, -1.0);
        break;
    case Qt::Key_Down:
        step = QPointF(0.0, 1.0);
        break;
    default:
        break;
    }
    if (!step.isNull() && nudgeSelection(step))
    {
        event->accept();
        return;
    }

    QGraphicsScene::keyPressEvent(event);
}

bool CanvasScene::nudgeSelection(const QPointF &delta)
{
    // A run owns the positions. Setting one there does not move a body, it
    // teleports it: past the solver, through whatever was in the way.
    if (!selectionAllowed())
    {
        return false;
    }

    if (m_editorMode == EditorMode::Edit)
    {
        // With nodes picked the arrows belong to them rather than to the
        // shape holding them -- the same as Delete.
        if (m_active && m_active->mode() == ShapeMode::Editing && !m_selectedNodes.isEmpty())
        {
            // A local vector, not a scene one: the shape may be turned, and a
            // node moves in the shape's own frame.
            const QPointF local = m_active->mapFromScene(m_active->mapToScene(QPointF()) + delta);
            for (int index : std::as_const(m_selectedNodes))
            {
                m_active->moveNode(index, m_active->nodePosition(index) + local);
            }
            update();
            notifyEdit(tr("Move %n node(s) in %1", nullptr, int(m_selectedNodes.size())).arg(m_active->name()),
                       QStringLiteral("nudge"));
            return true;
        }

        if (!m_active)
        {
            return false;
        }

        QVector<ShapeItem *> moving = m_editSelection;
        moving.prepend(m_active);
        for (ShapeItem *shape : std::as_const(moving))
        {
            shape->setPos(shape->pos() + delta);
        }
        update();
        notifyEdit(moving.size() > 1 ? tr("Move %n shapes", nullptr, int(moving.size()))
                                     : tr("Move %1").arg(m_active->name()),
                   QStringLiteral("nudge"));
        return true;
    }

    // Physics mode. A body moves whole, or the joints anchored to it are left
    // behind by the shapes they were holding.
    QVector<ShapeItem *> moving;
    for (ShapeItem *shape : std::as_const(m_physicsSelection))
    {
        if (PhysicsBody *body = shape->body())
        {
            for (ShapeItem *member : body->shapes())
            {
                if (!moving.contains(member))
                {
                    moving.append(member);
                }
            }
        }
        else if (!moving.contains(shape))
        {
            moving.append(shape);
        }
    }

    QString what;
    if (!moving.isEmpty())
    {
        for (ShapeItem *shape : std::as_const(moving))
        {
            shape->setPos(shape->pos() + delta);
        }
        PhysicsBody *body = commonSelectedBody();
        what = body ? body->name() : moving.first()->name();
    }
    if (m_selectedRay)
    {
        m_selectedRay->setPos(m_selectedRay->pos() + delta);
        what = m_selectedRay->name();
    }
    if (m_selectedExplosion)
    {
        m_selectedExplosion->setPos(m_selectedExplosion->pos() + delta);
        what = m_selectedExplosion->name();
    }
    if (what.isEmpty())
    {
        return false;
    }

    update();
    notifyEdit(tr("Move %1").arg(what), QStringLiteral("nudge"));
    return true;
}

void CanvasScene::handleEditModeEnter()
{
    if (!m_active || m_selectedNodes.size() != 2)
    {
        return;
    }

    QList<int> indices(m_selectedNodes.begin(), m_selectedNodes.end());
    std::sort(indices.begin(), indices.end());
    const int i = indices[0];
    const int j = indices[1];
    const int count = m_active->nodeCount();

    const bool linearAdjacent = (j == i + 1);
    const bool wrapAdjacent = m_active->isClosed() && i == 0 && j == count - 1;
    const bool openEndpoints = !m_active->isClosed() && i == 0 && j == count - 1 && !linearAdjacent;

    if (openEndpoints)
    {
        m_active->closeShape();
    }
    else if (linearAdjacent || wrapAdjacent)
    {
        m_active->insertNodeBetween(i, j);
    }
    else
    {
        return;
    }

    setNodeSelection({});
}

void CanvasScene::mousePressEvent(QGraphicsSceneMouseEvent *event)
{
    m_snapSuspended = event->modifiers().testFlag(Qt::ShiftModifier);

    // The slingshot has the mouse while it is aiming: any other button calls
    // the shot off. A left press on a body that can be shot starts one; a
    // press anywhere else goes on to do what it always did.
    if (isAimingShot())
    {
        if (event->button() != Qt::LeftButton)
        {
            cancelShot();
        }
        event->accept();
        return;
    }
    if (event->button() == Qt::LeftButton && beginShot(event->scenePos()))
    {
        event->accept();
        return;
    }

    if (m_polygonDrawing)
    {
        if (event->button() == Qt::LeftButton)
        {
            m_polygonScenePoints << event->scenePos();
            update();
        }
        return;
    }

    if (event->button() != Qt::LeftButton)
    {
        QGraphicsScene::mousePressEvent(event);
        return;
    }

    const QPointF scenePos = event->scenePos();

    if (!selectionAllowed())
    {
        // A run blocks selection, not navigation: without this the field
        // freezes exactly when there is something moving to follow across it.
        if (event->button() == Qt::LeftButton)
        {
            m_dragMode = DragMode::PanField;
            m_panLastScreenPos = event->screenPos();
        }
        event->accept();
        return;
    }

    if (m_editorMode == EditorMode::Physics)
    {
        int end = 0;
        if (Joint *joint = jointAt(scenePos, &end))
        {
            selectJoint(joint);
            if (end != kJointShaft)
            {
                m_draggedJoint = joint;
                m_draggedJointEnd = end;
            }
            event->accept();
            return;
        }
        selectJoint(nullptr);
    }

    if (m_editorMode != EditorMode::Edit)
    {
        // A ray or explosion sits above the shapes and is picked first, so
        // one dropped on top of something can still be grabbed.
        for (QGraphicsItem *candidate : items(scenePos))
        {
            auto *ray = qgraphicsitem_cast<RayItem *>(candidate);
            if (!ray)
            {
                continue;
            }
            selectRay(ray);
            if (event->button() == Qt::LeftButton)
            {
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

        for (QGraphicsItem *candidate : items(scenePos))
        {
            auto *explosion = qgraphicsitem_cast<ExplosionItem *>(candidate);
            if (!explosion)
            {
                continue;
            }
            selectExplosion(explosion);
            if (event->button() == Qt::LeftButton)
            {
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

        const QVector<ShapeItem *> under = shapesAt(scenePos);
        ShapeItem *current = m_physicsSelection.size() == 1 ? m_physicsSelection.first() : nullptr;
        ShapeItem *hit = nullptr;
        if (event->modifiers().testFlag(Qt::ControlModifier) && !under.isEmpty())
        {
            // Ctrl+click steps down through the shapes under the pointer: the
            // one beneath the current selection, and round to the top again.
            hit = under.at((under.indexOf(current) + 1) % under.size());
        }
        else
        {
            // A press on the shape already selected keeps it, even where
            // another lies on top of it -- so a shape reached with Ctrl+click
            // can still be dragged, or double-clicked into a body.
            hit = current && under.contains(current) ? current : under.value(0, nullptr);
        }
        selectForPhysics(hit, event->modifiers().testFlag(Qt::ShiftModifier));
        // Dragging empty field pans it, the same as in Edit mode. Without this
        // a click outside every shape only cleared the selection, so the field
        // could not be moved at all once out of Edit mode.
        if (!hit)
        {
            m_dragMode = DragMode::PanField;
            m_panLastScreenPos = event->screenPos();
        }
        else if (event->button() == Qt::LeftButton)
        {
            // Dragging a shape moves it, as in Edit mode -- but a whole body at
            // once, since moving one shape out of a body would deform it.
            m_bodyDragShapes.clear();
            m_bodyDragStartPositions.clear();
            if (PhysicsBody *body = hit->body())
            {
                m_bodyDragShapes = body->shapes();
                m_bodyDragLabel = body->name();
            }
            else
            {
                m_bodyDragShapes = {hit};
                m_bodyDragLabel = hit->name();
            }
            for (ShapeItem *shape : std::as_const(m_bodyDragShapes))
            {
                m_bodyDragStartPositions.append(shape->pos());
            }
            m_dragMode = DragMode::MoveBody;
            m_lastScenePos = scenePos;
            // Tracked against the leading piece, since that is what the offset
            // is measured from -- the clicked shape may not be that one.
            m_moveDragVirtualPos = m_bodyDragStartPositions.value(0);
        }
        event->accept();
        return;
    }

    // Ctrl+click steps down through the shapes under the pointer: the one
    // beneath the active shape, and round to the top again.
    if (event->modifiers().testFlag(Qt::ControlModifier))
    {
        const QVector<ShapeItem *> under = shapesAt(scenePos);
        if (!under.isEmpty())
        {
            ShapeItem *next = under.at((under.indexOf(m_active) + 1) % under.size());
            if (next != m_active)
            {
                clearEditSelection();
                deactivate();
                activate(next);
            }
            event->accept();
            return;
        }
    }

    if (m_active)
    {
        const QPointF local = m_active->mapFromScene(scenePos);

        // Shift adds a shape to the selection instead of replacing it. Node
        // editing spends Shift on picking vertices, so it is left alone there.
        if (event->modifiers().testFlag(Qt::ShiftModifier) && m_active->mode() != ShapeMode::Editing)
        {
            ShapeItem *picked = nullptr;
            for (QGraphicsItem *candidate : items(scenePos))
            {
                if (auto *shape = qgraphicsitem_cast<ShapeItem *>(candidate))
                {
                    picked = shape;
                    break;
                }
            }
            if (picked)
            {
                addToEditSelection(picked);
                event->accept();
                return;
            }
        }

        // The group's own handles sit on top of the shapes, so they are tested
        // before anything under them.
        const int corner = groupHandleAt(scenePos);
        if (corner >= 0)
        {
            m_dragMode = DragMode::GroupScale;
            beginGroupScale(corner);
            event->accept();
            return;
        }

        // The pivot sits on top of the shapes too, for the same reason.
        if (groupOriginHandleContains(scenePos))
        {
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
            {
                return false;
            }
            if (m_active->shapeContains(local))
            {
                m_groupClickCandidate = m_active;
                return true;
            }
            for (ShapeItem *other : std::as_const(m_editSelection))
            {
                if (other->shapeContains(other->mapFromScene(scenePos)))
                {
                    m_groupClickCandidate = other;
                    return true;
                }
            }
            return false;
        };

        if (m_active->mode() == ShapeMode::Rotating)
        {
            if (m_active->originHandleContains(local))
            {
                m_dragMode = DragMode::Origin;
                return;
            }
            if (m_active->shapeContains(local) || pressedInGroup())
            {
                m_dragMode = DragMode::Rotate;
                // A group turns about its own pivot; a single shape about its
                // origin handle.
                m_dragOriginScene = m_editSelection.isEmpty() ? m_active->mapToScene(m_active->origin())
                                                              : m_groupOrigin;
                m_rotateStartAngle = angleAt(m_dragOriginScene, scenePos);
                m_itemStartRotation = m_active->rotation();
                beginGroupDrag();
                return;
            }
        }
        else if (m_active->mode() == ShapeMode::Editing)
        {
            const int node = m_active->nodeAt(local);
            if (node >= 0)
            {
                if (event->modifiers() & Qt::ShiftModifier)
                {
                    QSet<int> selection = m_selectedNodes;
                    if (selection.contains(node))
                    {
                        selection.remove(node);
                    }
                    else
                    {
                        selection.insert(node);
                    }
                    setNodeSelection(selection);
                    return;
                }
                if (!m_selectedNodes.contains(node))
                {
                    setNodeSelection({node});
                }

                m_dragMode = DragMode::EditNode;
                m_editNodeIndex = node;
                m_editDragNodeStart.clear();
                for (int idx : std::as_const(m_selectedNodes))
                {
                    m_editDragNodeStart[idx] = m_active->nodePosition(idx);
                }
                return;
            }
            if (m_active->shapeContains(local))
            {
                setNodeSelection({});
                return;
            }
        }
        else if (m_active->mode() == ShapeMode::Selected)
        {
            const HandleId handle = geometryEditingAllowed() ? m_active->handleAt(local) : HandleId::None;
            if (handle != HandleId::None)
            {
                m_dragMode = DragMode::Scale;
                m_activeHandle = handle;
                return;
            }
            if (m_active->shapeContains(local) || pressedInGroup())
            {
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
    if (auto *shape = qgraphicsitem_cast<ShapeItem *>(hit))
    {
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

    if (isAimingShot())
    {
        aimShot(event->scenePos());
        event->accept();
        return;
    }

    if (m_draggedJoint)
    {
        m_draggedJoint->setAnchorScenePos(m_draggedJointEnd == 0 ? Joint::End::A : Joint::End::B,
                                          snapScenePoint(event->scenePos()));
        event->accept();
        return;
    }

    if (m_polygonDrawing)
    {
        m_polygonCursorScenePos = event->scenePos();
        if (!m_polygonScenePoints.isEmpty())
        {
            update();
        }
        return;
    }

    if (m_dragMode == DragMode::PanField)
    {
        const QPoint delta = event->screenPos() - m_panLastScreenPos;
        m_panLastScreenPos = event->screenPos();
        for (QGraphicsView *view : views())
        {
            view->horizontalScrollBar()->setValue(view->horizontalScrollBar()->value() - delta.x());
            view->verticalScrollBar()->setValue(view->verticalScrollBar()->value() - delta.y());
        }
        return;
    }

    // Ahead of the m_active guard below: a Physics-mode drag has no active
    // item, so it would never reach the switch.
    if (m_dragMode == DragMode::MoveBody)
    {
        const QPointF scenePos = event->scenePos();

        const QPointF delta = scenePos - m_lastScenePos;
        m_moveDragVirtualPos += delta;

        QPointF shift = m_moveDragVirtualPos;
        if (m_snapToGrid)
        {
            if (!m_bodyDragShapes.isEmpty())
            {
                ShapeItem *lead = m_bodyDragShapes.first();
                const QPointF localReference = (m_snapPoint == SnapPoint::Position) ? lead->rect().topLeft()
                                                                                    : lead->origin();
                const QPointF referenceScene = shift + localReference;
                shift += snapScenePoint(referenceScene) - referenceScene;
            }
            else if (m_draggedExplosion || m_draggedRay)
            {
                // A blast and a rangefinder are points: there is no corner or
                // pivot to snap by, because the thing itself is the point.
                shift = snapScenePoint(shift);
            }
        }

        // Every piece moves by what the leading one moved, so their relative
        // layout -- and the joints anchored to them -- survives the drag.
        const QPointF applied = shift - m_bodyDragStartPositions.value(0);
        if (m_draggedExplosion)
        {
            m_draggedExplosion->setPos(m_bodyDragStartPositions.value(0) + applied);
        }
        if (m_draggedRay)
        {
            m_draggedRay->setPos(m_bodyDragStartPositions.value(0) + applied);
        }
        for (int i = 0; i < m_bodyDragShapes.size(); ++i)
        {
            m_bodyDragShapes[i]->setPos(m_bodyDragStartPositions[i] + applied);
        }

        m_lastScenePos = scenePos;
        update();
        return;
    }

    if (m_dragMode == DragMode::None || !m_active)
    {
        QGraphicsScene::mouseMoveEvent(event);
        return;
    }

    const QPointF scenePos = event->scenePos();

    switch (m_dragMode)
    {
    case DragMode::Move:
    {
        // Accumulate the true, unsnapped drag position from raw mouse deltas.
        const QPointF delta = scenePos - m_lastScenePos;
        m_moveDragVirtualPos += delta;

        QPointF displayPos = m_moveDragVirtualPos;
        if (m_snapToGrid)
        {
            const QPointF localReference = (m_snapPoint == SnapPoint::Position) ? m_active->rect().topLeft()
                                                                                : m_active->origin();
            const QPointF referenceScene = displayPos + localReference;
            displayPos += snapScenePoint(referenceScene) - referenceScene;
        }
        const QPointF previous = m_active->pos();
        m_active->setPos(displayPos);
        // The rest of the selection moves by exactly what the lead moved, so
        // the spacing within the group survives the drag.
        const QPointF applied = m_active->pos() - m_groupLeadStart;
        for (int i = 0; i < m_editSelection.size() && i < m_groupStartPositions.size(); ++i)
        {
            m_editSelection[i]->setPos(m_groupStartPositions.at(i) + applied);
        }
        // The pivot travels with the shapes it belongs to, wherever it was put.
        if (!m_editSelection.isEmpty())
        {
            m_groupOrigin += m_active->pos() - previous;
            update();
        }
        m_lastScenePos = scenePos;
        break;
    }
    case DragMode::Scale:
    {
        const QPointF local = m_active->mapFromScene(snapScenePoint(scenePos));
        m_active->resizeByHandle(m_activeHandle, local);
        break;
    }
    case DragMode::Rotate:
    {
        const qreal angle = angleAt(m_dragOriginScene, scenePos);
        const qreal turned = angle - m_rotateStartAngle;

        if (m_editSelection.isEmpty())
        {
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

        const auto turn = [&](ShapeItem *shape, const QPointF &startPos, qreal startRotation) {
            // A shape turns about its own origin, which leaves that origin at
            // pos + origin whatever the rotation -- so the orbit is applied
            // there and the position follows from it.
            const QPointF originScene = startPos + shape->origin();
            shape->setRotation(startRotation + turned);
            shape->setPos(orbit.map(originScene) - shape->origin());
        };

        turn(m_active, m_groupLeadStart, m_groupLeadStartRotation);
        for (int i = 0; i < m_editSelection.size() && i < m_groupStartPositions.size(); ++i)
        {
            turn(m_editSelection.at(i), m_groupStartPositions.at(i), m_groupStartRotations.at(i));
        }
        break;
    }
    case DragMode::GroupScale:
    {
        const QPointF from = m_groupScaleStart - m_groupScaleAnchor;
        const qreal span = QPointF::dotProduct(from, from);
        if (span > 0.0)
        {
            // The cursor is read along the box's diagonal, so the group keeps
            // its proportions however the mouse wanders off the line.
            const QPointF to = snapScenePoint(scenePos) - m_groupScaleAnchor;
            applyGroupScale(qMax(0.05, QPointF::dotProduct(to, from) / span));
        }
        break;
    }
    case DragMode::GroupOrigin:
    {
        setEditSelectionOrigin(snapScenePoint(scenePos));
        break;
    }
    case DragMode::Origin:
    {
        const QPointF local = m_active->mapFromScene(snapScenePoint(scenePos));
        m_active->setOrigin(local);
        break;
    }
    case DragMode::EditNode:
    {
        const QPointF grabbedStart = m_editDragNodeStart.value(m_editNodeIndex);
        const QPointF grabbedNew = m_active->mapFromScene(snapScenePoint(scenePos));
        const QPointF delta = grabbedNew - grabbedStart;
        for (auto it = m_editDragNodeStart.constBegin(); it != m_editDragNodeStart.constEnd(); ++it)
        {
            m_active->moveNode(it.key(), it.value() + delta);
        }
        break;
    }
    case DragMode::None:
        break;
    }
}

void CanvasScene::mouseReleaseEvent(QGraphicsSceneMouseEvent *event)
{
    m_snapSuspended = false;

    if (isAimingShot())
    {
        if (event->button() == Qt::LeftButton)
        {
            releaseShot();
        }
        event->accept();
        return;
    }

    if (m_draggedJoint)
    {
        notifyEdit(tr("Move %1 anchor").arg(m_draggedJoint->name()));
        m_draggedJoint = nullptr;
        event->accept();
        return;
    }

    if (m_dragMode != DragMode::None)
    {
        const DragMode finished = m_dragMode;
        m_dragMode = DragMode::None;

        // Physics-mode drags have no m_active to hang the undo label on, and
        // the joints anchored to what moved need their cached ends refreshed.
        if (finished == DragMode::MoveBody)
        {
            const QPointF now = m_draggedRay         ? m_draggedRay->pos()
                                : m_draggedExplosion ? m_draggedExplosion->pos()
                                                     : (m_bodyDragShapes.isEmpty() ? m_bodyDragStartPositions.value(0)
                                                                                   : m_bodyDragShapes.first()->pos());
            const bool moved = now != m_bodyDragStartPositions.value(0);
            m_draggedExplosion = nullptr;
            m_draggedRay = nullptr;
            m_bodyDragShapes.clear();
            m_bodyDragStartPositions.clear();
            if (moved)
            {
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
            && QLineF(m_pressScenePos, event->scenePos()).length() < 3.0)
        {
            ShapeItem *only = m_groupClickCandidate;
            m_groupClickCandidate = nullptr;
            clearEditSelection();
            activate(only);
            update();
            return;
        }
        m_groupClickCandidate = nullptr;

        if (m_active)
        {
            m_active->update();
        }
        m_activeHandle = HandleId::None;
        m_editNodeIndex = -1;
        m_editDragNodeStart.clear();
        if (m_active)
        {
            switch (finished)
            {
            case DragMode::Move:
                notifyEdit(m_editSelection.isEmpty() ? tr("Move %1").arg(m_active->name())
                                                     : tr("Move %n shapes", nullptr, int(m_editSelection.size()) + 1));
                break;
            case DragMode::Rotate:
                notifyEdit(m_editSelection.isEmpty()
                                   ? tr("Rotate %1").arg(m_active->name())
                                   : tr("Rotate %n shapes", nullptr, int(m_editSelection.size()) + 1));
                break;
            case DragMode::Origin:
                notifyEdit(tr("Move %1 origin").arg(m_active->name()));
                break;
            case DragMode::Scale:
                notifyEdit(tr("Resize %1").arg(m_active->name()));
                break;
            case DragMode::EditNode:
                notifyEdit(tr("Edit %1").arg(m_active->name()));
                break;
            case DragMode::GroupScale:
                notifyEdit(tr("Resize %n shapes", nullptr, int(m_editSelection.size()) + 1));
                break;
            case DragMode::GroupOrigin:
            case DragMode::PanField:
            case DragMode::None:
                break;
            }
        }
        return;
    }
    QGraphicsScene::mouseReleaseEvent(event);
}

void CanvasScene::mouseDoubleClickEvent(QGraphicsSceneMouseEvent *event)
{
    // Deliberately never forwarded to QGraphicsScene::mouseDoubleClickEvent().
    if (m_editorMode != EditorMode::Edit)
    {
        if (event->button() == Qt::LeftButton && selectionAllowed())
        {
            // The shape already selected, even beneath another -- one reached
            // with Ctrl+click -- and otherwise the one on top.
            const QVector<ShapeItem *> under = shapesAt(event->scenePos());
            ShapeItem *current = m_physicsSelection.size() == 1 ? m_physicsSelection.first() : nullptr;
            ShapeItem *hit = current && under.contains(current) ? current : under.value(0, nullptr);
            if (hit && hit->body())
            {
                selectJoint(nullptr);
                clearPhysicsSelection();
                selectForPhysics(hit, true);
            }
            else if (hit)
            {
                if (!isSelectedForPhysics(hit))
                {
                    selectForPhysics(hit);
                }
                emit createBodyRequested(event->modifiers().testFlag(Qt::ControlModifier));
            }
        }
        event->accept();
        return;
    }

    if (event->button() == Qt::LeftButton && m_active && geometryEditingAllowed())
    {
        const QPointF local = m_active->mapFromScene(event->scenePos());
        if (m_active->shapeContains(local))
        {
            if (m_active->supportsNodeEditing())
            {
                switch (m_active->mode())
                {
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
            }
            else if (m_active->mode() == ShapeMode::Selected)
            {
                switchActiveToRotating();
            }
            else if (m_active->mode() == ShapeMode::Rotating)
            {
                switchActiveToSelected();
            }
        }
    }
    event->accept();
}

void CanvasScene::wheelEvent(QGraphicsSceneWheelEvent *event)
{
    if (event->modifiers() & Qt::ShiftModifier)
    {
        const qreal notches = event->delta() / 120.0;
        setCurrentScale(m_currentScale + notches * m_scaleStep);
        event->accept();
        return;
    }
    QGraphicsScene::wheelEvent(event);
}
