#include "CanvasScene.h"
#include "Joint.h"
#include "PhysicsBody.h"

#include <QLineF>
#include <QTransform>

namespace {

QTransform bodyToScene(PhysicsBody *body)
{
    QTransform transform;
    if (!body)
        return transform;
    const QPointF origin = body->originScenePos();
    transform.translate(origin.x(), origin.y());
    transform.rotate(body->rotationDegrees());
    return transform;
}

} // namespace

Joint::Joint(QObject *parent)
    : QObject(parent)
{
}

void Joint::setName(const QString &name)
{
    if (m_name == name)
        return;
    const QString previous = m_name;
    const auto *canvas = qobject_cast<const CanvasScene *>(parent());
    m_name = canvas ? canvas->uniqueName(name, this) : name;
    emit nameChanged(previous, name);
    emit propertyChanged();
}

void Joint::setTypeId(const QString &typeId)
{
    if (m_typeId == typeId)
        return;
    m_typeId = typeId;
    emit propertyChanged();
}

void Joint::setBodies(PhysicsBody *bodyA, PhysicsBody *bodyB)
{
    if (m_bodyA == bodyA && m_bodyB == bodyB)
        return;
    m_bodyA = bodyA;
    m_bodyB = bodyB;
    emit propertyChanged();
}

QPointF Joint::toBodyFrame(PhysicsBody *body, const QPointF &scenePos)
{
    if (!body)
        return scenePos;
    return bodyToScene(body).inverted().map(scenePos);
}

QPointF Joint::toSceneFrame(PhysicsBody *body, const QPointF &localPos)
{
    if (!body)
        return localPos;
    return bodyToScene(body).map(localPos);
}

QPointF Joint::anchorScenePos(End end) const
{
    return end == End::A ? toSceneFrame(m_bodyA, m_localAnchorA)
                         : toSceneFrame(m_bodyB, m_localAnchorB);
}

void Joint::setAnchorScenePos(End end, const QPointF &scenePos)
{
    if (end == End::A)
        m_localAnchorA = toBodyFrame(m_bodyA, scenePos);
    else
        m_localAnchorB = toBodyFrame(m_bodyB, scenePos);

    if (m_anchorCount < 2) {
        if (end == End::A)
            m_localAnchorB = toBodyFrame(m_bodyB, scenePos);
        else
            m_localAnchorA = toBodyFrame(m_bodyA, scenePos);
    }

    emit propertyChanged();
}

QPointF Joint::axisScene() const
{
    if (!m_bodyA)
        return m_localAxis;
    // A direction, so it is rotated but not translated.
    QTransform rotation;
    rotation.rotate(m_bodyA->rotationDegrees());
    const QPointF mapped = rotation.map(m_localAxis);
    const qreal length = QLineF(QPointF(), mapped).length();
    return length > 0.0 ? mapped / length : QPointF(1.0, 0.0);
}

void Joint::setAxisScene(const QPointF &direction)
{
    const qreal length = QLineF(QPointF(), direction).length();
    if (length <= 0.0)
        return;

    QTransform rotation;
    rotation.rotate(m_bodyA ? -m_bodyA->rotationDegrees() : 0.0);
    m_localAxis = rotation.map(direction / length);
    emit propertyChanged();
}

physics::JointDesc Joint::toJointDesc(int bodyHandleA, int bodyHandleB) const
{
    physics::JointDesc desc;
    desc.typeId = m_typeId;
    desc.name = m_name;
    desc.bodyA = bodyHandleA;
    desc.bodyB = bodyHandleB;
    desc.params = m_params;
    desc.collideConnected = m_collideConnected;
    desc.axis = axisScene();

    if (m_anchorCount > 0)
        desc.anchors.append(anchorScenePos(End::A));
    if (m_anchorCount > 1)
        desc.anchors.append(anchorScenePos(End::B));

    return desc;
}

void Joint::setCollideConnected(bool collide)
{
    if (m_collideConnected == collide)
        return;
    m_collideConnected = collide;
    emit propertyChanged();
}

void Joint::setBroken(bool broken)
{
    if (m_broken == broken)
        return;
    m_broken = broken;
    emit propertyChanged();
}
