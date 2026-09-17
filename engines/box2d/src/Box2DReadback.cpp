#include <QLineF>
#include <cmath>
#include "Box2DEngine.h"

#include <QObject>
#include <QtMath>
#include <QHash>

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
    // A speed, so it comes back out of the reference scale the same way it
    // went in.
    if (key == QLatin1String("sleepThreshold"))
        return b2Body_GetSleepThreshold(body) / m_motionScale;
    // b2BodyType is an enum in the same order the catalogue lists its choices,
    // and the editor stores a choice as its index.
    if (key == QLatin1String("bodyType"))
        return static_cast<int>(b2Body_GetType(body));

    // Mass and inertia are SI and pass through untouched; the centre of mass
    // is a point, and does not.
    if (key == QLatin1String("mass"))
        return b2Body_GetMass(body);
    if (key == QLatin1String("rotationalInertia"))
        return b2Body_GetRotationalInertia(body);
    if (key == QLatin1String("kineticEnergy")) {
        const float v = b2Length(b2Body_GetLinearVelocity(body));
        const float w = b2Body_GetAngularVelocity(body);
        // In millijoules, as the catalogue quotes it.
        return 500.0 * (b2Body_GetMass(body) * v * v + b2Body_GetRotationalInertia(body) * w * w);
    }
    if (key == QLatin1String("localCenterOfMassX"))
        return toScene(b2Body_GetLocalCenterOfMass(body).x);
    if (key == QLatin1String("localCenterOfMassY"))
        return toScene(b2Body_GetLocalCenterOfMass(body).y);
    if (key == QLatin1String("jointCount"))
        return b2Body_GetJointCount(body);
    if (key == QLatin1String("contactCount")) {
        // The capacity is what it might have; the call fills in what it has.
        const int capacity = b2Body_GetContactCapacity(body);
        if (capacity <= 0)
            return 0;
        QVarLengthArray<b2ContactData, 16> contacts(capacity);
        return b2Body_GetContactData(body, contacts.data(), capacity);
    }
    if (key.startsWith(QLatin1String("bounds"))) {
        const b2AABB box = b2Body_ComputeAABB(body);
        if (key == QLatin1String("boundsMinX")) return toScene(box.lowerBound.x);
        if (key == QLatin1String("boundsMinY")) return toScene(box.lowerBound.y);
        if (key == QLatin1String("boundsMaxX")) return toScene(box.upperBound.x);
        if (key == QLatin1String("boundsMaxY")) return toScene(box.upperBound.y);
    }
    if (key == QLatin1String("centerOfMassX"))
        return toScene(b2Body_GetWorldCenterOfMass(body).x);
    if (key == QLatin1String("centerOfMassY"))
        return toScene(b2Body_GetWorldCenterOfMass(body).y);

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
    // How far the solver is from holding what the joint asks. A joint being
    // pulled apart shows up here before it shows up anywhere else.
    if (key == QLatin1String("linearSeparation"))
        return toScene(b2Joint_GetLinearSeparation(joint));
    if (key == QLatin1String("angularSeparation"))
        return qRadiansToDegrees(b2Joint_GetAngularSeparation(joint));
    if (key == QLatin1String("collideConnected"))
        return b2Joint_GetCollideConnected(joint);

    // Where the limit events come from, read as a state. The engine works this
    // out after every step to decide whether the joint has just *arrived*
    // somewhere; a rule that wants to know where it is standing asks here.
    if (key == QLatin1String("atLowerLimit") || key == QLatin1String("atUpperLimit")) {
        if (static_cast<size_t>(handle) >= m_jointLimits.size())
            return false;
        const LimitState &state = m_jointLimits[handle];
        return key == QLatin1String("atLowerLimit") ? state.atLower : state.atUpper;
    }

    // Box2D holds the anchors in each body's own frame; the editor speaks
    // scene coordinates, and b2Body_GetWorldPoint is the whole conversion.
    if (key == QLatin1String("anchorAX") || key == QLatin1String("anchorAY")) {
        // this->, because the local toScene above shadows the member that
        // converts a whole point rather than one number.
        const QPointF world =
            this->toScene(b2Body_GetWorldPoint(b2Joint_GetBodyA(joint),
                                               b2Joint_GetLocalAnchorA(joint)));
        return key == QLatin1String("anchorAX") ? world.x() : world.y();
    }
    if (key == QLatin1String("anchorBX") || key == QLatin1String("anchorBY")) {
        const QPointF world =
            this->toScene(b2Body_GetWorldPoint(b2Joint_GetBodyB(joint),
                                               b2Joint_GetLocalAnchorB(joint)));
        return key == QLatin1String("anchorBX") ? world.x() : world.y();
    }
    if (key == QLatin1String("referenceAngleNow"))
        return qRadiansToDegrees(b2Joint_GetReferenceAngle(joint));
    if (key == QLatin1String("axisAngle")) {
        // The axis is stored in body A's frame, so it is rotated back out of
        // it before being reported as a scene direction.
        const b2Vec2 axis = b2RotateVector(b2Body_GetRotation(b2Joint_GetBodyA(joint)),
                                           b2Joint_GetLocalAxisA(joint));
        return qRadiansToDegrees(std::atan2(static_cast<double>(axis.y),
                                            static_cast<double>(axis.x)));
    }

    // The names a joint is *set* by, answered with what it actually is now.
    // Without these the log falls back to the stored setting and reports the
    // value the run started with for as long as the run lasts -- so a motor a
    // rule had switched on still read as off, and one it had not still read as
    // whatever the file said.
    static const QHash<QString, QString> kSettingReaders {
        {QStringLiteral("enableSpring"), QStringLiteral("springEnabled")},
        {QStringLiteral("enableLimit"),  QStringLiteral("limitEnabled")},
        {QStringLiteral("enableMotor"),  QStringLiteral("motorEnabled")},
    };
    // Asked again under the name that reads it, so every joint type's own
    // branch below answers without needing to know both spellings.
    if (const QString live = kSettingReaders.value(key); !live.isEmpty())
        return jointValue(handle, live);

    switch (b2Joint_GetType(joint)) {
    case b2_revoluteJoint:
        if (key == QLatin1String("angle"))
            return qRadiansToDegrees(b2RevoluteJoint_GetAngle(joint));
        if (key == QLatin1String("motorSpeed"))
            return qRadiansToDegrees(b2RevoluteJoint_GetMotorSpeed(joint));
        if (key == QLatin1String("motorTorque"))
            return b2RevoluteJoint_GetMotorTorque(joint);
        if (key == QLatin1String("springEnabled"))
            return b2RevoluteJoint_IsSpringEnabled(joint);
        if (key == QLatin1String("limitEnabled"))
            return b2RevoluteJoint_IsLimitEnabled(joint);
        if (key == QLatin1String("motorEnabled"))
            return b2RevoluteJoint_IsMotorEnabled(joint);
        break;

    case b2_prismaticJoint:
        // Travel from where the joint started, which is what its limits are
        // measured from and what the editor drew.
        if (key == QLatin1String("translation"))
            return toScene(b2PrismaticJoint_GetTranslation(joint) - m_travelOrigins.value(handle));
        if (key == QLatin1String("speed"))
            return toScene(b2PrismaticJoint_GetSpeed(joint));
        if (key == QLatin1String("motorSpeed"))
            return toScene(b2PrismaticJoint_GetMotorSpeed(joint));
        if (key == QLatin1String("motorForce"))
            return b2PrismaticJoint_GetMotorForce(joint);
        if (key == QLatin1String("springEnabled"))
            return b2PrismaticJoint_IsSpringEnabled(joint);
        if (key == QLatin1String("limitEnabled"))
            return b2PrismaticJoint_IsLimitEnabled(joint);
        if (key == QLatin1String("motorEnabled"))
            return b2PrismaticJoint_IsMotorEnabled(joint);
        break;

    case b2_distanceJoint:
        if (key == QLatin1String("length"))
            return toScene(b2DistanceJoint_GetLength(joint));
        // What the ends actually measure, as against the length being held
        // for: a loaded rope reads longer than it rests at.
        if (key == QLatin1String("currentLength"))
            return toScene(b2DistanceJoint_GetCurrentLength(joint));
        if (key == QLatin1String("motorSpeed"))
            return toScene(b2DistanceJoint_GetMotorSpeed(joint));
        if (key == QLatin1String("motorForce"))
            return b2DistanceJoint_GetMotorForce(joint);
        if (key == QLatin1String("springEnabled"))
            return b2DistanceJoint_IsSpringEnabled(joint);
        if (key == QLatin1String("limitEnabled"))
            return b2DistanceJoint_IsLimitEnabled(joint);
        if (key == QLatin1String("motorEnabled"))
            return b2DistanceJoint_IsMotorEnabled(joint);
        break;

    case b2_wheelJoint:
        if (key == QLatin1String("motorSpeed"))
            return qRadiansToDegrees(b2WheelJoint_GetMotorSpeed(joint));
        if (key == QLatin1String("motorTorque"))
            return b2WheelJoint_GetMotorTorque(joint);
        if (key == QLatin1String("springEnabled"))
            return b2WheelJoint_IsSpringEnabled(joint);
        if (key == QLatin1String("limitEnabled"))
            return b2WheelJoint_IsLimitEnabled(joint);
        if (key == QLatin1String("motorEnabled"))
            return b2WheelJoint_IsMotorEnabled(joint);
        break;

    case b2_mouseJoint:
        if (key == QLatin1String("targetX"))
            return toScene(b2MouseJoint_GetTarget(joint).x);
        if (key == QLatin1String("targetY"))
            return toScene(b2MouseJoint_GetTarget(joint).y);
        break;

    default:
        break;
    }

    return {};
}

