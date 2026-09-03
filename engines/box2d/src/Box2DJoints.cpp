#include "Box2DEngine.h"

#include <QLineF>
#include <QVarLengthArray>
#include <cmath>
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

// Just inside the half-turn Box2D allows a revolute limit: its assert is a
// strict comparison, so a value rounded up onto the bound fails it.
constexpr float kAngleLimitBound = 0.98f * B2_PI;

Limits angleLimits(const QVariantMap &params)
{
    constexpr float kBound = kAngleLimitBound;
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
        // body: it is the target being tracked, not an attachment. Naming a
        // target outright overrides the anchor; leaving it at the origin means
        // "wherever the anchor was put", the same way the distance joint reads
        // a zero length.
        const QPointF target(realValue(desc.params, "targetX"),
                             realValue(desc.params, "targetY"));
        def.target = target.isNull() ? toMeters(sceneA) : toMeters(target);
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

    // Two things belong to every joint whatever its type: the constraint
    // softness, which has no b2*JointDef field and is reachable only through
    // these calls, and whether the two bodies collide.
    if (key == QLatin1String("constraintHertz")
        || key == QLatin1String("constraintDampingRatio")) {
        // One call carries both, so the half not being written is read back
        // rather than assumed to still be Box2D's default.
        float hertz = 0.0f;
        float dampingRatio = 0.0f;
        b2Joint_GetConstraintTuning(joint, &hertz, &dampingRatio);
        (key == QLatin1String("constraintHertz") ? hertz : dampingRatio) = number;
        b2Joint_SetConstraintTuning(joint, hertz, dampingRatio);
        return;
    }
    if (key == QLatin1String("collideConnected")) {
        b2Joint_SetCollideConnected(joint, flag);
        return;
    }

    // Where the joint holds, and which way it travels. Both are stored in a
    // body's own frame and both are one value out of a pair, so the current
    // one is read back, converted to scene coordinates, amended and put back.
    if (key.startsWith(QLatin1String("anchor"))) {
        const bool endA = key.at(6) == QLatin1Char('A');
        const b2BodyId body = endA ? b2Joint_GetBodyA(joint) : b2Joint_GetBodyB(joint);
        const b2Vec2 local = endA ? b2Joint_GetLocalAnchorA(joint)
                                  : b2Joint_GetLocalAnchorB(joint);
        QPointF world = toScene(b2Body_GetWorldPoint(body, local));
        if (key.endsWith(QLatin1Char('X')))
            world.setX(value.toDouble());
        else
            world.setY(value.toDouble());

        const b2Vec2 moved = b2Body_GetLocalPoint(body, toMeters(world));
        if (endA)
            b2Joint_SetLocalAnchorA(joint, moved);
        else
            b2Joint_SetLocalAnchorB(joint, moved);
        return;
    }
    if (key == QLatin1String("referenceAngleNow")) {
        b2Joint_SetReferenceAngle(joint, radians);
        return;
    }
    if (key == QLatin1String("axisAngle")) {
        const b2Rot rotationA = b2Body_GetRotation(b2Joint_GetBodyA(joint));
        const b2Vec2 world { std::cos(radians), std::sin(radians) };
        b2Joint_SetLocalAxisA(joint, b2Normalize(b2InvRotateVector(rotationA, world)));
        return;
    }

    switch (b2Joint_GetType(joint)) {
    case b2_revoluteJoint:
        if (key == QLatin1String("enableMotor"))         b2RevoluteJoint_EnableMotor(joint, flag);
        else if (key == QLatin1String("motorSpeed"))     b2RevoluteJoint_SetMotorSpeed(joint, radians);
        else if (key == QLatin1String("maxMotorTorque")) b2RevoluteJoint_SetMaxMotorTorque(joint, number);
        else if (key == QLatin1String("enableLimit"))    b2RevoluteJoint_EnableLimit(joint, flag);
        else if (key == QLatin1String("enableSpring"))   b2RevoluteJoint_EnableSpring(joint, flag);
        else if (key == QLatin1String("hertz"))          b2RevoluteJoint_SetSpringHertz(joint, number);
        else if (key == QLatin1String("dampingRatio"))   b2RevoluteJoint_SetSpringDampingRatio(joint, number);
        else if (key == QLatin1String("targetAngle"))    b2RevoluteJoint_SetTargetAngle(joint, radians);
        // A limit is two numbers and a rule writes one of them, so the other
        // is read back. Both the bounds and their order are Box2D asserts, and
        // an assert in a release solver is a crash with no message.
        else if (key == QLatin1String("lowerAngle") || key == QLatin1String("upperAngle")) {
            float lower = b2RevoluteJoint_GetLowerLimit(joint);
            float upper = b2RevoluteJoint_GetUpperLimit(joint);
            (key == QLatin1String("lowerAngle") ? lower : upper) =
                qBound(-kAngleLimitBound, radians, kAngleLimitBound);
            if (lower > upper)
                std::swap(lower, upper);
            b2RevoluteJoint_SetLimits(joint, lower, upper);
        }
        break;

    case b2_prismaticJoint:
        if (key == QLatin1String("enableMotor"))         b2PrismaticJoint_EnableMotor(joint, flag);
        else if (key == QLatin1String("motorSpeed"))     b2PrismaticJoint_SetMotorSpeed(joint, metres);
        else if (key == QLatin1String("maxMotorForce"))  b2PrismaticJoint_SetMaxMotorForce(joint, number);
        else if (key == QLatin1String("enableLimit"))    b2PrismaticJoint_EnableLimit(joint, flag);
        else if (key == QLatin1String("enableSpring"))   b2PrismaticJoint_EnableSpring(joint, flag);
        else if (key == QLatin1String("hertz"))          b2PrismaticJoint_SetSpringHertz(joint, number);
        else if (key == QLatin1String("dampingRatio"))   b2PrismaticJoint_SetSpringDampingRatio(joint, number);
        // The smooth way to move something: the spring drives the body along
        // the axis, rather than the body being placed there outright.
        else if (key == QLatin1String("targetTranslation"))
            b2PrismaticJoint_SetTargetTranslation(joint, metres);
        else if (key == QLatin1String("lowerTranslation")
                 || key == QLatin1String("upperTranslation")) {
            float lower = b2PrismaticJoint_GetLowerLimit(joint);
            float upper = b2PrismaticJoint_GetUpperLimit(joint);
            (key == QLatin1String("lowerTranslation") ? lower : upper) = metres;
            if (lower > upper)
                std::swap(lower, upper);
            b2PrismaticJoint_SetLimits(joint, lower, upper);
        }
        break;

    case b2_wheelJoint:
        if (key == QLatin1String("enableMotor"))         b2WheelJoint_EnableMotor(joint, flag);
        else if (key == QLatin1String("motorSpeed"))     b2WheelJoint_SetMotorSpeed(joint, radians);
        else if (key == QLatin1String("maxMotorTorque")) b2WheelJoint_SetMaxMotorTorque(joint, number);
        else if (key == QLatin1String("enableLimit"))    b2WheelJoint_EnableLimit(joint, flag);
        else if (key == QLatin1String("enableSpring"))   b2WheelJoint_EnableSpring(joint, flag);
        else if (key == QLatin1String("hertz"))          b2WheelJoint_SetSpringHertz(joint, number);
        else if (key == QLatin1String("dampingRatio"))   b2WheelJoint_SetSpringDampingRatio(joint, number);
        else if (key == QLatin1String("lowerTranslation")
                 || key == QLatin1String("upperTranslation")) {
            float lower = b2WheelJoint_GetLowerLimit(joint);
            float upper = b2WheelJoint_GetUpperLimit(joint);
            (key == QLatin1String("lowerTranslation") ? lower : upper) = metres;
            if (lower > upper)
                std::swap(lower, upper);
            b2WheelJoint_SetLimits(joint, lower, upper);
        }
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
        else if (key == QLatin1String("minLength") || key == QLatin1String("maxLength")) {
            float lower = b2DistanceJoint_GetMinLength(joint);
            float upper = b2DistanceJoint_GetMaxLength(joint);
            // Zero maxLength stands in for unbounded here as it does at
            // creation: Box2D's own default is an internal huge value with no
            // public name.
            (key == QLatin1String("minLength") ? lower : upper) =
                (key == QLatin1String("maxLength") && value.toDouble() <= 0.0) ? 100000.0f
                                                                              : metres;
            if (lower > upper)
                std::swap(lower, upper);
            b2DistanceJoint_SetLengthRange(joint, lower, upper);
        }
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

    case b2_mouseJoint:
        if (key == QLatin1String("hertz"))               b2MouseJoint_SetSpringHertz(joint, number);
        else if (key == QLatin1String("dampingRatio"))   b2MouseJoint_SetSpringDampingRatio(joint, number);
        else if (key == QLatin1String("maxForce"))       b2MouseJoint_SetMaxForce(joint, number);
        // Moving the target is the whole point of this joint -- it is how a
        // body is led somewhere softly instead of being placed there.
        else if (key == QLatin1String("targetX") || key == QLatin1String("targetY")) {
            b2Vec2 target = b2MouseJoint_GetTarget(joint);
            (key == QLatin1String("targetX") ? target.x : target.y) = metres;
            b2MouseJoint_SetTarget(joint, target);
        }
        break;

    case b2_weldJoint:
        if (key == QLatin1String("linearHertz"))         b2WeldJoint_SetLinearHertz(joint, number);
        else if (key == QLatin1String("angularHertz"))   b2WeldJoint_SetAngularHertz(joint, number);
        else if (key == QLatin1String("linearDampingRatio"))
            b2WeldJoint_SetLinearDampingRatio(joint, number);
        else if (key == QLatin1String("angularDampingRatio"))
            b2WeldJoint_SetAngularDampingRatio(joint, number);
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

    // Where it should be by the end of the step, rather than where it is:
    // Box2D works out the velocity that arrives there, so the body travels
    // and shoves what is in the way instead of appearing past it.
    if (key == QLatin1String("targetX") || key == QLatin1String("targetY")
        || key == QLatin1String("targetAngle")) {
        b2Transform target { b2Body_GetPosition(body), b2Body_GetRotation(body) };
        if (key == QLatin1String("targetX"))
            target.p.x = static_cast<float>(value.toDouble() / m_pixelsPerMeter);
        else if (key == QLatin1String("targetY"))
            target.p.y = static_cast<float>(value.toDouble() / m_pixelsPerMeter);
        else
            target.q = b2MakeRot(static_cast<float>(qDegreesToRadians(value.toDouble())));
        b2Body_SetTargetTransform(body, target, m_lastStep);
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

    // The turning pair of the two above. A torque is a force times a distance
    // and both are quoted in scene units, so it comes down by the scale twice
    // -- which is what keeps a number that looks sensible next to an impulse
    // behaving like one.
    if (key == QLatin1String("torque") || key == QLatin1String("angularImpulse")) {
        const float amount = static_cast<float>(value.toDouble()
                                                / (m_pixelsPerMeter * m_pixelsPerMeter));
        if (key == QLatin1String("torque"))
            b2Body_ApplyTorque(body, amount, true);
        else
            b2Body_ApplyAngularImpulse(body, amount, true);
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

    if (key == QLatin1String("angularVelocity")) {
        b2Body_SetAngularVelocity(body, static_cast<float>(qDegreesToRadians(value.toDouble())));
        b2Body_SetAwake(body, true);
        return;
    }

    // Mass and inertia normally come from the shapes and their density.
    // b2Body_SetMassData replaces the lot, so the two thirds not being written
    // are read back -- and the override lasts only until a shape or the body
    // type changes, which is Box2D's rule, not ours.
    if (key == QLatin1String("mass") || key == QLatin1String("rotationalInertia")) {
        b2MassData data = b2Body_GetMassData(body);
        (key == QLatin1String("mass") ? data.mass : data.rotationalInertia) =
            qMax(0.0f, number);
        b2Body_SetMassData(body, data);
        return;
    }

    if (key == QLatin1String("bodyType")) {
        // Stored as the index into the catalogue's list, which is in
        // b2BodyType order.
        const int index = value.toInt();
        if (index >= 0 && index < b2_bodyTypeCount)
            b2Body_SetType(body, static_cast<b2BodyType>(index));
        return;
    }

    // A speed, so quoted at the reference scale like gravity and velocity.
    if (key == QLatin1String("sleepThreshold")) {
        b2Body_SetSleepThreshold(body, static_cast<float>(value.toDouble() * m_motionScale));
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

bool Box2DEngine::preSolve(b2ShapeId shapeA, b2ShapeId shapeB)
{
    if (!b2Shape_IsValid(shapeA) || !b2Shape_IsValid(shapeB))
        return true;

    const auto nameOf = [this](b2ShapeId shape) {
        const auto index =
            static_cast<int>(reinterpret_cast<intptr_t>(b2Shape_GetUserData(shape)));
        return (index >= 0 && index < m_shapeNames.size()) ? m_shapeNames[index] : QString();
    };

    // Both ways round, as a contact is: either shape can be the one a rule
    // watches. Raised a step earlier than "begins contact", which is the only
    // reason to want it.
    const auto raise = [this, &nameOf](b2ShapeId subject, b2ShapeId other) {
        EngineEvent event;
        event.body = handleOf(b2Shape_GetBody(subject));
        event.otherBody = handleOf(b2Shape_GetBody(other));
        event.subjectShape = nameOf(subject);
        event.otherShape = nameOf(other);
        event.eventId = QStringLiteral("preSolve");
        if (event.body != kInvalidBody && event.otherBody != kInvalidBody)
            m_pendingEvents.append(event);
    };
    raise(shapeA, shapeB);
    raise(shapeB, shapeA);

    // Always solved. Cancelling a contact from here is what one-way platforms
    // are built on, but nothing in the editor says which way is through.
    return true;
}

void Box2DEngine::collectBodyEvents()
{
    if (!b2World_IsValid(m_worldId))
        return;

    // Box2D reports only the bodies the solver actually moved, and flags the
    // one step on which a body settled. Nothing needs remembering: "starts
    // moving" comes out of the rules' own rising edge, and falling asleep is
    // a single step by construction.
    const b2BodyEvents events = b2World_GetBodyEvents(m_worldId);
    for (int i = 0; i < events.moveCount; ++i) {
        const b2BodyMoveEvent &move = events.moveEvents[i];
        const BodyHandle handle = handleOf(move.bodyId);
        if (handle == kInvalidBody)
            continue;

        EngineEvent event;
        event.body = handle;
        event.eventId = move.fellAsleep ? QStringLiteral("bodyFellAsleep")
                                        : QStringLiteral("bodyMoved");
        m_pendingEvents.append(event);
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

        // Named fields, not an aggregate: EngineEvent carries two shape names
        // between the handles and the id, so a positional initialiser puts the
        // id in subjectShape and the event never reaches a rule.
        const auto raiseLimit = [this, i](const QString &id) {
            EngineEvent event;
            event.joint = i;
            event.eventId = id;
            m_pendingEvents.append(event);
        };

        if (atLower && !m_jointLimits[i].atLower) {
            raiseLimit(QStringLiteral("limitLower"));
            raiseLimit(QStringLiteral("limitEither"));
        }
        if (atUpper && !m_jointLimits[i].atUpper) {
            raiseLimit(QStringLiteral("limitUpper"));
            raiseLimit(QStringLiteral("limitEither"));
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
        // A rule can destroy a body, and Box2D then reports the contacts it
        // left behind as ended -- naming shapes that no longer exist. Asking
        // one of those which body it belongs to is not a wrong answer, it is a
        // crash. The sensor events below have always checked for this; the
        // contact ones did not, because until a rule could remove something
        // there was nothing to find.
        if (!b2Shape_IsValid(a) || !b2Shape_IsValid(b))
            return;

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
    // Unlike a begin or end, it arrives with numbers: how fast the two were
    // closing and where they met. An EngineEvent carries only names, so the
    // numbers are kept per shape for shapeValue to answer.
    for (int i = 0; i < events.hitCount; ++i) {
        const b2ContactHitEvent &e = events.hitEvents[i];
        const auto record = [this, &e](b2ShapeId shape, float normalSign) {
            if (!b2Shape_IsValid(shape))
                return;
            const auto index =
                static_cast<int>(reinterpret_cast<intptr_t>(b2Shape_GetUserData(shape)));
            if (index < 0 || index >= m_shapeNames.size() || m_shapeNames[index].isEmpty())
                return;
            HitRecord hit;
            hit.speed = e.approachSpeed * m_pixelsPerMeter;
            hit.point = toScene(e.point);
            // Box2D's normal points from A to B, so B sees it the other way.
            hit.normal = QPointF(e.normal.x * normalSign, e.normal.y * normalSign);
            m_lastHit.insert(m_shapeNames[index], hit);
        };
        record(e.shapeIdA, 1.0f);
        record(e.shapeIdB, -1.0f);

        raise(e.shapeIdA, e.shapeIdB, QStringLiteral("contactHit"));
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
    // Zero means everything, so an older scene with no such setting is
    // unchanged by it.
    const double mask = params.value(QStringLiteral("maskBits"), 0.0).toDouble();
    if (mask > 0.0)
        blast.maskBits = static_cast<uint64_t>(mask);
    if (blast.radius > 0.0f)
        b2World_Explode(m_worldId, &blast);
}

void Box2DEngine::performAction(const QString &id, BodyHandle target,
                                const QVariantMap &params)
{
    if (!b2World_IsValid(m_worldId))
        return;
    if (target < 0 || target >= static_cast<BodyHandle>(m_bodies.size()))
        return;
    const b2BodyId body = m_bodies[target];
    if (!b2Body_IsValid(body))
        return;

    if (id == QLatin1String("explode")) {
        // The named body says only where.
        explodeAt(b2Body_GetPosition(body), params);
        return;
    }

    // b2Body_ApplyLinearImpulse: the same kick as the impulse property, but
    // landing somewhere in particular. Off the centre of mass it also turns
    // the body, which is the only reason to name a point at all.
    if (id == QLatin1String("pushAt")) {
        const auto metres = [this](const QVariantMap &from, const char *key) {
            return static_cast<float>(from.value(QLatin1String(key), 0.0).toDouble()
                                      / m_pixelsPerMeter);
        };
        const b2Vec2 impulse { metres(params, "impulseX"), metres(params, "impulseY") };
        const b2Vec2 centre = b2Body_GetWorldCenterOfMass(body);
        const b2Vec2 point { centre.x + metres(params, "offsetX"),
                             centre.y + metres(params, "offsetY") };
        b2Body_ApplyLinearImpulse(body, impulse, point, true);
        return;
    }

    if (id == QLatin1String("removeBody")) {
        // Box2D takes the body's shapes and joints with it. The handle stays
        // where it is so the editor's indices keep meaning what they meant --
        // it just stops naming anything, which every reader here checks for.
        b2DestroyBody(body);
        m_bodies[target] = b2_nullBodyId;
        return;
    }
}

void Box2DEngine::performJointAction(const QString &id, JointHandle target,
                                     const QVariantMap &params)
{
    Q_UNUSED(params);
    if (!b2World_IsValid(m_worldId))
        return;
    if (target < 0 || target >= m_joints.size())
        return;
    const b2JointId joint = m_joints[target];
    if (!b2Joint_IsValid(joint))
        return;

    if (id == QLatin1String("breakJoint")) {
        // The two bodies were asleep against each other as often as not, and a
        // sleeping body would not notice the joint had gone.
        b2Joint_WakeBodies(joint);
        b2DestroyJoint(joint);
        m_joints[target] = b2_nullJointId;
    }
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

    // A settled body is asleep, and a sleeping body does not notice what its
    // shapes are made of -- a conveyor switched on by a rule would carry
    // nothing until something else disturbed it. Same reasoning as
    // b2Joint_WakeBodies in setJointParam.
    b2Body_SetAwake(b2Shape_GetBody(*it), true);

    const float number = static_cast<float>(value.toDouble());
    if (key == QLatin1String("density")) {
        // Recomputes the body's mass, or the change has no effect until
        // something else happens to trigger it.
        b2Shape_SetDensity(*it, number, true);
    } else if (key == QLatin1String("friction")) {
        b2Shape_SetFriction(*it, number);
    } else if (key == QLatin1String("restitution")) {
        b2Shape_SetRestitution(*it, number);
    } else if (key == QLatin1String("rollingResistance")
               || key == QLatin1String("tangentSpeed")) {
        // The rest of the surface goes in one struct, so the half not being
        // written is read back. Tangent speed is a speed, and converts like
        // one; rolling resistance is a ratio and does not.
        b2SurfaceMaterial material = b2Shape_GetSurfaceMaterial(*it);
        if (key == QLatin1String("rollingResistance"))
            material.rollingResistance = number;
        else
            material.tangentSpeed = static_cast<float>(value.toDouble() / m_pixelsPerMeter);
        b2Shape_SetSurfaceMaterial(*it, material);
    } else if (key == QLatin1String("categoryBits") || key == QLatin1String("maskBits")
               || key == QLatin1String("groupIndex")) {
        b2Filter filter = b2Shape_GetFilter(*it);
        if (key == QLatin1String("categoryBits"))
            filter.categoryBits = static_cast<uint64_t>(qMax(0.0, value.toDouble()));
        else if (key == QLatin1String("maskBits"))
            filter.maskBits = static_cast<uint64_t>(qMax(0.0, value.toDouble()));
        else
            filter.groupIndex = value.toInt();
        b2Shape_SetFilter(*it, filter);
    } else if (key == QLatin1String("enableContactEvents")) {
        b2Shape_EnableContactEvents(*it, value.toBool());
    } else if (key == QLatin1String("enableHitEvents")) {
        b2Shape_EnableHitEvents(*it, value.toBool());
    } else if (key == QLatin1String("enableSensorEvents")) {
        b2Shape_EnableSensorEvents(*it, value.toBool());
    } else if (key == QLatin1String("enablePreSolveEvents")) {
        b2Shape_EnablePreSolveEvents(*it, value.toBool());
    } else if (key == QLatin1String("radius")) {
        // Box2D can swap a shape's outline in place, but only for the kind it
        // already is -- so this is a circle's radius and nothing else's.
        if (b2Shape_GetType(*it) == b2_circleShape) {
            b2Circle circle = b2Shape_GetCircle(*it);
            circle.radius = static_cast<float>(qMax(0.0, value.toDouble()) / m_pixelsPerMeter);
            b2Shape_SetCircle(*it, &circle);
        }
    } else if (key == QLatin1String("isSensor")) {
        // Not settable in Box2D once a shape exists; published read-only, and
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

    if (key == QLatin1String("rollingResistance"))
        return b2Shape_GetSurfaceMaterial(*it).rollingResistance;
    if (key == QLatin1String("tangentSpeed"))
        return b2Shape_GetSurfaceMaterial(*it).tangentSpeed * m_pixelsPerMeter;

    if (key == QLatin1String("isSensor"))            return b2Shape_IsSensor(*it);
    if (key == QLatin1String("enableContactEvents")) return b2Shape_AreContactEventsEnabled(*it);
    if (key == QLatin1String("enableHitEvents"))     return b2Shape_AreHitEventsEnabled(*it);
    if (key == QLatin1String("enableSensorEvents"))  return b2Shape_AreSensorEventsEnabled(*it);
    if (key == QLatin1String("enablePreSolveEvents"))
        return b2Shape_ArePreSolveEventsEnabled(*it);

    if (key == QLatin1String("categoryBits"))
        return static_cast<double>(b2Shape_GetFilter(*it).categoryBits);
    if (key == QLatin1String("maskBits"))
        return static_cast<double>(b2Shape_GetFilter(*it).maskBits);
    if (key == QLatin1String("groupIndex")) return b2Shape_GetFilter(*it).groupIndex;

    if (key == QLatin1String("radius")) {
        if (b2Shape_GetType(*it) != b2_circleShape)
            return {};
        return b2Shape_GetCircle(*it).radius * m_pixelsPerMeter;
    }
    if (key == QLatin1String("mass")) return b2Shape_GetMassData(*it).mass;

    // How many things are inside a sensor right now -- what the begin and end
    // events cannot say without the rule keeping a tally of its own. The list
    // has to be fetched rather than just measured: Box2D warns it can name
    // shapes destroyed since the last step.
    if (key == QLatin1String("sensorOverlapCount")) {
        const int capacity = b2Shape_GetSensorCapacity(*it);
        if (capacity <= 0)
            return 0;   // not a sensor, or nothing in it
        QVarLengthArray<b2ShapeId, 32> overlaps(capacity);
        const int found = b2Shape_GetSensorOverlaps(*it, overlaps.data(), capacity);
        int alive = 0;
        for (int i = 0; i < found; ++i)
            alive += b2Shape_IsValid(overlaps[i]) ? 1 : 0;
        return alive;
    }

    // What the last hit on this shape was like. The event says it happened;
    // these say how hard and where, which is what a rule wants to compare.
    const auto hit = m_lastHit.constFind(name);
    if (hit != m_lastHit.constEnd()) {
        if (key == QLatin1String("lastHitSpeed"))   return hit->speed;
        if (key == QLatin1String("lastHitX"))       return hit->point.x();
        if (key == QLatin1String("lastHitY"))       return hit->point.y();
        if (key == QLatin1String("lastHitNormalX")) return hit->normal.x();
        if (key == QLatin1String("lastHitNormalY")) return hit->normal.y();
    } else if (key.startsWith(QLatin1String("lastHit"))) {
        // Nothing has hit it yet -- zero, so a comparison is still answerable.
        return 0.0;
    }
    return {};
}


} // namespace physics
