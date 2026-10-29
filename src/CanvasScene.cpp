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
    item->setPos(scenePos);
    addItem(item);
    item->setName(uniqueName(item->name(), item));
    announceChange(Change::Shapes);
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

    return replaceShape(shape, json);
}

// One shape in another's place: its name, its body, its physics and whatever
// was holding it all carry over, because the document says so -- the new one
// is built from the old one's own JSON with the geometry changed.

// Taking one shape out, with everything that was holding on to it let go
// first. A raw delete leaves the scene pointing at freed memory -- the active
// item above all, which is read on the next paint and the next click -- so
// nothing deletes a shape itself.
void CanvasScene::destroyShape(ShapeItem *shape)
{
    if (!shape || shape->scene() != this)
    {
        return;
    }

    PhysicsBody *body = shape->body();
    if (body)
    {
        body->removeShape(shape);
    }

    const bool wasActive = m_active == shape;
    if (wasActive)
    {
        m_active = nullptr;
        m_selectedNodes.clear();
    }
    const bool wasCoSelected = m_editSelection.removeOne(shape);
    const bool wasPicked = m_physicsSelection.removeOne(shape);
    m_bodyDragShapes.removeAll(shape);
    if (m_groupClickCandidate == shape)
    {
        m_groupClickCandidate = nullptr;
    }
    m_dragMode = DragMode::None;

    removeItem(shape);
    delete shape;

    if (wasActive)
    {
        emit activeItemChanged(nullptr);
    }
    if (wasCoSelected || wasActive)
    {
        refreshGroupOrigin();
        emit editSelectionChanged();
    }
    if (wasPicked)
    {
        emit physicsSelectionChanged();
    }
    announceChange(Change::Shapes);
    if (body)
    {
        announceChange(Change::Bodies);
    }
    pruneWatches();
}