QVariant Box2DEngine::worldValue(const QString &key) const
{
    if (!b2World_IsValid(m_worldId))
        return {};

    // Speeds and accelerations were scaled on the way in so that changing
    // pixelsPerMeter leaves the pace on screen alone; they come back out the
    // same way, so a rule reads the number the world settings show.
    const auto unscaled = [this](float value) { return value / m_motionScale; };

    if (key == QLatin1String("gravityX"))
        return unscaled(b2World_GetGravity(m_worldId).x);
    if (key == QLatin1String("gravityY"))
        return unscaled(b2World_GetGravity(m_worldId).y);
    if (key == QLatin1String("restitutionThreshold"))
        return unscaled(b2World_GetRestitutionThreshold(m_worldId));
    if (key == QLatin1String("hitEventThreshold"))
        return unscaled(b2World_GetHitEventThreshold(m_worldId));
    if (key == QLatin1String("maximumLinearSpeed"))
        return unscaled(b2World_GetMaximumLinearSpeed(m_worldId));

    // No getters for these three: what the world was last told is what it has.
    if (key == QLatin1String("contactHertz"))
        return m_contactTuning.hertz;
    if (key == QLatin1String("contactDampingRatio"))
        return m_contactTuning.dampingRatio;
    if (key == QLatin1String("maxContactPushSpeed"))
        return unscaled(m_contactTuning.pushSpeed);

    if (key == QLatin1String("enableSleep"))
        return b2World_IsSleepingEnabled(m_worldId);
    if (key == QLatin1String("enableContinuous"))
        return b2World_IsContinuousEnabled(m_worldId);
    if (key == QLatin1String("enableWarmStarting"))
        return b2World_IsWarmStartingEnabled(m_worldId);
    if (key == QLatin1String("enableSpeculative"))
        return m_speculative;   // b2World_EnableSpeculative has no counterpart

    if (key == QLatin1String("awakeBodyCount"))
        return b2World_GetAwakeBodyCount(m_worldId);

    // b2World_GetProfile: where the last step went, in milliseconds.
    if (key.endsWith(QLatin1String("Milliseconds"))) {
        const b2Profile profile = b2World_GetProfile(m_worldId);
        if (key == QLatin1String("stepMilliseconds"))    return profile.step;
        if (key == QLatin1String("collideMilliseconds")) return profile.collide;
        if (key == QLatin1String("solveMilliseconds"))   return profile.solve;
    }
    if (key == QLatin1String("bodyCount") || key == QLatin1String("contactCount")
        || key == QLatin1String("jointCount") || key == QLatin1String("shapeCount")
        || key == QLatin1String("islandCount") || key == QLatin1String("treeHeight")) {
        const b2Counters counters = b2World_GetCounters(m_worldId);
        if (key == QLatin1String("bodyCount"))    return counters.bodyCount;
        if (key == QLatin1String("contactCount")) return counters.contactCount;
        if (key == QLatin1String("shapeCount"))   return counters.shapeCount;
        if (key == QLatin1String("islandCount"))  return counters.islandCount;
        if (key == QLatin1String("treeHeight"))   return counters.treeHeight;
        return counters.jointCount;
    }

    return {};
}

