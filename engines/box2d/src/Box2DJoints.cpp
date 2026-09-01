#include "Box2DEngine.h"

#include <QLineF>
#include <QtMath>

#include <utility>

// Turns an engine-neutral JointDesc into whichever b2*JointDef its type names.

namespace physics {

namespace {

// Values arrive in a bag keyed by JointParam::key. A key that isn't there
// means the joint was made before that parameter existed -- fall back rather
// than fail, so an older scene file still loads.
bool boolValue(const QVariantMap &params, const char *key, bool fallback = false)
{
    const auto it = params.constFind(QLatin1String(key));
    return it == params.constEnd() ? fallback : it->toBool();
}

qreal realValue(const QVariantMap &params, const char *key, qreal fallback = 0.0)
{
    const auto it = params.constFind(QLatin1String(key));
    return it == params.constEnd() ? fallback : it->toDouble();
}

float radians(const QVariantMap &params, const char *key, qreal fallback = 0.0)
{
    return static_cast<float>(qDegreesToRadians(realValue(params, key, fallback)));
}

// Box2D asserts lower <= upper (and, for angles, both inside +/-0.99*pi), and
// an assert aborts the process. These numbers come straight from a property
// table, so the pair is put in order here instead. A limit entered backwards
// means the same span either way round.
struct Limits
{
    float lower = 0.0f;
    float upper = 0.0f;
};

Limits translationLimits(const QVariantMap &params, qreal pixelsPerMeter)
{
    const auto toMetres = [&params, pixelsPerMeter](const char *key) {
        return static_cast<float>(realValue(params, key) / pixelsPerMeter);
    };
    float lower = toMetres("lowerTranslation");
    float upper = toMetres("upperTranslation");
    if (lower > upper)
        std::swap(lower, upper);
    return {lower, upper};
}

Limits angleLimits(const QVariantMap &params)
{
    // Just inside the half-turn Box2D allows: its assert is a strict
    // comparison, so a value rounded up onto the bound fails it.
    constexpr float kBound = 0.98f * B2_PI;
    float lower = qBound(-kBound, radians(params, "lowerAngle"), kBound);
    float upper = qBound(-kBound, radians(params, "upperAngle"), kBound);
    if (lower > upper)
        std::swap(lower, upper);
    return {lower, upper};
}

} // namespace

JointHandle Box2DEngine::addJoint(const JointDesc &desc)
{
    if (!b2World_IsValid(m_worldId))
        return kInvalidJoint;
    if (desc.bodyA < 0 || desc.bodyA >= static_cast<BodyHandle>(m_bodies.size()))
        return kInvalidJoint;
    if (desc.bodyB < 0 || desc.bodyB >= static_cast<BodyHandle>(m_bodies.size()))
        return kInvalidJoint;
    if (desc.bodyA == desc.bodyB)
        return kInvalidJoint; // a joint to itself constrains nothing

    const b2BodyId bodyA = m_bodies[desc.bodyA];
    const b2BodyId bodyB = m_bodies[desc.bodyB];

    // Scene units to metres, for every length-valued parameter.
    const auto metres = [this](qreal sceneUnits) {
        return static_cast<float>(sceneUnits / m_pixelsPerMeter);
    };

    // Anchors arrive in scene coordinates. Box2D wants them in each body's own
    // frame, which is exactly what b2Body_GetLocalPoint does -- so the editor
    // never has to think about body frames.
    const auto localAnchor = [this](b2BodyId body, const QPointF &scenePoint) {
        return b2Body_GetLocalPoint(body, toMeters(scenePoint));
    };

    // With fewer anchors than the joint wants, reuse the last one given; with
    // none, fall back to each body's own origin.
    const QPointF sceneA = desc.anchors.isEmpty() ? QPointF() : desc.anchors.first();
    const QPointF sceneB = desc.anchors.size() > 1 ? desc.anchors.at(1) : sceneA;

    const b2Vec2 anchorA = desc.anchors.isEmpty() ? b2Vec2 { 0.0f, 0.0f }
                                                  : localAnchor(bodyA, sceneA);
    const b2Vec2 anchorB = desc.anchors.isEmpty() ? b2Vec2 { 0.0f, 0.0f }
                                                  : localAnchor(bodyB, sceneB);

    // The axis is a direction, so it is rotated into body A's frame but not
    // translated, and must be unit length.
    const b2Rot rotationA = b2Body_GetRotation(bodyA);
    b2Vec2 axis { static_cast<float>(desc.axis.x()), static_cast<float>(desc.axis.y()) };
    if (b2Length(axis) < 1e-6f)
        axis = b2Vec2 { 1.0f, 0.0f };
    const b2Vec2 localAxisA = b2InvRotateVector(rotationA, b2Normalize(axis));

    // Where a reference angle isn't given, the bodies' current relative angle
    // is used, so a joint made in place starts unstrained.
    const float restingAngle =
        b2Rot_GetAngle(b2Body_GetRotation(bodyB)) - b2Rot_GetAngle(rotationA);
    const float referenceAngle = desc.params.contains(QStringLiteral("referenceAngle"))
                                     ? restingAngle + radians(desc.params, "referenceAngle")
                                     : restingAngle;

    b2JointId joint = b2_nullJointId;

    if (desc.typeId == QLatin1String("revolute")) {
        b2RevoluteJointDef def = b2DefaultRevoluteJointDef();
        def.bodyIdA = bodyA;
        def.bodyIdB = bodyB;
        def.localAnchorA = anchorA;
        def.localAnchorB = anchorB;
        def.referenceAngle = referenceAngle;
        def.targetAngle = radians(desc.params, "targetAngle");
        def.enableSpring = boolValue(desc.params, "enableSpring");
        def.hertz = static_cast<float>(realValue(desc.params, "hertz"));
        def.dampingRatio = static_cast<float>(realValue(desc.params, "dampingRatio"));
        def.enableLimit = boolValue(desc.params, "enableLimit");
        const Limits angles = angleLimits(desc.params);
        def.lowerAngle = angles.lower;
        def.upperAngle = angles.upper;
        def.enableMotor = boolValue(desc.params, "enableMotor");
        def.maxMotorTorque = static_cast<float>(realValue(desc.params, "maxMotorTorque"));
        def.motorSpeed = radians(desc.params, "motorSpeed");
        def.drawSize = static_cast<float>(realValue(desc.params, "drawSize", 0.25));
        def.collideConnected = desc.collideConnected;
        joint = b2CreateRevoluteJoint(m_worldId, &def);

    } else if (desc.typeId == QLatin1String("distance")) {
        b2DistanceJointDef def = b2DefaultDistanceJointDef();
        def.bodyIdA = bodyA;
        def.bodyIdB = bodyB;
        def.localAnchorA = anchorA;
        def.localAnchorB = anchorB;

        // Length zero means "however far apart the anchors already are", which
        // is what you want when the joint is dropped onto an existing layout.
        const qreal requested = realValue(desc.params, "length");
        const qreal separation = QLineF(sceneA, sceneB).length();
        def.length = qMax(metres(requested > 0.0 ? requested : separation), 0.001f);

        def.enableSpring = boolValue(desc.params, "enableSpring");
        def.hertz = static_cast<float>(realValue(desc.params, "hertz"));
        def.dampingRatio = static_cast<float>(realValue(desc.params, "dampingRatio"));
        def.enableLimit = boolValue(desc.params, "enableLimit");
        def.minLength = metres(realValue(desc.params, "minLength"));
        // Zero maxLength means unbounded. Box2D's own default is its internal
        // B2_HUGE (100000 length units), which isn't in the public headers and
        // isn't a number a spin box can hold -- so zero stands in for it.
        const qreal maxLength = realValue(desc.params, "maxLength");
        def.maxLength = maxLength > 0.0 ? metres(maxLength) : 100000.0f;
        def.enableMotor = boolValue(desc.params, "enableMotor");
        def.maxMotorForce = static_cast<float>(realValue(desc.params, "maxMotorForce"));
        // Scene units per second, converted the same way a length is.
        def.motorSpeed = metres(realValue(desc.params, "motorSpeed"));
        def.collideConnected = desc.collideConnected;
        joint = b2CreateDistanceJoint(m_worldId, &def);

    } else if (desc.typeId == QLatin1String("weld")) {
        b2WeldJointDef def = b2DefaultWeldJointDef();
        def.bodyIdA = bodyA;
        def.bodyIdB = bodyB;
        def.localAnchorA = anchorA;
        def.localAnchorB = anchorB;
        def.referenceAngle = referenceAngle;
        def.linearHertz = static_cast<float>(realValue(desc.params, "linearHertz"));
        def.angularHertz = static_cast<float>(realValue(desc.params, "angularHertz"));
        def.linearDampingRatio = static_cast<float>(realValue(desc.params, "linearDampingRatio"));
        def.angularDampingRatio = static_cast<float>(realValue(desc.params, "angularDampingRatio"));
        def.collideConnected = desc.collideConnected;
        joint = b2CreateWeldJoint(m_worldId, &def);

    } else if (desc.typeId == QLatin1String("prismatic")) {
        b2PrismaticJointDef def = b2DefaultPrismaticJointDef();
        def.bodyIdA = bodyA;
        def.bodyIdB = bodyB;
        def.localAnchorA = anchorA;
        def.localAnchorB = anchorB;
        def.localAxisA = localAxisA;
        def.referenceAngle = referenceAngle;
        def.targetTranslation = metres(realValue(desc.params, "targetTranslation"));
        def.enableSpring = boolValue(desc.params, "enableSpring");
        def.hertz = static_cast<float>(realValue(desc.params, "hertz"));
        def.dampingRatio = static_cast<float>(realValue(desc.params, "dampingRatio"));
        def.enableLimit = boolValue(desc.params, "enableLimit");
        const Limits span = translationLimits(desc.params, m_pixelsPerMeter);
        def.lowerTranslation = span.lower;
        def.upperTranslation = span.upper;
        def.enableMotor = boolValue(desc.params, "enableMotor");
        def.maxMotorForce = static_cast<float>(realValue(desc.params, "maxMotorForce"));
        // Scene units per second; see the distance joint above.
        def.motorSpeed = metres(realValue(desc.params, "motorSpeed"));
        def.collideConnected = desc.collideConnected;
        joint = b2CreatePrismaticJoint(m_worldId, &def);

    } else if (desc.typeId == QLatin1String("wheel")) {
        b2WheelJointDef def = b2DefaultWheelJointDef();
        def.bodyIdA = bodyA;
        def.bodyIdB = bodyB;
        def.localAnchorA = anchorA;
        def.localAnchorB = anchorB;
        def.localAxisA = localAxisA;
        def.enableSpring = boolValue(desc.params, "enableSpring", true);
        def.hertz = static_cast<float>(realValue(desc.params, "hertz", 1.0));
        def.dampingRatio = static_cast<float>(realValue(desc.params, "dampingRatio", 0.7));
        def.enableLimit = boolValue(desc.params, "enableLimit");
        const Limits span = translationLimits(desc.params, m_pixelsPerMeter);
        def.lowerTranslation = span.lower;
        def.upperTranslation = span.upper;
        def.enableMotor = boolValue(desc.params, "enableMotor");
        def.maxMotorTorque = static_cast<float>(realValue(desc.params, "maxMotorTorque"));
        def.motorSpeed = radians(desc.params, "motorSpeed");
        def.collideConnected = desc.collideConnected;
        joint = b2CreateWheelJoint(m_worldId, &def);

    } else if (desc.typeId == QLatin1String("motor")) {
        b2MotorJointDef def = b2DefaultMotorJointDef();
        def.bodyIdA = bodyA;
        def.bodyIdB = bodyB;
        def.linearOffset = b2Vec2 { metres(realValue(desc.params, "linearOffsetX")),
                                    metres(realValue(desc.params, "linearOffsetY")) };
        def.angularOffset = radians(desc.params, "angularOffset");
        def.maxForce = static_cast<float>(realValue(desc.params, "maxForce", 1.0));
        def.maxTorque = static_cast<float>(realValue(desc.params, "maxTorque", 1.0));
        def.correctionFactor = static_cast<float>(realValue(desc.params, "correctionFactor", 0.3));
        def.collideConnected = desc.collideConnected;
        joint = b2CreateMotorJoint(m_worldId, &def);

    } else if (desc.typeId == QLatin1String("mouse")) {
        // Box2D asserts rather than fails if body A moves, and an assert in a
        // release-mode solver is a crash with no message. Refusing here turns
        // it into a joint the run reports as skipped.
        if (b2Body_GetType(bodyA) != b2_staticBody)
            return kInvalidJoint;

        b2MouseJointDef def = b2DefaultMouseJointDef();
        def.bodyIdA = bodyA;
        def.bodyIdB = bodyB;
        // The one joint whose anchor is a world point rather than a spot on a
        // body: it is the target being tracked, not an attachment.
        def.target = toMeters(sceneA);
        def.hertz = static_cast<float>(realValue(desc.params, "hertz", 4.0));
        def.dampingRatio = static_cast<float>(realValue(desc.params, "dampingRatio", 1.0));
        def.maxForce = static_cast<float>(realValue(desc.params, "maxForce", 1.0));
        def.collideConnected = desc.collideConnected;
        joint = b2CreateMouseJoint(m_worldId, &def);

    } else if (desc.typeId == QLatin1String("filter")) {
        b2FilterJointDef def = b2DefaultFilterJointDef();
        def.bodyIdA = bodyA;
        def.bodyIdB = bodyB;
        joint = b2CreateFilterJoint(m_worldId, &def);

    } else {
        return kInvalidJoint; // a type this backend doesn't offer
    }

    if (!b2Joint_IsValid(joint))
        return kInvalidJoint;

    // Constraint softness is common to every type and set after creation --
    // there is no b2*JointDef field for it.
    if (desc.params.contains(QStringLiteral("constraintHertz"))
        || desc.params.contains(QStringLiteral("constraintDampingRatio"))) {
        b2Joint_SetConstraintTuning(
            joint, static_cast<float>(realValue(desc.params, "constraintHertz", 60.0)),
            static_cast<float>(realValue(desc.params, "constraintDampingRatio", 2.0)));
    }

    m_joints.append(joint);
    return static_cast<JointHandle>(m_joints.size() - 1);
}

// --- live changes -----------------------------------------------------------
// The setters Box2D offers on a joint that already exists.

void Box2DEngine::setJointParam(JointHandle handle, const QString &key, const QVariant &value)
{
    if (handle < 0 || handle >= m_joints.size())
        return;
    const b2JointId joint = m_joints[handle];
    if (!b2Joint_IsValid(joint))
        return;

    const float number = static_cast<float>(value.toDouble());
    const bool flag = value.toBool();
    // Lengths and linear speeds arrive in scene units, the same as everywhere
    // else, and convert the same way.
    const float metres = static_cast<float>(value.toDouble() / m_pixelsPerMeter);
    const float radians = static_cast<float>(qDegreesToRadians(value.toDouble()));

    // A body that has settled is asleep, and a sleeping body ignores its
    // joints -- so a motor switched on by a rule would do nothing until
    // something else happened to wake it.
    b2Joint_WakeBodies(joint);

    switch (b2Joint_GetType(joint)) {
    case b2_revoluteJoint:
        if (key == QLatin1String("enableMotor"))         b2RevoluteJoint_EnableMotor(joint, flag);
        else if (key == QLatin1String("motorSpeed"))     b2RevoluteJoint_SetMotorSpeed(joint, radians);
        else if (key == QLatin1String("maxMotorTorque")) b2RevoluteJoint_SetMaxMotorTorque(joint, number);
        else if (key == QLatin1String("enableLimit"))    b2RevoluteJoint_EnableLimit(joint, flag);
        else if (key == QLatin1String("enableSpring"))   b2RevoluteJoint_EnableSpring(joint, flag);
        else if (key == QLatin1String("hertz"))          b2RevoluteJoint_SetSpringHertz(joint, number);
        else if (key == QLatin1String("dampingRatio"))   b2RevoluteJoint_SetSpringDampingRatio(joint, number);
        break;

    case b2_prismaticJoint:
        if (key == QLatin1String("enableMotor"))         b2PrismaticJoint_EnableMotor(joint, flag);
        else if (key == QLatin1String("motorSpeed"))     b2PrismaticJoint_SetMotorSpeed(joint, metres);
        else if (key == QLatin1String("maxMotorForce"))  b2PrismaticJoint_SetMaxMotorForce(joint, number);
        else if (key == QLatin1String("enableLimit"))    b2PrismaticJoint_EnableLimit(joint, flag);
        else if (key == QLatin1String("enableSpring"))   b2PrismaticJoint_EnableSpring(joint, flag);
        else if (key == QLatin1String("hertz"))          b2PrismaticJoint_SetSpringHertz(joint, number);
        else if (key == QLatin1String("dampingRatio"))   b2PrismaticJoint_SetSpringDampingRatio(joint, number);
        break;

    case b2_wheelJoint:
        if (key == QLatin1String("enableMotor"))         b2WheelJoint_EnableMotor(joint, flag);
        else if (key == QLatin1String("motorSpeed"))     b2WheelJoint_SetMotorSpeed(joint, radians);
        else if (key == QLatin1String("maxMotorTorque")) b2WheelJoint_SetMaxMotorTorque(joint, number);
        else if (key == QLatin1String("enableLimit"))    b2WheelJoint_EnableLimit(joint, flag);
        else if (key == QLatin1String("enableSpring"))   b2WheelJoint_EnableSpring(joint, flag);
        else if (key == QLatin1String("hertz"))          b2WheelJoint_SetSpringHertz(joint, number);
        else if (key == QLatin1String("dampingRatio"))   b2WheelJoint_SetSpringDampingRatio(joint, number);
        break;

    case b2_distanceJoint:
        if (key == QLatin1String("enableMotor"))         b2DistanceJoint_EnableMotor(joint, flag);
        else if (key == QLatin1String("motorSpeed"))     b2DistanceJoint_SetMotorSpeed(joint, metres);
        else if (key == QLatin1String("maxMotorForce"))  b2DistanceJoint_SetMaxMotorForce(joint, number);
        else if (key == QLatin1String("enableLimit"))    b2DistanceJoint_EnableLimit(joint, flag);
        else if (key == QLatin1String("enableSpring"))   b2DistanceJoint_EnableSpring(joint, flag);
        else if (key == QLatin1String("hertz"))          b2DistanceJoint_SetSpringHertz(joint, number);
        else if (key == QLatin1String("dampingRatio"))   b2DistanceJoint_SetSpringDampingRatio(joint, number);
        else if (key == QLatin1String("length"))         b2DistanceJoint_SetLength(joint, metres);
        break;

    case b2_motorJoint:
        if (key == QLatin1String("maxForce"))            b2MotorJoint_SetMaxForce(joint, number);
        else if (key == QLatin1String("maxTorque"))      b2MotorJoint_SetMaxTorque(joint, number);
        else if (key == QLatin1String("correctionFactor")) b2MotorJoint_SetCorrectionFactor(joint, number);
        else if (key == QLatin1String("angularOffset"))  b2MotorJoint_SetAngularOffset(joint, radians);
        // The offset is one b2Vec2 but two properties, so the component that
        // is not being set is read back rather than assumed to be zero.
        else if (key == QLatin1String("linearOffsetX")) {
            b2Vec2 offset = b2MotorJoint_GetLinearOffset(joint);
            offset.x = metres;
            b2MotorJoint_SetLinearOffset(joint, offset);
        } else if (key == QLatin1String("linearOffsetY")) {
            b2Vec2 offset = b2MotorJoint_GetLinearOffset(joint);
            offset.y = metres;
            b2MotorJoint_SetLinearOffset(joint, offset);
        }
        break;

    default:
        break;
    }
}

void Box2DEngine::setBodyParam(BodyHandle handle, const QString &key, const QVariant &value)
{
    if (handle < 0 || handle >= static_cast<BodyHandle>(m_bodies.size()))
        return;
    const b2BodyId body = m_bodies[handle];
    if (!b2Body_IsValid(body))
        return;

    const float number = static_cast<float>(value.toDouble());
    const bool flag = value.toBool();

    // Placing a body: it arrives without travelling. Read-modify-write,
    // because Box2D moves position and rotation together.
    if (key == QLatin1String("positionX") || key == QLatin1String("positionY")
        || key == QLatin1String("angle")) {
        b2Vec2 position = b2Body_GetPosition(body);
        b2Rot rotation = b2Body_GetRotation(body);
        if (key == QLatin1String("positionX"))
            position.x = static_cast<float>(value.toDouble() / m_pixelsPerMeter);
        else if (key == QLatin1String("positionY"))
            position.y = static_cast<float>(value.toDouble() / m_pixelsPerMeter);
        else
            rotation = b2MakeRot(static_cast<float>(qDegreesToRadians(value.toDouble())));
        b2Body_SetTransform(body, position, rotation);
        // A body that had settled would otherwise stay put where it was.
        b2Body_SetAwake(body, true);
        return;
    }

    // A push, rather than a setting.
    if (key == QLatin1String("impulseX") || key == QLatin1String("impulseY")) {
        const float amount = static_cast<float>(value.toDouble() / m_pixelsPerMeter);
        const b2Vec2 impulse = key == QLatin1String("impulseX") ? b2Vec2 { amount, 0.0f }
                                                                : b2Vec2 { 0.0f, amount };
        b2Body_ApplyLinearImpulseToCenter(body, impulse, true);
        return;
    }

    // A force lasts as long as it is applied, and a rule fires for one step --
    // so this is a one-step shove and almost always the wrong choice next to
    // an impulse. Offered because it is what Box2D offers.
    if (key == QLatin1String("forceX") || key == QLatin1String("forceY")) {
        const float amount = static_cast<float>(value.toDouble() / m_pixelsPerMeter);
        const b2Vec2 force = key == QLatin1String("forceX") ? b2Vec2 { amount, 0.0f }
                                                            : b2Vec2 { 0.0f, amount };
        b2Body_ApplyForceToCenter(body, force, true);
        return;
    }

    // A velocity is the one thing a rule needs that is not a settings field:
    // "kick this upwards" is setting the vertical velocity, and it arrives in
    // scene units per second like every other speed the editor shows.
    if (key == QLatin1String("velocityX") || key == QLatin1String("velocityY")) {
        b2Vec2 velocity = b2Body_GetLinearVelocity(body);
        const float converted = static_cast<float>(value.toDouble() / m_pixelsPerMeter);
        if (key == QLatin1String("velocityX"))
            velocity.x = converted;
        else
            velocity.y = converted;
        b2Body_SetLinearVelocity(body, velocity);
        // A sleeping body ignores a velocity change; a kick has to wake it.
        b2Body_SetAwake(body, true);
        return;
    }

    if (key == QLatin1String("gravityScale"))        b2Body_SetGravityScale(body, number);
    else if (key == QLatin1String("linearDamping"))  b2Body_SetLinearDamping(body, number);
    else if (key == QLatin1String("angularDamping")) b2Body_SetAngularDamping(body, number);
    else if (key == QLatin1String("fixedRotation"))  b2Body_SetFixedRotation(body, flag);
    else if (key == QLatin1String("isBullet"))       b2Body_SetBullet(body, flag);
    else if (key == QLatin1String("enableSleep"))    b2Body_EnableSleep(body, flag);
    else if (key == QLatin1String("isAwake"))        b2Body_SetAwake(body, flag);
    else if (key == QLatin1String("isEnabled")) {
        // Less a property than a switch on the body existing at all, which is
        // exactly what a door or a trap wants.
        if (flag)
            b2Body_Enable(body);
        else
            b2Body_Disable(body);
    }
}

QVector<EngineEvent> Box2DEngine::pollEvents()
{
    QVector<EngineEvent> drained;
    drained.swap(m_pendingEvents);
    return drained;
}

void Box2DEngine::detectLimitEvents()
{
    m_jointLimits.resize(m_joints.size());

    for (int i = 0; i < m_joints.size(); ++i) {
        const b2JointId joint = m_joints[i];
        if (!b2Joint_IsValid(joint))
            continue;

        // How far the joint has travelled and the bounds it may travel
        // between, in whatever unit that type measures in. Types without a
        // limit are skipped.
        float value = 0.0f, lower = 0.0f, upper = 0.0f;
        bool limited = false;
        switch (b2Joint_GetType(joint)) {
        case b2_revoluteJoint:
            limited = b2RevoluteJoint_IsLimitEnabled(joint);
            value = b2RevoluteJoint_GetAngle(joint);
            lower = b2RevoluteJoint_GetLowerLimit(joint);
            upper = b2RevoluteJoint_GetUpperLimit(joint);
            break;
        case b2_prismaticJoint:
            limited = b2PrismaticJoint_IsLimitEnabled(joint);
            value = b2PrismaticJoint_GetTranslation(joint);
            lower = b2PrismaticJoint_GetLowerLimit(joint);
            upper = b2PrismaticJoint_GetUpperLimit(joint);
            break;
        default:
            break;
        }

        if (!limited) {
            m_jointLimits[i] = {false, false, true};
            continue;
        }

        // A tolerance, because a constraint solved to within a hair of its
        // bound never compares exactly equal to it.
        constexpr float kSlack = 0.005f;
        const bool atLower = value <= lower + kSlack;
        const bool atUpper = value >= upper - kSlack;

        // Edge-triggered; the first sample only establishes the baseline.
        const bool baseline = !m_jointLimits[i].sampled;
        if (baseline) {
            m_jointLimits[i] = {atLower, atUpper, true};
            continue;
        }

        if (atLower && !m_jointLimits[i].atLower) {
            m_pendingEvents.append({i, kInvalidBody, kInvalidBody, QStringLiteral("limitLower")});
            m_pendingEvents.append({i, kInvalidBody, kInvalidBody, QStringLiteral("limitEither")});
        }
        if (atUpper && !m_jointLimits[i].atUpper) {
            m_pendingEvents.append({i, kInvalidBody, kInvalidBody, QStringLiteral("limitUpper")});
            m_pendingEvents.append({i, kInvalidBody, kInvalidBody, QStringLiteral("limitEither")});
        }

        m_jointLimits[i] = {atLower, atUpper, true};
    }
}


BodyHandle Box2DEngine::handleOf(b2BodyId body) const
{
    if (!b2Body_IsValid(body))
        return kInvalidBody;
    // Written in addBody(). A body with none is one this engine did not make.
    void *stored = b2Body_GetUserData(body);
    const auto handle = static_cast<BodyHandle>(reinterpret_cast<intptr_t>(stored));
    return (handle >= 0 && handle < static_cast<BodyHandle>(m_bodies.size()))
               ? handle
               : kInvalidBody;
}

void Box2DEngine::collectContactEvents()
{
    if (!b2World_IsValid(m_worldId))
        return;

    const b2ContactEvents events = b2World_GetContactEvents(m_worldId);

    // A contact is between two *shapes*, and that is how it is reported.
    const auto raise = [this](b2ShapeId a, b2ShapeId b, const QString &id) {
        const BodyHandle firstBody = handleOf(b2Shape_GetBody(a));
        const BodyHandle secondBody = handleOf(b2Shape_GetBody(b));
        if (firstBody == kInvalidBody || secondBody == kInvalidBody)
            return;

        const auto nameOf = [this](b2ShapeId shape) {
            const auto index = static_cast<int>(
                reinterpret_cast<intptr_t>(b2Shape_GetUserData(shape)));
            return (index >= 0 && index < m_shapeNames.size()) ? m_shapeNames[index]
                                                               : QString();
        };
        const QString firstShape = nameOf(a);
        const QString secondShape = nameOf(b);

        EngineEvent one;
        one.body = firstBody;
        one.otherBody = secondBody;
        one.subjectShape = firstShape;
        one.otherShape = secondShape;
        one.eventId = id;
        m_pendingEvents.append(one);

        EngineEvent two;
        two.body = secondBody;
        two.otherBody = firstBody;
        two.subjectShape = secondShape;
        two.otherShape = firstShape;
        two.eventId = id;
        m_pendingEvents.append(two);
    };

    for (int i = 0; i < events.beginCount; ++i) {
        raise(events.beginEvents[i].shapeIdA, events.beginEvents[i].shapeIdB,
              QStringLiteral("contactBegin"));
    }
    for (int i = 0; i < events.endCount; ++i) {
        raise(events.endEvents[i].shapeIdA, events.endEvents[i].shapeIdB,
              QStringLiteral("contactEnd"));
    }
    // A hit is a contact hard enough to matter -- Box2D raises it separately,
    // above the world's hit threshold, and only for shapes asking for it.
    for (int i = 0; i < events.hitCount; ++i) {
        raise(events.hitEvents[i].shapeIdA, events.hitEvents[i].shapeIdB,
              QStringLiteral("contactHit"));
    }

    // Sensors are a separate stream: overlaps, not collisions. Reported the
    // same way round as a contact -- sensor first, whatever entered it second.
    const b2SensorEvents sensors = b2World_GetSensorEvents(m_worldId);
    for (int i = 0; i < sensors.beginCount; ++i) {
        const b2SensorBeginTouchEvent &e = sensors.beginEvents[i];
        if (b2Shape_IsValid(e.sensorShapeId) && b2Shape_IsValid(e.visitorShapeId))
            raise(e.sensorShapeId, e.visitorShapeId, QStringLiteral("sensorBegin"));
    }
    for (int i = 0; i < sensors.endCount; ++i) {
        const b2SensorEndTouchEvent &e = sensors.endEvents[i];
        // Box2D warns these may name shapes destroyed since the step.
        if (b2Shape_IsValid(e.sensorShapeId) && b2Shape_IsValid(e.visitorShapeId))
            raise(e.sensorShapeId, e.visitorShapeId, QStringLiteral("sensorEnd"));
    }
}

void Box2DEngine::explodeAt(const b2Vec2 &position, const QVariantMap &params) const
{
    // Box2D applies the impulse per unit of shape perimeter facing the blast,
    // so a big object takes a bigger shove than a small one the same distance
    // away.
    b2ExplosionDef blast = b2DefaultExplosionDef();
    blast.position = position;
    blast.radius = static_cast<float>(params.value(QStringLiteral("radius"), 0.0).toDouble()
                                      / m_pixelsPerMeter);
    blast.falloff = static_cast<float>(params.value(QStringLiteral("falloff"), 0.0).toDouble()
                                       / m_pixelsPerMeter);
    blast.impulsePerLength =
        static_cast<float>(params.value(QStringLiteral("impulse"), 0.0).toDouble()
                           / m_pixelsPerMeter);
    if (blast.radius > 0.0f)
        b2World_Explode(m_worldId, &blast);
}

void Box2DEngine::performAction(const QString &id, BodyHandle target,
                                const QVariantMap &params)
{
    if (id != QLatin1String("explode") || !b2World_IsValid(m_worldId))
        return;
    if (target < 0 || target >= static_cast<BodyHandle>(m_bodies.size()))
        return;
    const b2BodyId body = m_bodies[target];
    if (!b2Body_IsValid(body))
        return;

    // The named body says only where.
    explodeAt(b2Body_GetPosition(body), params);
}

void Box2DEngine::performActionAt(const QString &id, const QPointF &position,
                                  const QVariantMap &params)
{
    if (id != QLatin1String("explode") || !b2World_IsValid(m_worldId))
        return;
    explodeAt(toMeters(position), params);
}

void Box2DEngine::setShapeParam(const QString &name, const QString &key, const QVariant &value)
{
    const auto it = m_shapesByName.constFind(name);
    if (it == m_shapesByName.constEnd() || !b2Shape_IsValid(*it))
        return;

    const float number = static_cast<float>(value.toDouble());
    if (key == QLatin1String("density")) {
        // Recomputes the body's mass, or the change has no effect until
        // something else happens to trigger it.
        b2Shape_SetDensity(*it, number, true);
    } else if (key == QLatin1String("friction")) {
        b2Shape_SetFriction(*it, number);
    } else if (key == QLatin1String("restitution")) {
        b2Shape_SetRestitution(*it, number);
    } else if (key == QLatin1String("isSensor")) {
        // Not settable in Box2D once a shape exists; listed nowhere, and
        // ignored here rather than silently doing nothing somewhere else.
        return;
    }
}

QVariant Box2DEngine::shapeValue(const QString &name, const QString &key) const
{
    const auto it = m_shapesByName.constFind(name);
    if (it == m_shapesByName.constEnd() || !b2Shape_IsValid(*it))
        return {};

    if (key == QLatin1String("density"))     return b2Shape_GetDensity(*it);
    if (key == QLatin1String("friction"))    return b2Shape_GetFriction(*it);
    if (key == QLatin1String("restitution")) return b2Shape_GetRestitution(*it);
    return {};
}


} // namespace physics