ShapeItem *CanvasScene::replaceShape(ShapeItem *shape, const QJsonObject &json)
{
    if (!shape || shape->scene() != this)
    {
        return nullptr;
    }

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

    announceChange(Change::Shapes);
    if (body)
    {
        announceChange(Change::Bodies);
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

// A circle is not a shape an engine can round off into segments on its own --
// how many is the scene's business, not the engine's -- so the editor picks
// the count and hands over the polygon it makes.

// The corners the outline of a circle or a rectangle is made of, in the
// shape's own coordinates and in the order they run. A circle has as many as
// it is asked for; a rectangle keeps its four and splits each side that many
// ways, so a corner never falls in the middle of a segment.
QVector<QPointF> CanvasScene::outlineCorners(ShapeItem *shape, int segments,
                                             qreal startAngle) const
{
    QVector<QPointF> corners;
    if (!shape || segments < 1)
    {
        return corners;
    }

    const QRectF own = shape->rect();
    if (shape->typeName() == QLatin1String("circle"))
    {
        if (segments < 3)
        {
            return corners;
        }
        const QPointF middle = own.center();
        const qreal radius = qMin(own.width(), own.height()) / 2.0;
        const qreal from = qDegreesToRadians(startAngle);
        for (int i = 0; i < segments; ++i)
        {
            const qreal angle = from + i * 2.0 * M_PI / segments;
            corners.append(middle + QPointF(radius * std::cos(angle), radius * std::sin(angle)));
        }
        return corners;
    }

    const QPointF side[4] { own.topLeft(), own.topRight(), own.bottomRight(), own.bottomLeft() };
    for (int i = 0; i < 4; ++i)
    {
        const QPointF from = side[i];
        const QPointF to = side[(i + 1) % 4];
        for (int step = 0; step < segments; ++step)
        {
            const qreal t = qreal(step) / segments;
            corners.append(from + (to - from) * t);
        }
    }
    return corners;
}

// One shape per side of that outline, each a line of its own, in no body and
// free to be jointed to its neighbours. The shape they came from goes.
QVector<ShapeItem *> CanvasScene::convertToSegments(ShapeItem *shape, int segments,
                                                    qreal startAngle)
{
    QVector<ShapeItem *> made;
    if (!shape || shape->scene() != this)
    {
        return made;
    }
    const QVector<QPointF> corners = outlineCorners(shape, segments, startAngle);
    if (corners.size() < 2)
    {
        return made;
    }

    // Built from the shape's own JSON, so each segment keeps its friction and
    // the rest of what the engine was told about it.
    const QJsonObject source = SceneSerializer::shapeToJson(shape);
    const QString stem = shape->name();
    const qreal z = shape->zValue();

    for (int i = 0; i < corners.size(); ++i)
    {
        const QPointF a = shape->mapToScene(corners.at(i));
        const QPointF b = shape->mapToScene(corners.at((i + 1) % corners.size()));
        const QPointF middle = (a + b) / 2.0;

        QJsonObject json = source;
        json.insert(QStringLiteral("type"), QStringLiteral("polygon"));
        json.insert(QStringLiteral("closed"), false);
        json.insert(QStringLiteral("outline"), true);
        json.insert(QStringLiteral("cornerRadius"), 0.0);
        // Laid out about its own middle with no turn of its own, so the two
        // ends say where it is and nothing else has to agree with them.
        json.insert(QStringLiteral("rotation"), 0.0);
        json.insert(QStringLiteral("origin"), QJsonObject { { "x", 0.0 }, { "y", 0.0 } });
        json.insert(QStringLiteral("pos"),
                    QJsonObject { { "x", middle.x() }, { "y", middle.y() } });
        json.insert(QStringLiteral("points"),
                    QJsonArray { QJsonObject { { "x", a.x() - middle.x() },
                                               { "y", a.y() - middle.y() } },
                                 QJsonObject { { "x", b.x() - middle.x() },
                                               { "y", b.y() - middle.y() } } });

        ShapeItem *segment = SceneSerializer::shapeFromJson(json);
        if (!segment)
        {
            continue;
        }
        segment->setZValue(z);
        addItem(segment);
        segment->setName(Naming::nextName(stem, takenNames(segment)));
        made.append(segment);
    }

    if (!made.isEmpty())
    {
        announceChange(Change::Shapes);
    }
    return made;
}

ShapeItem *CanvasScene::convertCircleToChain(ShapeItem *shape, int segments, qreal startAngle)
{
    if (!shape || shape->scene() != this || segments < 3)
    {
        return nullptr;
    }

    const QRectF own = shape->rect();
    const QPointF middle = own.center();
    const qreal radius = qMin(own.width(), own.height()) / 2.0;

    // Where the first corner sits. Turning the corners is not the same as
    // turning the shape: the shape's own rotation is what its body is built
    // with, and a chain wanted flat side down should not have to carry one.
    const qreal from = qDegreesToRadians(startAngle);

    QJsonArray points;
    for (int i = 0; i < segments; ++i)
    {
        const qreal angle = from + i * 2.0 * M_PI / segments;
        points.append(QJsonObject { { "x", middle.x() + radius * std::cos(angle) },
                                    { "y", middle.y() + radius * std::sin(angle) } });
    }

    QJsonObject json = SceneSerializer::shapeToJson(shape);
    json.insert(QStringLiteral("type"), QStringLiteral("polygon"));
    json.insert(QStringLiteral("closed"), true);
    json.insert(QStringLiteral("points"), points);
    json.insert(QStringLiteral("cornerRadius"), 0.0);
    // A chain is hollow, and a closed convex outline within the vertex limit
    // would otherwise be built as a solid shape -- so eight segments or fewer
    // came out a filled polygon rather than a ring with an inside.
    json.insert(QStringLiteral("outline"), true);
    return replaceShape(shape, json);
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
    // Let go of it before it is deleted, the way a ray is let go of below:
    // whoever hears explosionsChanged asks the scene what is selected, and a
    // pointer to a deleted item answers that question on Windows and crashes
    // on Linux.
    if (m_selectedExplosion == explosion)
    {
        m_selectedExplosion = nullptr;
        emit selectedExplosionChanged(nullptr);
    }
    if (m_draggedExplosion == explosion)
    {
        m_draggedExplosion = nullptr;
    }
    removeItem(explosion);
    delete explosion;
    emit explosionsChanged();
    pruneWatches();
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
    pruneWatches();
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

void CanvasScene::letOthersGo(Picked keep)
{
    if (keep != Picked::Shapes)
    {
        clearPhysicsSelection();
    }
    if (keep != Picked::Joint)
    {
        selectJoint(nullptr);
    }
    if (keep != Picked::Ray)
    {
        selectRay(nullptr);
    }
    if (keep != Picked::Explosion)
    {
        selectExplosion(nullptr);
    }
}

void CanvasScene::selectRay(RayItem *ray)
{
    if (m_selectedRay == ray)
    {
        return;
    }
    if (ray)
    {
        letOthersGo(Picked::Ray);
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
        letOthersGo(Picked::Explosion);
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

bool CanvasScene::revealObjectNamed(const QString &name)
{
    if (name.isEmpty())
    {
        return false;
    }

    // A rule names one namespace, so the first thing that answers is the one
    // meant -- see takenNames(), which is what keeps them apart.
    for (Joint *joint : std::as_const(m_joints))
    {
        if (joint->name() != name)
        {
            continue;
        }
        clearPhysicsSelection();
        selectExplosion(nullptr);
        selectRay(nullptr);
        selectJoint(joint);
        return true;
    }
    if (RayItem *ray = rayNamed(name))
    {
        clearPhysicsSelection();
        selectJoint(nullptr);
        selectExplosion(nullptr);
        selectRay(ray);
        return true;
    }
    if (ExplosionItem *explosion = explosionNamed(name))
    {
        clearPhysicsSelection();
        selectJoint(nullptr);
        selectRay(nullptr);
        selectExplosion(explosion);
        return true;
    }
    for (ShapeItem *shape : shapes())
    {
        if (shape->name() == name)
        {
            selectJoint(nullptr);
            selectRay(nullptr);
            selectExplosion(nullptr);
            clearPhysicsSelection();
            selectForPhysics(shape, true);
            return true;
        }
    }
    for (PhysicsBody *body : m_bodies)
    {
        if (body->name() == name && !body->shapes().isEmpty())
        {
            selectJoint(nullptr);
            selectRay(nullptr);
            selectExplosion(nullptr);
            clearPhysicsSelection();
            selectForPhysics(body->shapes().first(), true);
            return true;
        }
    }
    return false;
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
    item->setPos(scenePos);
    addItem(item);
    item->setName(uniqueName(item->name(), item));
    announceChange(Change::Shapes);
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

    // Picking a body is picking something else: the joint, the ray and the
    // explosion stop being what the panel and the Remove button are pointed at.
    letOthersGo(Picked::Shapes);

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

// Everything the swept box touches, by its outline rather than by the box it
// covers: a circle a corner of the band clips is caught, one it only passes
// near is not.
void CanvasScene::applyRubberBand(const QRectF &band, bool additive)
{
    QVector<ShapeItem *> caught;
    const QList<QGraphicsItem *> under = items(band, Qt::IntersectsItemShape, Qt::AscendingOrder);
    for (QGraphicsItem *candidate : under)
    {
        if (auto *shape = qgraphicsitem_cast<ShapeItem *>(candidate))
        {
            caught.append(shape);
        }
    }
    if (caught.isEmpty())
    {
        return;
    }

    if (m_editorMode != EditorMode::Edit)
    {
        // A body comes whole: one of its shapes swept up brings the rest, as
        // a Shift+click on it does.
        QVector<ShapeItem *> wanted = additive ? m_physicsSelection : QVector<ShapeItem *>();
        for (ShapeItem *shape : std::as_const(caught))
        {
            const QVector<ShapeItem *> group =
                shape->body() ? shape->body()->shapes() : QVector<ShapeItem *> { shape };
            for (ShapeItem *member : group)
            {
                if (!wanted.contains(member))
                {
                    wanted.append(member);
                }
            }
        }
        if (wanted == m_physicsSelection)
        {
            return;
        }
        selectRay(nullptr);
        selectExplosion(nullptr);
        m_physicsSelection = wanted;
        update();
        emit physicsSelectionChanged();
        return;
    }

    for (ShapeItem *shape : std::as_const(caught))
    {
        // addToEditSelection() toggles, which is what a Shift+click wants and
        // not what a box does: a shape the box covers ends up picked whether
        // it was before or not.
        if (shape == m_active || m_editSelection.contains(shape))
        {
            continue;
        }
        addToEditSelection(shape);
    }
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
        announceChange(Change::Bodies);
    }
    return body;
}

void CanvasScene::clearContents()
{
    deactivate();
    clearPhysicsSelection();
    clearWatches();

    m_rules.clear();
    announceChange(Change::Rules);

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
    announceChange(Change::Joints);

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
    announceChange(Change::Shapes);
    announceChange(Change::Bodies);
    emit physicsSelectionChanged();
}

void CanvasScene::announceChange(Change what)
{
    if (m_bulkDepth > 0) {
        m_deferredChanges.insert(int(what));
        return;
    }
    switch (what) {
    case Change::Shapes:    emit shapesChanged(); break;
    case Change::Bodies:    emit bodiesChanged(); break;
    case Change::Joints:    emit jointsChanged(); break;
    case Change::Rules:     emit rulesChanged(); break;
    case Change::Variables: emit variablesChanged(); break;
    case Change::Controls:  emit controlsChanged(); break;
    }
}

void CanvasScene::endBulkChange()
{
    if (--m_bulkDepth > 0)
        return;

    // Taken first: what a listener does on its way through can ask for another
    // announcement, and this one is already being delivered.
    const QSet<int> deferred = m_deferredChanges;
    m_deferredChanges.clear();
    // In a fixed order rather than the set's, so a panel listening to two of
    // them sees them the same way every time.
    for (Change what : { Change::Shapes, Change::Bodies, Change::Joints,
                         Change::Rules, Change::Variables, Change::Controls }) {
        if (deferred.contains(int(what)))
            announceChange(what);
    }
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
    return createBodyFromShapes(m_physicsSelection);
}

PhysicsBody *CanvasScene::createBodyFromShapes(const QVector<ShapeItem *> &shapes)
{
    if (shapes.isEmpty())
    {
        return nullptr;
    }

    PhysicsBody *body = createEmptyBody();

    QVector<PhysicsBody *> vacated;
    for (ShapeItem *shape : shapes)
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
    announceChange(Change::Bodies);
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
    announceChange(Change::Joints);
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
    pruneWatches();
    update();
    announceChange(Change::Joints);
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
    announceChange(Change::Joints); // the icon beside a joint's name -- see setBodyColor
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
    announceChange(Change::Joints);
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
        m_jointSprings.clear();
        m_jointLimits.clear();
        if (auto engine = physics::EngineRegistry::create(engineName))
        {
            for (const physics::JointType &type : engine->jointTypes())
            {
                m_jointVisuals.insert(type.id, type.visual);

                SpringKeys keys;
                for (const physics::JointParam &param : type.params)
                {
                    if (param.role == physics::PropertyRole::SpringEnabled)
                    {
                        keys.enabledKey = param.key;
                        keys.enabledDefault = param.defaultValue.toBool();
                    }
                    else if (param.role == physics::PropertyRole::SpringStiffness
                             && keys.stiffnessKey.isEmpty())
                    {
                        keys.stiffnessKey = param.key;
                        keys.stiffnessDefault = param.defaultValue.toDouble();
                    }
                    else if (param.role == physics::PropertyRole::SpringRestLength)
                    {
                        keys.restKey = param.key;
                        keys.restDefault = param.defaultValue.toDouble();
                    }
                }
                m_jointSprings.insert(type.id, keys);

                LimitKeys stops;
                for (const physics::JointParam &param : type.params)
                {
                    if (param.role == physics::PropertyRole::LimitEnabled)
                    {
                        stops.enabledKey = param.key;
                        stops.enabledDefault = param.defaultValue.toBool();
                    }
                    else if (param.role == physics::PropertyRole::LowerLimit)
                    {
                        stops.lowerKey = param.key;
                        stops.lowerDefault = param.defaultValue.toDouble();
                    }
                    else if (param.role == physics::PropertyRole::UpperLimit)
                    {
                        stops.upperKey = param.key;
                        stops.upperDefault = param.defaultValue.toDouble();
                    }
                }
                m_jointLimits.insert(type.id, stops);
            }
        }
    }
    return m_jointVisuals.value(typeId, physics::JointVisual::Pivot);
}

CanvasScene::JointLimits CanvasScene::jointLimits(const Joint *joint) const
{
    JointLimits stops;
    if (!joint)
    {
        return stops;
    }
    jointVisual(joint->typeId()); // fills the cache for this engine
    const LimitKeys keys = m_jointLimits.value(joint->typeId());
    if (keys.lowerKey.isEmpty() || keys.upperKey.isEmpty())
    {
        return stops;
    }
    const QVariantMap &params = joint->params();

    // A switch where the engine has one. Where there is none the pair of
    // numbers is the whole of it -- some engines make a limited joint its own
    // type rather than a setting -- so it is in force wherever it exists.
    stops.enforced = keys.enabledKey.isEmpty()
                     || params.value(keys.enabledKey, keys.enabledDefault).toBool();
    stops.lower = params.value(keys.lowerKey, keys.lowerDefault).toDouble();
    stops.upper = params.value(keys.upperKey, keys.upperDefault).toDouble();
    return stops;
}

bool CanvasScene::isSpringJoint(const Joint *joint) const
{
    if (!joint)
    {
        return false;
    }
    jointVisual(joint->typeId()); // fills the cache for this engine
    const SpringKeys keys = m_jointSprings.value(joint->typeId());
    const QVariantMap &params = joint->params();

    // A switch where the engine has one; otherwise a stiffness, where above
    // zero is the only thing that can mean "there is a spring here".
    if (!keys.enabledKey.isEmpty())
    {
        return params.value(keys.enabledKey, keys.enabledDefault).toBool();
    }
    if (!keys.stiffnessKey.isEmpty())
    {
        return params.value(keys.stiffnessKey, keys.stiffnessDefault).toDouble() > 0.0;
    }
    return false;
}

// How many turns this spring has: its rest length over the pitch a turn is
// drawn at. Worked out only while the scene stands still, because that is when
// the joint is at rest; through a run the answer is held, so the turns spread
// and pack instead of being recounted as the spring moves. An engine that lets
// a rest length of zero mean "wherever the anchors start" leaves the span to
// say, which is the same thing while nothing is running.
int CanvasScene::springTurns(const Joint *joint, qreal spanLength) const
{
    const QString name = joint->name();
    if (simulationRunning())
    {
        const int held = m_springTurns.value(name, 0);
        if (held > 0)
        {
            return held;
        }
    }

    const SpringKeys keys = m_jointSprings.value(joint->typeId());
    qreal rest = 0.0;
    if (!keys.restKey.isEmpty())
    {
        rest = joint->params().value(keys.restKey, keys.restDefault).toDouble();
    }
    if (rest <= 0.0)
    {
        rest = spanLength;
    }

    const int turns = qBound(2, qRound(rest / qMax(m_springPitch, 1.0)), 60);
    m_springTurns.insert(name, turns);
    return turns;
}

// The turns, between two straight leads. Their count is fixed by the rest
// length; the spacing comes from how far apart the anchors actually are, so
// the coil flattens towards a line as it is pulled out and packs towards
// touching strokes as it is squashed -- which is what a spring does, and needs
// no travel limits to work.
QPainterPath CanvasScene::springPath(const Joint *joint, const QPointF &a, const QPointF &b) const
{
    const QLineF span(a, b);
    const qreal length = span.length();
    QPainterPath path;
    path.moveTo(a);
    if (length < 0.01)
    {
        return path;
    }

    const QPointF along = (b - a) / length;
    const QPointF across(-along.y(), along.x());
    // Straight at each end so the turns do not start inside the anchor rings.
    const qreal lead = qMin(m_jointAnchorRadius * 1.5, length * 0.3);
    const qreal body = length - lead * 2.0;
    const int steps = springTurns(joint, length) * 2;
    if (body <= 0.0 || steps < 2)
    {
        path.lineTo(b);
        return path;
    }

    const QPointF start = a + along * lead;
    const qreal pitch = body / steps;
    const qreal amplitude = m_springWidth / 2.0;
    path.lineTo(start);
    for (int i = 1; i < steps; ++i)
    {
        const qreal side = (i % 2) ? 1.0 : -1.0;
        path.lineTo(start + along * (pitch * i) + across * (side * amplitude));
    }
    path.lineTo(start + along * body);
    path.lineTo(b);
    return path;
}

void CanvasScene::setSpringsAsCoils(bool asCoils)
{
    if (m_springsAsCoils == asCoils)
    {
        return;
    }
    m_springsAsCoils = asCoils;
    update();
}

void CanvasScene::setSpringPitch(qreal pitch)
{
    const qreal wanted = qBound(1.0, pitch, 500.0);
    if (qFuzzyCompare(m_springPitch, wanted))
    {
        return;
    }
    m_springPitch = wanted;
    m_springTurns.clear();
    update();
}

void CanvasScene::setSpringWidth(qreal width)
{
    const qreal wanted = qBound(0.0, width, 200.0);
    if (qFuzzyCompare(m_springWidth, wanted))
    {
        return;
    }
    m_springWidth = wanted;
    update();
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
    announceChange(Change::Joints);
}

void CanvasScene::selectJoint(Joint *joint)
{
    if (m_selectedJoint == joint)
    {
        return;
    }
    if (joint)
    {
        letOthersGo(Picked::Joint);
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

    // Something else still answers to the old name, so this was not a rename
    // of the thing the rules mean. A shape arrives carrying the default name
    // of its kind and is given a free one the moment it joins the scene, which
    // reaches here as "circle_1 became circle_2" -- and following that rewrote
    // every rule that named the circle_1 already on the canvas. Adding a shape
    // changed what other rules watched, which is the sort of thing you only
    // notice once the scene has stopped working.
    if (takenNames().contains(previous))
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
        announceChange(Change::Rules);
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
    announceChange(Change::Rules);
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

PhysicsBody *CanvasScene::focusedBody() const
{
    for (PhysicsBody *body : m_bodies)
    {
        if (body->isFocused())
        {
            return body;
        }
    }
    return nullptr;
}

void CanvasScene::setFocusedBody(PhysicsBody *body)
{
    bool moved = false;
    for (PhysicsBody *one : m_bodies)
    {
        const bool wanted = one == body;
        if (one->isFocused() != wanted)
        {
            one->setFocused(wanted);
            moved = true;
        }
    }
    if (moved)
    {
        emit focusedBodyChanged();
    }
}

void CanvasScene::destroyBody(PhysicsBody *body, bool announce)
{
    if (!body || !m_bodies.removeOne(body))
    {
        return;
    }
    // Nothing to follow once it is gone, and the view has to be told rather
    // than left pointing at a body that no longer exists.
    const bool wasFollowed = body->isFocused();

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
    if (wasFollowed)
    {
        emit focusedBodyChanged();
    }
    if (announce)
    {
        announceChange(Change::Bodies);
        emit physicsSelectionChanged();
    }
    pruneWatches();
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
    // The rule cards carry a body's colour in the icon beside its name, and
    // they keep the list they built. A colour is part of what that list shows.
    announceChange(Change::Bodies);
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
    // What the log shows is saved with the scene, so putting something on it is
    // a change like any other -- without this the Save button stayed grey and
    // closing offered to throw the change away without saying there was one.
    // The loader uses setWatches() and so does not come through here.
    notifyEdit(tr("Add %1 to the log").arg(watch.label.isEmpty() ? watch.propertyKey
                                                                 : watch.label));
}

void CanvasScene::pruneWatches()
{
    if (m_watches.isEmpty())
    {
        return;
    }

    // What each joint type still answers to, asked once rather than per watch.
    QHash<QString, QSet<QString>> keysByType;
    if (auto engine = physics::EngineRegistry::create(describingEngineName()))
    {
        for (const physics::JointType &type : engine->jointTypes())
        {
            QSet<QString> keys;
            for (const physics::JointParam &param : type.params)
            {
                keys.insert(param.key);
            }
            for (const physics::JointParam &param : engine->jointReadables(type.id))
            {
                keys.insert(param.key);
            }
            keysByType.insert(type.id, keys);
        }
    }

    const auto stillThere = [&](const Watch &watch) {
        // The world and the scene's own variables are not objects and outlive
        // anything on the canvas.
        if (watch.objectName == Rule::world() || watch.objectName == Rule::variables())
        {
            return true;
        }
        for (Joint *joint : std::as_const(m_joints))
        {
            if (joint->name() != watch.objectName)
            {
                continue;
            }
            const auto keys = keysByType.constFind(joint->typeId());
            // An engine that says nothing about the type is no reason to throw
            // the watch away; only a type that plainly lacks the key is.
            return keys == keysByType.constEnd() || keys->contains(watch.propertyKey);
        }
        for (PhysicsBody *body : std::as_const(m_bodies))
        {
            if (body->name() == watch.objectName)
            {
                return true;
            }
        }
        for (ShapeItem *shape : shapes())
        {
            if (shape->name() == watch.objectName)
            {
                return true;
            }
        }
        for (RayItem *ray : std::as_const(m_rays))
        {
            if (ray->name() == watch.objectName)
            {
                return true;
            }
        }
        for (ExplosionItem *explosion : std::as_const(m_explosions))
        {
            if (explosion->name() == watch.objectName)
            {
                return true;
            }
        }
        return false;
    };

    QVector<Watch> kept;
    kept.reserve(m_watches.size());
    for (const Watch &watch : std::as_const(m_watches))
    {
        if (stillThere(watch))
        {
            kept.append(watch);
        }
    }
    if (kept.size() == m_watches.size())
    {
        return;
    }
    m_watches = kept;
    emit watchesChanged();
}

void CanvasScene::setWatchPlotted(const QString &objectName, const QString &propertyKey,
                                  bool plotted)
{
    for (Watch &watch : m_watches)
    {
        if (watch.objectName == objectName && watch.propertyKey == propertyKey
            && watch.plotted != plotted)
        {
            watch.plotted = plotted;
            emit watchesChanged();
            notifyEdit(tr("Change log"));
            return;
        }
    }
}

bool CanvasScene::isPlotted(const QString &objectName, const QString &propertyKey) const
{
    for (const Watch &watch : m_watches)
    {
        if (watch.objectName == objectName && watch.propertyKey == propertyKey)
            return watch.plotted;
    }
    return false;
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
        notifyEdit(tr("Take %1 off the log").arg(propertyKey));
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
    announceChange(Change::Variables);
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
        announceChange(Change::Rules);
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
        // An explosion and a ray are named like everything else a rule can
        // name, and what they carry is worth watching: how hard it goes off,
        // how far it reaches, what the ray last hit.
        for (ExplosionItem *explosion : m_explosions)
        {
            if (explosion->name() != objectName)
            {
                continue;
            }
            const auto it = explosion->params().constFind(what);
            if (it != explosion->params().constEnd())
            {
                return *it;
            }
            // An explosion stores only what differs from the default, so an
            // untouched setting is not in the map at all. The default is the
            // engine's to state, the same way the property table gets it.
            if (auto engine = physics::EngineRegistry::create(simulationEngineName()))
            {
                for (const physics::ActionType &action : engine->bodyActions())
                {
                    if (action.id != QLatin1String("explode"))
                    {
                        continue;
                    }
                    for (const physics::JointParam &param : action.params)
                    {
                        if (param.key == what)
                        {
                            return param.defaultValue;
                        }
                    }
                }
            }
        }
        for (RayItem *ray : m_rays)
        {
            if (ray->name() != objectName)
            {
                continue;
            }
            if (what == QLatin1String("angle"))
            {
                return ray->angleDegrees();
            }
            if (what == QLatin1String("length"))
            {
                return ray->length();
            }
        }

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

void CanvasScene::setRandomSeed(quint32 seed)
{
    if (m_randomSeed == seed)
    {
        return;
    }
    m_randomSeed = seed;
    emit fieldPropertyChanged();
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

void CanvasScene::setDefaultShapeStyles(const QHash<QString, ShapeStyle> &styles)
{
    m_defaultShapeStyles = styles;
}

ShapeStyle CanvasScene::defaultShapeStyle(const QString &typeName) const
{
    const QString kind = ShapeStyle::kindOf(typeName);
    return m_defaultShapeStyles.value(kind, ShapeStyle::defaultFor(kind));
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

bool CanvasScene::canAlignSelection() const
{
    return m_editorMode == EditorMode::Edit && selectedShapes().size() > 1;
}

// A ring is a wheel, not a scattering: each shape is turned so the side facing
// the circle lies along it, and they are packed edge to edge with the ring
// sized to hold exactly that many of them.
//
// The radius is what makes the packing exact. A shape of width w standing on a
// circle of radius r covers an angle of 2*asin(w / 2r) -- the chord, not the
// arc, because it is the flat side that touches its neighbours. The whole ring
// closes when those angles add to a full turn, which is one equation in r;
// there is no closed form, so it is bisected. Using the arc instead is the
// obvious shortcut and leaves a visible gap at every joint.
void CanvasScene::ringSelection(const QVector<ShapeItem *> &shapes,
                                const QVector<QRectF> &boxes, const QRectF &all, bool outside,
                                qreal gap)
{
    const QPointF centre = all.center();

    // A shape has edges, and one of them is nearer the middle of the ring than
    // the rest. That edge is the one the user means: it goes on the circle,
    // and whatever stands behind it points away. Nothing here looks at width
    // or height -- a box standing on its short side and the same box lying on
    // its long one are different arrangements, and which was drawn is the
    // answer, not which number is larger.
    QVector<QPointF> faceFrom;   // the edge, in the shape's own coordinates
    QVector<QPointF> faceTo;
    QVector<qreal> widths;       // its length, which is what has to fit
    QVector<qreal> rounds;
    qreal widest = 0.0;
    qreal total = 0.0;

    for (int i = 0; i < shapes.size(); ++i)
    {
        ShapeItem *shape = shapes.at(i);
        QPolygonF own;
        const int corners = shape->nodeCount();
        if (corners >= 3)
        {
            for (int c = 0; c < corners; ++c)
            {
                own << shape->nodePosition(c);
            }
        }
        else
        {
            // A circle has no edges to choose between, so the box it fills
            // stands in for one and any of its sides will do.
            own = QPolygonF(shape->rect());
        }

        const QPolygonF there = shape->mapToScene(own);
        int nearest = 0;
        qreal best = std::numeric_limits<qreal>::max();
        for (int c = 0; c < own.size(); ++c)
        {
            const QPointF middle = (there.at(c) + there.at((c + 1) % own.size())) / 2.0;
            const qreal reach = QLineF(centre, middle).length();
            if (reach < best)
            {
                best = reach;
                nearest = c;
            }
        }

        const int next = (nearest + 1) % own.size();
        faceFrom.append(own.at(nearest));
        faceTo.append(own.at(next));
        const qreal length = QLineF(there.at(nearest), there.at(next)).length();
        widths.append(length);
        widest = qMax(widest, length);
        total += length;
        rounds.append(qBound(0.0, shape->cornerRadius(), length / 2.0));
    }

    // Somewhere between "every shape flat against the circle" and a ring so
    // large the chords are the faces; the answer is always between them.
    qreal low = qMax(widest, qAbs(gap)) / 2.0 + 0.001;
    qreal high = qMax(total + qAbs(gap) * shapes.size(), widest) + 1.0;

    // Half the turn one shape takes up on a ring of this size. With square
    // corners that is the chord: half the face subtends asin(w / 2r). Rounding
    // the corners cuts away exactly the material that was doing the touching,
    // which is why a ring of rounded boxes came out with a gap at every joint.
    //
    // What touches then is the two corner arcs, and they touch when their
    // centres are 2*rho apart. Each centre sits a rho in from the end of the
    // face and a rho out from the face itself, so it stands at (ax, ay) from
    // the ring's centre, and the share works out as the angle to that centre
    // plus the angle the arc adds beyond it. At rho = 0 both terms collapse
    // back to asin(w / 2r), which is how the square-cornered case stays exact.
    const auto halfAngle = [&](int i, qreal radius) {
        const qreal w = widths.at(i);
        const qreal rho = rounds.at(i);
        const qreal middle = std::sqrt(qMax(0.0, radius * radius - w * w / 4.0));
        if (rho <= 0.0 || middle <= 0.0)
        {
            return std::asin(qMin(1.0, w / (2.0 * radius)));
        }
        const qreal ax = w / 2.0 - rho;
        const qreal ay = middle + rho;
        const qreal reach = std::hypot(ax, ay);
        return std::atan2(ax, ay) + std::asin(qBound(-1.0, rho / reach, 1.0));
    };

    // Every shape's own share plus one gap each, which is what has to add to a
    // whole turn. A gap pushed negative is an overlap and the equation holds.
    const auto turnAt = [&](qreal radius) {
        qreal turn = 2.0 * std::asin(qBound(-1.0, gap / (2.0 * radius), 1.0)) * widths.size();
        for (int i = 0; i < widths.size(); ++i)
        {
            turn += 2.0 * halfAngle(i, radius);
        }
        return turn;
    };
    for (int step = 0; step < 60; ++step)
    {
        const qreal mid = (low + high) / 2.0;
        // A bigger ring subtends less, so the two ends move the way they do.
        if (turnAt(mid) > 2.0 * M_PI)
        {
            low = mid;
        }
        else
        {
            high = mid;
        }
    }
    // The ring is exactly the size that holds them edge to edge. Keeping the
    // radius they happen to stand at was the obvious kindness and it is wrong:
    // any circle bigger than this one leaves a gap at every joint, and the
    // whole point of the arrangement is that there are none.
    const qreal path = (low + high) / 2.0;

    // Kept in the order they already stand around the centre, so nothing jumps
    // across the ring, and started where the first of them is, so asking twice
    // does not spin it.
    QVector<int> order;
    for (int i = 0; i < shapes.size(); ++i)
    {
        order.append(i);
    }
    const auto bearing = [&](int i) {
        const QPointF from = boxes.at(i).center() - centre;
        return std::atan2(from.y(), from.x());
    };
    std::sort(order.begin(), order.end(),
              [&](int a, int b) { return bearing(a) < bearing(b); });

    // Each shape takes the share its own face needs, not an even slice: a ring
    // of shapes of different sizes has to give the big ones more of the turn or
    // the small ones stand apart. It still comes out exact, because the radius
    // above was chosen to make these shares add to a whole turn.
    const qreal gapAngle = 2.0 * std::asin(qBound(-1.0, gap / (2.0 * path), 1.0));
    qreal angle = order.isEmpty() ? 0.0 : bearing(order.first());
    for (const int i : std::as_const(order))
    {
        const qreal half = halfAngle(i, path);
        // Half its own share before it and half after, so neighbours meet.
        angle += half;

        ShapeItem *shape = shapes.at(i);

        // Turned by the difference between where its chosen edge points now
        // and where it has to point: across the radius, which is what makes it
        // a chord of the ring. Half a turn more for the inside, so the shape
        // hangs towards the centre instead of away from it.
        const QPointF was = shape->mapToScene(faceTo.at(i)) - shape->mapToScene(faceFrom.at(i));
        const qreal facing = std::atan2(was.y(), was.x());
        const qreal wanted = angle + M_PI / 2.0;
        shape->setRotation(shape->rotation() + qRadiansToDegrees(wanted - facing));

        // Laying the edge along the ring says nothing about which side of it
        // the rest of the shape ended up on, and that depends on which way
        // round the outline happens to run. Asked rather than assumed: the
        // body has to stand outside the circle, or inside it, and half a turn
        // puts it right where it does not. Without this the shapes hung the
        // wrong way and every pair of neighbours overlapped.
        const QPointF outward(std::cos(angle), std::sin(angle));
        const QPointF body = shape->mapToScene(shape->rect().center())
                             - (shape->mapToScene(faceFrom.at(i))
                                + shape->mapToScene(faceTo.at(i)))
                                   / 2.0;
        const qreal side = body.x() * outward.x() + body.y() * outward.y();
        if ((outside && side < 0.0) || (!outside && side > 0.0))
        {
            shape->setRotation(shape->rotation() + 180.0);
        }

        // The face is a chord of the circle, not a tangent to it: its two ends
        // sit on the circle and its middle falls short of it by the sagitta.
        // Placing the middle on the circle instead leaves every pair of
        // neighbours meeting outside it, which is a visible gap at each of the
        // joints and the whole ring failing to close.
        const qreal faceMiddle =
            std::sqrt(qMax(0.0, path * path - widths.at(i) * widths.at(i) / 4.0));
        const QPointF lands(centre.x() + faceMiddle * std::cos(angle),
                            centre.y() + faceMiddle * std::sin(angle));
        // Measured after the turn, since turning moves the edge with it.
        const QPointF now =
            (shape->mapToScene(faceFrom.at(i)) + shape->mapToScene(faceTo.at(i))) / 2.0;
        shape->setPos(shape->pos() + (lands - now));

        angle += half + gapAngle;
    }

    refreshGroupOrigin();
    update();

    notifyEdit(outside ? tr("Ring outside") : tr("Ring inside"));
}


// Even gaps in one direction, the other left alone. The shapes keep the order
// they already stand in, so distributing does not shuffle them, and the first
// one does not move -- everything is measured from where it already is.
void CanvasScene::distributeSelection(const QVector<ShapeItem *> &shapes,
                                      const QVector<QRectF> &boxes, const QRectF &all,
                                      bool horizontal, qreal gap)
{
    QVector<int> order;
    for (int i = 0; i < shapes.size(); ++i)
    {
        order.append(i);
    }
    std::sort(order.begin(), order.end(), [&](int a, int b) {
        return horizontal ? boxes.at(a).left() < boxes.at(b).left()
                          : boxes.at(a).top() < boxes.at(b).top();
    });

    qreal at = horizontal ? all.left() : all.top();
    for (const int i : std::as_const(order))
    {
        const QRectF &box = boxes.at(i);
        const QPointF wanted = horizontal ? QPointF(at, box.top()) : QPointF(box.left(), at);
        shapes.at(i)->setPos(shapes.at(i)->pos() + (wanted - box.topLeft()));
        at += (horizontal ? box.width() : box.height()) + gap;
    }

    refreshGroupOrigin();
    update();
    notifyEdit(horizontal ? tr("Distribute across") : tr("Distribute down"));
}

qreal CanvasScene::evenGap(bool horizontal) const
{
    QVector<ShapeItem *> picked = selectedShapes();
    if (picked.size() < 2)
    {
        return 0.0;
    }
    QRectF all;
    qreal filled = 0.0;
    for (ShapeItem *shape : std::as_const(picked))
    {
        const QRectF box = shape->mapToScene(shape->rect()).boundingRect();
        all = all.isNull() ? box : all.united(box);
        filled += horizontal ? box.width() : box.height();
    }
    const qreal span = horizontal ? all.width() : all.height();
    return (span - filled) / (picked.size() - 1);
}

void CanvasScene::alignSelection(Align edge, qreal gap)
{
    const QVector<ShapeItem *> shapes = selectedShapes();
    if (!canAlignSelection())
    {
        return;
    }

    // Each shape is lined up by the box it covers on the canvas, turning and
    // all -- what the eye sees, not the rectangle it was drawn with, or a
    // rotated shape would settle somewhere its own outline never reaches.
    QVector<QRectF> boxes;
    boxes.reserve(shapes.size());
    QRectF all;
    for (ShapeItem *shape : shapes)
    {
        const QRectF box = shape->mapToScene(shape->rect()).boundingRect();
        boxes.append(box);
        all = all.isNull() ? box : all.united(box);
    }

    if (edge == Align::RingOutside || edge == Align::RingInside)
    {
        ringSelection(shapes, boxes, all, edge == Align::RingOutside, gap);
        return;
    }

    if (edge == Align::DistributeAcross || edge == Align::DistributeDown)
    {
        distributeSelection(shapes, boxes, all, edge == Align::DistributeAcross, gap);
        return;
    }

    QString what;
    for (int i = 0; i < shapes.size(); ++i)
    {
        const QRectF &box = boxes.at(i);
        QPointF delta;
        switch (edge)
        {
        case Align::Left:
            delta.setX(all.left() - box.left());
            what = tr("Align left");
            break;
        case Align::HorizontalCentre:
            delta.setX(all.center().x() - box.center().x());
            what = tr("Align centres across");
            break;
        case Align::Right:
            delta.setX(all.right() - box.right());
            what = tr("Align right");
            break;
        case Align::Top:
            delta.setY(all.top() - box.top());
            what = tr("Align top");
            break;
        case Align::VerticalCentre:
            delta.setY(all.center().y() - box.center().y());
            what = tr("Align centres down");
            break;
        case Align::Bottom:
            delta.setY(all.bottom() - box.bottom());
            what = tr("Align bottom");
            break;
        }
        shapes.at(i)->setPos(shapes.at(i)->pos() + delta);
    }

    refreshGroupOrigin();
    update();
    // No merge key: two alignments in a row are two things to undo, where a
    // nudge held down is one.
    notifyEdit(what);
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

qreal CanvasScene::selectionRotation() const
{
    return m_active ? m_active->rotation() : 0.0;
}

void CanvasScene::turnSelectionTo(qreal degrees)
{
    if (!m_active)
    {
        return;
    }
    const qreal turned = degrees - m_active->rotation();
    if (qFuzzyIsNull(turned))
    {
        return;
    }
    if (m_editSelection.isEmpty())
    {
        m_active->setRotation(degrees);
        update();
        return;
    }

    // The same orbit a drag builds: each shape gains the angle and its origin
    // is carried round the group's pivot, so the arrangement turns rather than
    // every shape spinning where it stands.
    refreshGroupOrigin();
    QTransform orbit;
    orbit.translate(m_groupOrigin.x(), m_groupOrigin.y());
    orbit.rotate(turned);
    orbit.translate(-m_groupOrigin.x(), -m_groupOrigin.y());

    const auto turn = [&](ShapeItem *shape) {
        const QPointF originScene = shape->pos() + shape->origin();
        shape->setRotation(shape->rotation() + turned);
        shape->setPos(orbit.map(originScene) - shape->origin());
    };
    turn(m_active);
    for (ShapeItem *shape : std::as_const(m_editSelection))
    {
        turn(shape);
    }
    update();
}

void CanvasScene::moveSelectionBy(const QPointF &by)
{
    for (ShapeItem *shape : selectedShapes())
    {
        shape->setPos(shape->pos() + by);
    }
    refreshGroupOrigin();
    update();
}

void CanvasScene::scaleSelectionBy(qreal across, qreal down)
{
    if (across <= 0.0 || down <= 0.0)
    {
        return;
    }
    const QRectF box = editSelectionBounds();
    if (box.isNull())
    {
        return;
    }

    // Measured from the corner the box starts at, so what is typed into Width
    // is what the box comes out as -- scaling about the middle would move the
    // left edge as well and the number would never settle.
    const QPointF from = box.topLeft();
    for (ShapeItem *shape : selectedShapes())
    {
        const QRectF own = shape->rect();
        shape->setRect(QRectF(own.topLeft(), QSizeF(own.width() * across, own.height() * down)));
        const QPointF at = shape->pos() - from;
        shape->setPos(from + QPointF(at.x() * across, at.y() * down));
    }
    refreshGroupOrigin();
    update();
}

void CanvasScene::setControls(const QVector<SceneControl> &controls)
{
    m_controls = controls;
    // A control that has gone takes its value with it; one that has arrived
    // starts where the scene says.
    QHash<QString, QVariant> values;
    QHash<QString, bool> enabled;
    for (const SceneControl &control : m_controls)
    {
        values.insert(control.id, m_controlValues.value(control.id, control.startingValue()));
        enabled.insert(control.id, m_controlEnabled.value(control.id, true));
    }
    m_controlValues = values;
    m_controlEnabled = enabled;
    announceChange(Change::Controls);
}

const SceneControl *CanvasScene::controlWithId(const QString &id) const
{
    for (const SceneControl &control : m_controls)
    {
        if (control.id == id)
        {
            return &control;
        }
    }
    return nullptr;
}

QString CanvasScene::uniqueControlId(const QString &desired, int except) const
{
    QString wanted = desired.isEmpty() ? QStringLiteral("control") : desired;
    const auto taken = [&](const QString &name) {
        for (int i = 0; i < m_controls.size(); ++i)
        {
            if (i != except && m_controls.at(i).id == name)
            {
                return true;
            }
        }
        return false;
    };
    if (!taken(wanted))
    {
        return wanted;
    }
    for (int n = 2;; ++n)
    {
        const QString tried = QStringLiteral("%1%2").arg(wanted).arg(n);
        if (!taken(tried))
        {
            return tried;
        }
    }
}

// A control renamed is a property key in every rule that reads or writes it,
// and the other object of every event that names it. They follow, the way they
// follow any other rename.
void CanvasScene::renameControlInRules(const QString &previous, const QString &current)
{
    if (previous == current)
    {
        return;
    }
    // Every property of it, so a rule naming any of them follows the rename.
    QHash<QString, QString> moved;
    for (const QString &what : { SceneControl::valueProperty(), SceneControl::enabledProperty(),
                                 SceneControl::captionProperty() }) {
        moved.insert(SceneControl::key(previous, what), SceneControl::key(current, what));
    }

    bool touched = false;
    const auto follow = [&](QString &key) {
        const auto it = moved.constFind(key);
        if (it != moved.constEnd())
        {
            key = *it;
            touched = true;
        }
    };
    for (Rule &rule : m_rules)
    {
        for (RuleCondition &condition : rule.conditions)
        {
            if (condition.subjectName == Rule::controls())
            {
                follow(condition.conditionKey);
                if (condition.isEvent() && condition.conditionValue.toString() == previous)
                {
                    condition.conditionValue = current;
                    touched = true;
                }
            }
        }
        for (RuleAction &action : rule.actions)
        {
            if (action.targetName == Rule::controls())
            {
                follow(action.propertyKey);
            }
            if (action.sourceObject == Rule::controls())
            {
                follow(action.sourceProperty);
            }
        }
    }
    if (touched)
    {
        announceChange(Change::Rules);
    }
}

QVariant CanvasScene::controlValue(const QString &name) const
{
    const auto it = m_controlValues.constFind(name);
    if (it != m_controlValues.constEnd())
    {
        return *it;
    }
    const SceneControl *control = controlWithId(name);
    return control ? control->startingValue() : QVariant();
}

bool CanvasScene::controlEnabled(const QString &name) const
{
    return m_controlEnabled.value(name, true);
}

void CanvasScene::setControlValue(const QString &name, const QVariant &value)
{
    const SceneControl *control = controlWithId(name);
    if (!control || m_controlValues.value(name) == value)
    {
        return;
    }
    m_controlValues.insert(name, value);
    // A button says pressed and released; everything else says it changed.
    if (control->kind == SceneControl::Kind::Button)
    {
        m_controlEvents.append({ name, value.toBool() ? Rule::controlPressedEvent()
                                                      : Rule::controlReleasedEvent() });
    }
    m_controlEvents.append({ name, Rule::controlChangedEvent() });
    emit controlValuesChanged();
}

void CanvasScene::setControlEnabled(const QString &name, bool enabled)
{
    if (!controlWithId(name) || m_controlEnabled.value(name, true) == enabled)
    {
        return;
    }
    m_controlEnabled.insert(name, enabled);
    emit controlValuesChanged();
}

QString CanvasScene::controlCaption(const QString &id) const
{
    const auto it = m_controlCaptions.constFind(id);
    if (it != m_controlCaptions.constEnd() && !it->isEmpty())
    {
        return *it;
    }
    const SceneControl *control = controlWithId(id);
    return control && !control->caption.isEmpty() ? control->caption : id;
}

void CanvasScene::setControlCaption(const QString &id, const QString &caption)
{
    if (!controlWithId(id) || controlCaption(id) == caption)
    {
        return;
    }
    m_controlCaptions.insert(id, caption);
    // A value change, not a different set of controls: rebuilding the widgets
    // here deleted the button that was still held down, so its release never
    // arrived and its value stayed true -- leaving every other press a no-op.
    emit controlValuesChanged();
}

void CanvasScene::resetControls()
{
    m_controlEvents.clear();
    m_controlCaptions.clear();
    for (const SceneControl &control : std::as_const(m_controls))
    {
        m_controlValues.insert(control.id, control.startingValue());
        m_controlEnabled.insert(control.id, true);
    }
    emit controlValuesChanged();
}

QVector<CanvasScene::ControlEvent> CanvasScene::takeControlEvents()
{
    QVector<ControlEvent> taken;
    taken.swap(m_controlEvents);
    return taken;
}

void CanvasScene::setCaption(const QString &text, bool shown)
{
    if (m_caption == text && m_captionShown == shown)
    {
        return;
    }
    m_caption = text;
    m_captionShown = shown;
    emit captionChanged();
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
    // Whatever was already picked keeps the place it had; whatever is new goes
    // on the end. Each Shift-click adds one, so the end of the list is the one
    // just picked and the front is the one picked first.
    QList<int> kept;
    for (int node : std::as_const(m_nodePickOrder))
    {
        if (m_selectedNodes.contains(node))
        {
            kept.append(node);
        }
    }
    QList<int> added(m_selectedNodes.begin(), m_selectedNodes.end());
    std::sort(added.begin(), added.end());
    for (int node : std::as_const(added))
    {
        if (!kept.contains(node))
        {
            kept.append(node);
        }
    }
    m_nodePickOrder = kept;

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
    // A ray and a blast are selected on their own, not as the active shape,
    // so Delete reached the guard below and did nothing at all. They are what
    // is picked, so they are what goes.
    if (RayItem *ray = m_selectedRay)
    {
        const QString name = ray->name();
        removeRay(ray);
        notifyEdit(tr("Delete %1").arg(name));
        return;
    }
    if (ExplosionItem *explosion = m_selectedExplosion)
    {
        const QString name = explosion->name();
        removeExplosion(explosion);
        notifyEdit(tr("Delete %1").arg(name));
        return;
    }

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
    announceChange(Change::Shapes);
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
            item->setPos(itemPos);
        addItem(item);
        item->setName(uniqueName(item->name(), item));
        announceChange(Change::Shapes);
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
    if (m_dragMode == DragMode::RubberBand && !m_bandRect.isNull())
    {
        const QColor colour =
            m_editorMode == EditorMode::Edit ? m_selectionColor : m_physicsSelectionColor;
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, false);
        QPen pen(colour);
        pen.setStyle(Qt::DashLine);
        pen.setCosmetic(true);
        painter->setPen(pen);
        QColor wash = colour;
        wash.setAlpha(40);
        painter->setBrush(wash);
        painter->drawRect(m_bandRect);
        painter->restore();
    }

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
            // Only the kinds drawn between two anchors: a revolute's spring is
            // rotational, and a zigzag along its ring would mean nothing.
            const bool coil = m_springsAsCoils
                              && (kind == physics::JointVisual::Segment
                                  || kind == physics::JointVisual::Axis)
                              && isSpringJoint(joint);
            if (span.length() > 0.01)
            {
                if (coil)
                {
                    shaft = springPath(joint, a, b);
                }
                else if (kindStyle == JointStyle::Rod)
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
                // A dashed coil is a scattering of ticks, not a spring.
                shaftPen.setStyle(coil ? Qt::SolidLine : penStyle(kindStyle));
                shaftPen.setCapStyle(coil ? Qt::RoundCap : Qt::FlatCap);
                shaftPen.setJoinStyle(Qt::RoundJoin);
                painter->setPen(shaftPen);
                painter->setBrush(Qt::NoBrush);
                painter->drawPath(shaft);
            }

            // How far it is allowed to travel, where the engine says it has a
            // limit at all. The two numbers are read by the joint's kind and
            // not by their names: a distance along the shaft for the ones
            // drawn between two anchors, an angle about the hub for a pivot.
            const JointLimits stops = jointLimits(joint);
            if (stops.enforced && !qFuzzyCompare(stops.lower, stops.upper))
            {
                QPen limitPen(kindColor);
                limitPen.setWidthF(qMax(1.0, m_jointWaistWidth * 0.6));
                limitPen.setCapStyle(Qt::FlatCap);
                painter->setPen(limitPen);
                painter->setBrush(Qt::NoBrush);

                const qreal tick = qMax(5.0, ring * 1.6);
                if ((kind == physics::JointVisual::Segment
                     || kind == physics::JointVisual::Axis)
                    && span.length() > 0.01)
                {
                    const QPointF along = (b - a) / span.length();
                    const QPointF across(-along.y(), along.x());
                    for (const qreal at : { stops.lower, stops.upper })
                    {
                        const QPointF stop = a + along * at;
                        painter->drawLine(stop - across * tick, stop + across * tick);
                    }
                    // The run between them, set off to one side so it is not
                    // lost in the shaft -- or in the coil, which is the case
                    // the limits are worth seeing in.
                    const QPointF side = across * (tick * 0.6);
                    painter->drawLine(a + along * stops.lower + side,
                                      a + along * stops.upper + side);
                }
                else if (kind == physics::JointVisual::Pivot)
                {
                    // Measured from the line between the anchors, which is
                    // what the joint's axis was taken from. An engine that
                    // counts its angles from the bodies' own reference frame
                    // will have the sector turned by however far they have
                    // since moved.
                    const qreal reach = qMax(ring * 2.6, 15.0);
                    const qreal base = span.length() > 0.01 ? QLineF(a, b).angle() : 0.0;
                    painter->drawArc(QRectF(a.x() - reach, a.y() - reach, reach * 2, reach * 2),
                                     qRound((base + stops.lower) * 16.0),
                                     qRound((stops.upper - stops.lower) * 16.0));
                    for (const qreal at : { stops.lower, stops.upper })
                    {
                        const qreal radians = qDegreesToRadians(base + at);
                        // Qt measures its angles the other way up from its
                        // coordinates, which is why the sine is negated.
                        const QPointF out(std::cos(radians), -std::sin(radians));
                        painter->drawLine(a + out * (reach * 0.7), a + out * (reach * 1.15));
                    }
                }
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

void CanvasScene::keyReleaseEvent(QKeyEvent *event)
{
    if (m_simulationRunning && !event->isAutoRepeat())
    {
        emit keyReleasedDuringRun(event->key(), event->modifiers());
    }
    QGraphicsScene::keyReleaseEvent(event);
}

void CanvasScene::keyPressEvent(QKeyEvent *event)
{
    // While a run is going the keyboard belongs to the rules. Taken here and
    // not passed on, or the arrow keys would nudge whatever was selected at
    // the same time as driving whatever the rules drive.
    if (m_simulationRunning)
    {
        if (!event->isAutoRepeat())
        {
            emit keyPressedDuringRun(event->key(), event->modifiers());
        }
        event->accept();
        return;
    }

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
        // Shift, the same as everywhere else Enter has a bigger version: while
        // a polygon is being drawn Enter finishes it open and Shift+Enter
        // closes it, and that is answered further up.
        if (event->modifiers().testFlag(Qt::ShiftModifier))
        {
            subdivideSelectedNodes();
        }
        else
        {
            handleEditModeEnter();
        }
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

// Ctrl+Enter puts a node in the middle of every edge the selection spans. Plain
// Enter inserts one node between two nodes that are already neighbours; this is
// for the other case -- picking the two ends of a run and splitting all of it at
// once, whether or not the nodes in between were picked too.
void CanvasScene::subdivideSelectedNodes()
{
    if (!m_active || m_selectedNodes.size() < 2 || !m_active->supportsNodeEditing())
    {
        return;
    }

    const int count = m_active->nodeCount();
    QList<int> picked;
    for (int node : std::as_const(m_nodePickOrder))
    {
        if (node >= 0 && node < count)
        {
            picked.append(node);
        }
    }
    if (picked.size() < 2)
    {
        return;
    }

    // Between any two nodes of a closed outline there are two runs, so which
    // one is meant has to come from somewhere: it starts at the node picked
    // first and goes clockwise. Which way round the indices run is the
    // outline's own business -- the shoelace sum says, remembering that the
    // canvas has y growing downwards, so a positive sum is clockwise on screen.
    int step = 1;
    if (m_active->isClosed())
    {
        qreal twiceArea = 0.0;
        for (int at = 0; at < count; ++at)
        {
            const QPointF here = m_active->nodePosition(at);
            const QPointF next = m_active->nodePosition((at + 1) % count);
            twiceArea += here.x() * next.y() - next.x() * here.y();
        }
        step = twiceArea >= 0.0 ? 1 : -1;
    }

    // Walk from each picked node to the next, collecting the edges passed. An
    // edge is named by the lower of the two indices it joins, except the one
    // that closes the outline, which is named by the last.
    QList<int> edges;
    for (int at = 0; at + 1 < picked.size(); ++at)
    {
        int node = picked.at(at);
        const int until = picked.at(at + 1);
        for (int guard = 0; node != until && guard <= count; ++guard)
        {
            int next = node + step;
            if (!m_active->isClosed() && (next < 0 || next >= count))
            {
                break; // an open run has only the one way to go
            }
            next = (next + count) % count;
            const bool closingEdge = (node == count - 1 && next == 0)
                                     || (node == 0 && next == count - 1);
            const int edge = closingEdge ? count - 1 : qMin(node, next);
            if (!edges.contains(edge))
            {
                edges.append(edge);
            }
            node = next;
        }
    }
    if (edges.isEmpty())
    {
        return;
    }

    // Backwards: inserting shifts every index above it, and an edge named
    // before the insert would then point at the wrong pair.
    std::sort(edges.begin(), edges.end(), std::greater<int>());
    for (int edge : std::as_const(edges))
    {
        m_active->insertNodeBetween(edge, (edge + 1) % m_active->nodeCount());
    }

    setNodeSelection({});
    update();
    notifyEdit(tr("Split %n edge(s) of %1", nullptr, int(edges.size()))
                   .arg(m_active->name()));
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

    if (event->button() == Qt::MiddleButton)
    {
        m_dragMode = DragMode::PanField;
        m_panLastScreenPos = event->screenPos();
        event->accept();
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
        const bool additive = event->modifiers().testFlag(Qt::ShiftModifier);
        // A press on something already picked keeps the whole selection, so
        // several bodies can be dragged together -- narrowing to the one under
        // the pointer happens on release, if the press turned out to be a
        // click. Without this, pressing to drag threw the rest away first.
        m_physicsClickCandidate = nullptr;
        const bool keepGroup = hit && !additive && m_physicsSelection.size() > 1
                               && m_physicsSelection.contains(hit);
        if (keepGroup)
        {
            m_physicsClickCandidate = hit;
            m_pressScenePos = scenePos;
        }
        // Shift on empty field keeps what is already picked, since the box
        // about to be swept adds to it.
        else if (hit || !additive)
        {
            selectForPhysics(hit, additive);
        }
        if (!hit)
        {
            m_dragMode = DragMode::RubberBand;
            m_bandStart = scenePos;
            m_bandRect = QRectF(scenePos, scenePos);
        }
        else if (event->button() == Qt::LeftButton)
        {
            // Dragging a shape moves it, as in Edit mode -- but a whole body at
            // once, since moving one shape out of a body would deform it.
            m_bodyDragShapes.clear();
            m_bodyDragStartPositions.clear();

            // Everything picked travels together. Taking only the shape under
            // the pointer made picking several of them pointless: they could
            // be selected and then moved one at a time.
            QVector<ShapeItem *> dragging;
            // The one under the pointer leads, whatever order they were picked
            // in: snapping measures from the leading piece, and it is the one
            // being dragged that should land on the grid.
            QVector<ShapeItem *> leads { hit };
            if (m_physicsSelection.contains(hit))
            {
                for (ShapeItem *picked : std::as_const(m_physicsSelection))
                {
                    if (picked != hit)
                    {
                        leads.append(picked);
                    }
                }
            }
            int bodies = 0;
            for (ShapeItem *lead : leads)
            {
                PhysicsBody *body = lead->body();
                const QVector<ShapeItem *> group =
                    body ? body->shapes() : QVector<ShapeItem *> { lead };
                bool fresh = false;
                for (ShapeItem *member : group)
                {
                    if (dragging.contains(member))
                    {
                        continue;
                    }
                    dragging.append(member);
                    fresh = true;
                }
                if (fresh)
                {
                    ++bodies;
                }
            }
            m_bodyDragShapes = dragging;
            if (bodies > 1)
            {
                m_bodyDragLabel = tr("%n bodies", nullptr, bodies);
            }
            else if (PhysicsBody *body = hit->body())
            {
                m_bodyDragLabel = body->name();
            }
            else
            {
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

        // Shift is two things at once: it adds a shape to the selection, and
        // while dragging it suspends snapping. A press on a handle can only
        // mean the second -- there is no shape to add that is not already the
        // one being resized -- so the handles are tested first. Without this,
        // holding Shift before grabbing a handle picked the shape underneath
        // and the drag never started, and the only way to drag without snapping
        // was to start the drag and press Shift afterwards.
        const auto pressedOnAHandle = [&] {
            if (groupHandleAt(scenePos) >= 0 || groupOriginHandleContains(scenePos))
            {
                return true;
            }
            if (m_active->mode() == ShapeMode::Rotating
                && m_active->originHandleContains(local))
            {
                return true;
            }
            return m_active->mode() == ShapeMode::Selected && geometryEditingAllowed()
                   && m_active->handleAt(local) != HandleId::None;
        };

        // Shift adds a shape to the selection instead of replacing it -- but
        // only while shapes are what is being picked. Node editing spends
        // Shift on picking vertices, and turning spends it on slowing the turn
        // down, so neither of those is touched here.
        if (event->modifiers().testFlag(Qt::ShiftModifier)
            && m_active->mode() != ShapeMode::Editing
            && m_active->mode() != ShapeMode::Rotating && !pressedOnAHandle())
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
            if (!picked)
            {
                m_dragMode = DragMode::RubberBand;
                m_bandStart = scenePos;
                m_bandRect = QRectF(scenePos, scenePos);
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
                m_rotateLastAngle = m_rotateStartAngle;
                m_rotateTurned = 0.0;
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
                    // Shift on a node picks it as well as suspending snapping,
                    // and which was meant is not known until the mouse either
                    // moves or does not. So the drag begins either way and the
                    // picking is done on release, if nothing moved.
                    m_shiftPickNode = node;
                    m_shiftPickWasSelected = m_selectedNodes.contains(node);
                    if (!m_shiftPickWasSelected)
                    {
                        QSet<int> selection = m_selectedNodes;
                        selection.insert(node);
                        setNodeSelection(selection);
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
                m_moveStartPos = m_active->pos();
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
        m_moveStartPos = shape->pos();
        beginGroupDrag();
        return;
    }

    m_dragMode = DragMode::RubberBand;
    m_bandStart = scenePos;
    m_bandRect = QRectF(scenePos, scenePos);
}

// How much of the mouse's turn the shape takes while Shift is held. A fifth is
// slow enough to place a degree by hand and fast enough not to feel stuck.
static constexpr qreal kFineTurn = 0.2;

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

    if (m_dragMode == DragMode::RubberBand)
    {
        const QRectF was = m_bandRect;
        m_bandRect = QRectF(m_bandStart, event->scenePos()).normalized();
        // Both boxes, and a little over: the outgoing one has to be painted
        // out wherever it reached past the new one.
        update(was.united(m_bandRect).adjusted(-2, -2, 2, 2));
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

        // Accumulated rather than measured from where the drag began, so that
        // holding Shift can slow the turn down without the shape jumping the
        // moment the key goes down or comes up: each scrap of mouse movement
        // is weighed as it arrives and added to what has been turned so far.
        qreal step = angle - m_rotateLastAngle;
        while (step > 180.0)
            step -= 360.0;
        while (step < -180.0)
            step += 360.0;
        m_rotateLastAngle = angle;
        m_rotateTurned += step * (m_snapSuspended ? kFineTurn : 1.0);
        const qreal turned = m_rotateTurned;

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
        // Which tells a Shift drag from a Shift click, once the button is up.
        if (!delta.isNull())
        {
            m_editNodeMoved = true;
        }
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

    if (m_dragMode == DragMode::RubberBand)
    {
        const QRectF band = m_bandRect;
        m_dragMode = DragMode::None;
        m_bandRect = QRectF();
        update(band.adjusted(-2, -2, 2, 2));
        // A press that never moved is a click on empty field, which has
        // already cleared the selection; sweeping nothing must not be made to
        // mean "pick everything the point touches".
        if (band.width() > 0.0 || band.height() > 0.0)
        {
            applyRubberBand(band, event->modifiers().testFlag(Qt::ShiftModifier));
        }
        event->accept();
        return;
    }

    if (m_dragMode != DragMode::None)
    {
        const DragMode finished = m_dragMode;
        m_dragMode = DragMode::None;

        // A Shift press on a node that never moved was somebody picking it, not
        // dragging it: taken off the selection here, where it is finally known
        // which of the two it was.
        const int picked = m_shiftPickNode;
        const bool wasPicked = m_shiftPickWasSelected;
        m_shiftPickNode = -1;
        if (finished == DragMode::EditNode && picked >= 0 && !m_editNodeMoved)
        {
            // Nothing moved, so it was a click: one that lands on a node
            // already picked takes it off again, and one on a node that was not
            // leaves it picked, which the press has already done.
            if (wasPicked && m_selectedNodes.size() > 1)
            {
                QSet<int> selection = m_selectedNodes;
                selection.remove(picked);
                setNodeSelection(selection);
            }
            m_editNodeMoved = false;
            m_dragMode = DragMode::None;
            event->accept();
            return;
        }
        m_editNodeMoved = false;

        // Physics-mode drags have no m_active to hang the undo label on, and
        // the joints anchored to what moved need their cached ends refreshed.
        if (finished == DragMode::MoveBody)
        {
            const QPointF now = m_draggedRay         ? m_draggedRay->pos()
                                : m_draggedExplosion ? m_draggedExplosion->pos()
                                                     : (m_bodyDragShapes.isEmpty() ? m_bodyDragStartPositions.value(0)
                                                                                   : m_bodyDragShapes.first()->pos());
            const bool moved = now != m_bodyDragStartPositions.value(0);
            // Pressed and released on one of several without moving: that is a
            // click, and a click without a modifier picks the one thing, the
            // same as it does everywhere else.
            if (!moved && m_physicsClickCandidate
                && QLineF(m_pressScenePos, event->scenePos()).length() < 3.0)
            {
                ShapeItem *only = m_physicsClickCandidate;
                m_physicsClickCandidate = nullptr;
                selectForPhysics(only, false);
            }
            m_physicsClickCandidate = nullptr;
            m_draggedExplosion = nullptr;
            m_draggedRay = nullptr;
            m_bodyDragShapes.clear();
            m_bodyDragStartPositions.clear();
            if (moved)
            {
                announceChange(Change::Bodies);
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
                // Selecting a shape presses and releases on it, which is a
                // move of nothing. Recorded, it left the scene looking edited
                // and lit Save up over a click.
                if (m_active->pos() != m_moveStartPos)
                {
                    notifyEdit(m_editSelection.isEmpty()
                                   ? tr("Move %1").arg(m_active->name())
                                   : tr("Move %n shapes", nullptr, int(m_editSelection.size()) + 1));
                }
                break;
            case DragMode::Rotate:
                // Left as it came out of the drag, a shape taken round twice
                // carries 720 degrees for ever after.
                m_active->setRotation(normalisedDegrees(m_active->rotation()));
                for (ShapeItem *shape : std::as_const(m_editSelection))
                {
                    shape->setRotation(normalisedDegrees(shape->rotation()));
                }
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
            case DragMode::RubberBand:
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
