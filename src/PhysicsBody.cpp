#include "CanvasScene.h"
#include "PhysicsBody.h"
#include "ShapeItem.h"

#include <QTransform>
#include <QtMath>

PhysicsBody::PhysicsBody(QObject *parent)
    : QObject(parent)
{
}

PhysicsBody::~PhysicsBody()
{
    for (ShapeItem *shape : m_shapes)
        shape->setBody(nullptr);
}

void PhysicsBody::setName(const QString &name)
{
    if (m_props.name == name)
        return;
    const QString previous = m_props.name;
    const auto *canvas = qobject_cast<const CanvasScene *>(parent());
    m_props.name = canvas ? canvas->uniqueName(name, this) : name;
    emit nameChanged(previous, name);
    emit propertyChanged();
}

void PhysicsBody::addShape(ShapeItem *shape)
{
    if (!shape || m_shapes.contains(shape))
        return;

    if (PhysicsBody *previous = shape->body())
        previous->removeShape(shape);

    m_shapes.append(shape);
    shape->setBody(this);
    emit membershipChanged();
}

void PhysicsBody::removeShape(ShapeItem *shape)
{
    if (!shape)
        return;
    if (m_shapes.removeOne(shape)) {
        if (shape->body() == this)
            shape->setBody(nullptr);
        emit membershipChanged();
    }
}

QPointF PhysicsBody::originScenePos() const
{
    if (m_shapes.isEmpty())
        return QPointF();
    return m_shapes.first()->pos() + m_shapes.first()->origin();
}

void PhysicsBody::setAsleep(bool asleep)
{
    if (m_asleep == asleep)
        return;
    m_asleep = asleep;

    for (ShapeItem *shape : m_shapes)
        shape->update();
}

void PhysicsBody::setRemoved(bool removed)
{
    if (m_removed == removed)
        return;
    m_removed = removed;

    // The shapes go with it. Hiding them is what stops them being drawn,
    // dragged or hit-tested; the flag is what stops everything drawn *about*
    // the body, which is not any one shape's business.
    for (ShapeItem *shape : m_shapes)
        shape->setVisible(!removed);
}

namespace {

bool polygonAreaAndCentroid(const QVector<QPointF> &points, qreal *area, QPointF *centroid)
{
    if (points.size() < 3)
        return false;

    qreal twiceArea = 0.0;
    qreal cx = 0.0;
    qreal cy = 0.0;
    for (int i = 0; i < points.size(); ++i) {
        const QPointF &a = points[i];
        const QPointF &b = points[(i + 1) % points.size()];
        const qreal cross = a.x() * b.y() - b.x() * a.y();
        twiceArea += cross;
        cx += (a.x() + b.x()) * cross;
        cy += (a.y() + b.y()) * cross;
    }
    if (qFuzzyIsNull(twiceArea))
        return false;

    *area = qAbs(twiceArea) / 2.0;
    *centroid = QPointF(cx / (3.0 * twiceArea), cy / (3.0 * twiceArea));
    return true;
}

} // namespace

QPointF PhysicsBody::centerOfMassScenePos() const
{
    qreal totalMass = 0.0;
    QPointF weighted(0.0, 0.0);

    for (ShapeItem *shape : m_shapes) {
        const physics::Geometry geometry = shape->physicsGeometry();

        qreal area = 0.0;
        QPointF centroid;
        switch (geometry.kind) {
        case physics::GeometryKind::Box:
            area = (geometry.halfExtents.x() * 2.0) * (geometry.halfExtents.y() * 2.0);
            centroid = geometry.center;
            break;
        case physics::GeometryKind::Circle:
            area = M_PI * geometry.radius * geometry.radius;
            centroid = geometry.center;
            break;
        case physics::GeometryKind::Polygon:
            if (!polygonAreaAndCentroid(geometry.points, &area, &centroid))
                area = 0.0;
            break;
        case physics::GeometryKind::Chain:
            break; // an outline encloses nothing, so it carries no mass
        }

        if (area <= 0.0)
            continue;

        const qreal mass = area * qMax(0.0, shape->part().density);
        if (mass <= 0.0)
            continue;

        // The centroid is in the shape's own frame; take it out to the scene.
        QTransform shapeToScene;
        const QPointF pivot = shape->pos() + shape->origin();
        shapeToScene.translate(pivot.x(), pivot.y());
        shapeToScene.rotate(shape->rotation());

        weighted += shapeToScene.map(centroid) * mass;
        totalMass += mass;
    }

    if (totalMass <= 0.0)
        return originScenePos();

    return weighted / totalMass;
}

qreal PhysicsBody::rotationDegrees() const
{
    return m_shapes.isEmpty() ? 0.0 : m_shapes.first()->rotation();
}

physics::BodyDesc PhysicsBody::toBodyDesc() const
{
    physics::BodyDesc desc = m_props;
    desc.parts.clear();

    if (m_shapes.isEmpty())
        return desc;

    ShapeItem *reference = m_shapes.first();
    desc.position = reference->pos() + reference->origin();
    desc.rotationDegrees = reference->rotation();

    QTransform bodyToScene;
    bodyToScene.translate(desc.position.x(), desc.position.y());
    bodyToScene.rotate(desc.rotationDegrees);
    const QTransform sceneToBody = bodyToScene.inverted();

    for (ShapeItem *shape : m_shapes) {
        physics::ShapePart part = shape->part();
        part.name = shape->name();
        part.geometry = shape->physicsGeometry();

        QTransform shapeToScene;
        const QPointF pivot = shape->pos() + shape->origin();
        shapeToScene.translate(pivot.x(), pivot.y());
        shapeToScene.rotate(shape->rotation());

        const QTransform shapeToBody = shapeToScene * sceneToBody;
        part.geometry.center = shapeToBody.map(part.geometry.center);
        for (QPointF &point : part.geometry.points)
            point = shapeToBody.map(point);
        part.geometry.rotationDegrees = shape->rotation() - desc.rotationDegrees;

        desc.parts.append(part);
    }
    return desc;
}
