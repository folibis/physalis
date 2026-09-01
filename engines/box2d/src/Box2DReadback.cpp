#include <QLineF>
#include <cmath>
#include "Box2DEngine.h"

#include <QObject>
#include <QtMath>

// What a running world can be asked about.
//
// Rules are conditions over these -- "when the lift's travel reaches 250",
// "when the ball falls past y=2000" -- so this is the read side of the same
// contract setJointParam/setBodyParam form the write side of. Everything is
// returned in the units the editor shows: scene units for lengths and speeds,
// degrees for angles.

namespace physics {


QVariant Box2DEngine::bodyValue(BodyHandle handle, const QString &key) const
{
    if (handle < 0 || handle >= static_cast<BodyHandle>(m_bodies.size()))
        return {};
    const b2BodyId body = m_bodies[handle];
    if (!b2Body_IsValid(body))
        return {};

    // Back into scene units, so a rule is written against the same numbers the
    // property table shows rather than against metres.
    const auto toScene = [this](float metres) { return metres * m_pixelsPerMeter; };

    if (key == QLatin1String("positionX"))
        return toScene(b2Body_GetPosition(body).x);
    if (key == QLatin1String("positionY"))
        return toScene(b2Body_GetPosition(body).y);
    if (key == QLatin1String("angle"))
        return qRadiansToDegrees(b2Rot_GetAngle(b2Body_GetRotation(body)));
    if (key == QLatin1String("velocityX"))
        return toScene(b2Body_GetLinearVelocity(body).x);
    if (key == QLatin1String("velocityY"))
        return toScene(b2Body_GetLinearVelocity(body).y);
    if (key == QLatin1String("speed")) {
        const b2Vec2 v = b2Body_GetLinearVelocity(body);
        return toScene(b2Length(v));
    }
    if (key == QLatin1String("angularVelocity"))
        return qRadiansToDegrees(b2Body_GetAngularVelocity(body));
    if (key == QLatin1String("isAwake"))
        return b2Body_IsAwake(body);
    if (key == QLatin1String("isEnabled"))
        return b2Body_IsEnabled(body);
    if (key == QLatin1String("gravityScale"))
        return b2Body_GetGravityScale(body);
    if (key == QLatin1String("linearDamping"))
        return b2Body_GetLinearDamping(body);
    if (key == QLatin1String("angularDamping"))
        return b2Body_GetAngularDamping(body);
    if (key == QLatin1String("fixedRotation"))
        return b2Body_IsFixedRotation(body);
    if (key == QLatin1String("isBullet"))
        return b2Body_IsBullet(body);
    if (key == QLatin1String("enableSleep"))
        return b2Body_IsSleepEnabled(body);

    return {};
}


RayHit Box2DEngine::castRay(const QPointF &origin, const QPointF &translation,
                            quint64 maskBits) const
{
    RayHit result;
    if (!b2World_IsValid(m_worldId))
        return result;

    b2QueryFilter filter = b2DefaultQueryFilter();
    filter.maskBits = maskBits;

    // Closest, not every hit: "how far to the thing in the way" is what a
    // rangefinder means, and it needs no callback.
    const b2RayResult found = b2World_CastRayClosest(
        m_worldId, toMeters(origin), toMeters(translation), filter);
    if (!found.hit)
        return result;

    result.hit = true;
    result.point = QPointF(found.point.x, found.point.y) * m_pixelsPerMeter;
    result.normal = QPointF(found.normal.x, found.normal.y);
    result.distance = QLineF(origin, result.point).length();

    if (b2Shape_IsValid(found.shapeId)) {
        const auto index =
            static_cast<int>(reinterpret_cast<intptr_t>(b2Shape_GetUserData(found.shapeId)));
        if (index >= 0 && index < m_shapeNames.size())
            result.shapeName = m_shapeNames[index];
    }
    return result;
}

QVariant Box2DEngine::jointValue(JointHandle handle, const QString &key) const
{
    if (handle < 0 || handle >= m_joints.size())
        return {};
    const b2JointId joint = m_joints[handle];
    if (!b2Joint_IsValid(joint))
        return {};

    const auto toScene = [this](float metres) { return metres * m_pixelsPerMeter; };

    // How hard the joint is working to hold its two bodies together. Every
    // type reports this, so it is answered before the per-type keys.
    if (key == QLatin1String("constraintForce")) {
        const b2Vec2 force = b2Joint_GetConstraintForce(joint);
        return std::hypot(static_cast<double>(force.x), static_cast<double>(force.y));
    }
    if (key == QLatin1String("constraintTorque"))
        return static_cast<double>(b2Joint_GetConstraintTorque(joint));

    switch (b2Joint_GetType(joint)) {
    case b2_revoluteJoint:
        if (key == QLatin1String("angle"))
            return qRadiansToDegrees(b2RevoluteJoint_GetAngle(joint));
        if (key == QLatin1String("motorSpeed"))
            return qRadiansToDegrees(b2RevoluteJoint_GetMotorSpeed(joint));
        if (key == QLatin1String("motorTorque"))
            return b2RevoluteJoint_GetMotorTorque(joint);
        break;

    case b2_prismaticJoint:
        if (key == QLatin1String("translation"))
            return toScene(b2PrismaticJoint_GetTranslation(joint));
        if (key == QLatin1String("speed"))
            return toScene(b2PrismaticJoint_GetSpeed(joint));
        if (key == QLatin1String("motorSpeed"))
            return toScene(b2PrismaticJoint_GetMotorSpeed(joint));
        if (key == QLatin1String("motorForce"))
            return b2PrismaticJoint_GetMotorForce(joint);
        break;

    case b2_distanceJoint:
        if (key == QLatin1String("length"))
            return toScene(b2DistanceJoint_GetLength(joint));
        if (key == QLatin1String("motorSpeed"))
            return toScene(b2DistanceJoint_GetMotorSpeed(joint));
        break;

    case b2_wheelJoint:
        if (key == QLatin1String("motorSpeed"))
            return qRadiansToDegrees(b2WheelJoint_GetMotorSpeed(joint));
        if (key == QLatin1String("motorTorque"))
            return b2WheelJoint_GetMotorTorque(joint);
        break;

    default:
        break;
    }

    return {};
}

} // namespace physics