void Box2DEngine::setWorldParam(const QString &key, const QVariant &value)
{
    if (!b2World_IsValid(m_worldId))
        return;

    const bool flag = value.toBool();
    // The mirror of worldValue's unscaling: a speed given in the world
    // settings' units becomes one in the solver's.
    const float scaled = static_cast<float>(value.toDouble() * m_motionScale);

    if (key == QLatin1String("gravityX") || key == QLatin1String("gravityY")) {
        b2Vec2 gravity = b2World_GetGravity(m_worldId);
        (key == QLatin1String("gravityX") ? gravity.x : gravity.y) = scaled;
        b2World_SetGravity(m_worldId, gravity);
        return;
    }
    if (key == QLatin1String("restitutionThreshold")) {
        b2World_SetRestitutionThreshold(m_worldId, scaled);
        return;
    }
    if (key == QLatin1String("hitEventThreshold")) {
        b2World_SetHitEventThreshold(m_worldId, scaled);
        return;
    }
    if (key == QLatin1String("maximumLinearSpeed")) {
        b2World_SetMaximumLinearSpeed(m_worldId, scaled);
        return;
    }

    // One call carries all three, so the two not being written come from the
    // copy kept when the world was made.
    if (key == QLatin1String("contactHertz") || key == QLatin1String("contactDampingRatio")
        || key == QLatin1String("maxContactPushSpeed")) {
        if (key == QLatin1String("contactHertz"))
            m_contactTuning.hertz = static_cast<float>(value.toDouble());
        else if (key == QLatin1String("contactDampingRatio"))
            m_contactTuning.dampingRatio = static_cast<float>(value.toDouble());
        else
            m_contactTuning.pushSpeed = scaled;   // a speed, unlike the other two
        b2World_SetContactTuning(m_worldId, m_contactTuning.hertz,
                                 m_contactTuning.dampingRatio, m_contactTuning.pushSpeed);
        return;
    }

    if (key == QLatin1String("enableSleep"))
        b2World_EnableSleeping(m_worldId, flag);
    else if (key == QLatin1String("enableContinuous"))
        b2World_EnableContinuous(m_worldId, flag);
    else if (key == QLatin1String("enableWarmStarting"))
        b2World_EnableWarmStarting(m_worldId, flag);
    else if (key == QLatin1String("enableSpeculative")) {
        b2World_EnableSpeculative(m_worldId, flag);
        m_speculative = flag;
    }
}

} // namespace physics
