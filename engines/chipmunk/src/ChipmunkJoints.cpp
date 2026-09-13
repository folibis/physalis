#include "ChipmunkEngine.h"

#include <QLineF>
#include <QtMath>
#include <algorithm>
#include <cmath>
#include <utility>

// Turns an engine-neutral JointDesc into whichever Chipmunk constraint its type
// names, and answers for it while it runs.

namespace physics {

namespace {

qreal realValue(const QVariantMap &params, const char *key, qreal fallback = 0.0)
{
    const auto it = params.constFind(QLatin1String(key));
    return it == params.constEnd() ? fallback : it->toDouble();
}

cpFloat radians(const QVariantMap &params, const char *key, qreal fallback = 0.0)
{
    return qDegreesToRadians(realValue(params, key, fallback));
}

// Zero, or anything below it, stands for Chipmunk's infinity -- which a spin
// box cannot hold, and which Chipmunk asserts against being negative.
cpFloat unlimited(qreal value)
{
    return value > 0.0 ? value : INFINITY;
}

qreal limitedOrZero(cpFloat value)
{
    return std::isfinite(value) ? value : 0.0;
}

bool actsOnAngles(const QString &typeId)
{
    return typeId == QLatin1String("dampedRotarySpring") || typeId == QLatin1String("rotaryLimit")
           || typeId == QLatin1String("ratchet") || typeId == QLatin1String("gear")
           || typeId == QLatin1String("simpleMotor");
}

bool isSpring(const QString &typeId)
{
    return typeId == QLatin1String("dampedSpring") || typeId == QLatin1String("dampedRotarySpring");
}

bool hasAnchors(const QString &typeId)
{
    return typeId == QLatin1String("pivot") || typeId == QLatin1String("pin")
           || typeId == QLatin1String("slide") || typeId == QLatin1String("dampedSpring")
           || typeId == QLatin1String("groove") || typeId == QLatin1String("mouse");
}

// Where a constraint holds each body, in that body's frame. A groove holds
// body A along a slot rather than at a point, so only its B end is one.
bool localAnchor(const cpConstraint *constraint, const QString &typeId, bool endA, cpVect *out)
{
    if (typeId == QLatin1String("pivot") || typeId == QLatin1String("mouse"))
        *out = endA ? cpPivotJointGetAnchorA(constraint) : cpPivotJointGetAnchorB(constraint);
    else if (typeId == QLatin1String("pin"))
        *out = endA ? cpPinJointGetAnchorA(constraint) : cpPinJointGetAnchorB(constraint);
    else if (typeId == QLatin1String("slide"))
        *out = endA ? cpSlideJointGetAnchorA(constraint) : cpSlideJointGetAnchorB(constraint);
    else if (typeId == QLatin1String("dampedSpring"))
        *out = endA ? cpDampedSpringGetAnchorA(constraint) : cpDampedSpringGetAnchorB(constraint);
    else if (typeId == QLatin1String("groove") && !endA)
        *out = cpGrooveJointGetAnchorB(constraint);
    else
        return false;
    return true;
}

void setLocalAnchor(cpConstraint *constraint, const QString &typeId, bool endA, cpVect local)
{
    if (typeId == QLatin1String("pivot") || typeId == QLatin1String("mouse")) {
        endA ? cpPivotJointSetAnchorA(constraint, local) : cpPivotJointSetAnchorB(constraint, local);
    } else if (typeId == QLatin1String("pin")) {
        endA ? cpPinJointSetAnchorA(constraint, local) : cpPinJointSetAnchorB(constraint, local);
    } else if (typeId == QLatin1String("slide")) {
        endA ? cpSlideJointSetAnchorA(constraint, local) : cpSlideJointSetAnchorB(constraint, local);
    } else if (typeId == QLatin1String("dampedSpring")) {
        endA ? cpDampedSpringSetAnchorA(constraint, local)
             : cpDampedSpringSetAnchorB(constraint, local);
    } else if (typeId == QLatin1String("groove") && !endA) {
        cpGrooveJointSetAnchorB(constraint, local);
    }
}

// How far apart a two-anchor constraint's ends are right now, in metres.
cpFloat currentLength(const cpConstraint *constraint, const QString &typeId)
{
    cpVect a, b;
    if (!localAnchor(constraint, typeId, true, &a) || !localAnchor(constraint, typeId, false, &b))
        return 0.0;
    return cpvdist(cpBodyLocalToWorld(cpConstraintGetBodyA(constraint), a),
                   cpBodyLocalToWorld(cpConstraintGetBodyB(constraint), b));
}

// Only a body that is in a space can be woken; one taken out by a disable
// has no space to be woken in.
void wake(cpBody *body)
{
    if (cpBodyGetSpace(body) && cpBodyGetType(body) == CP_BODY_TYPE_DYNAMIC)
        cpBodyActivate(body);
}

} // namespace

cpFloat ChipmunkEngine::effectiveMass(cpBody *a, cpBody *b)
{
    const auto massOf = [](cpBody *body) {
        const cpFloat mass = cpBodyGetMass(body);
        return cpBodyGetType(body) == CP_BODY_TYPE_DYNAMIC && std::isfinite(mass) ? mass
                                                                                  : INFINITY;
    };
    const cpFloat ma = massOf(a);
    const cpFloat mb = massOf(b);
    if (!std::isfinite(ma))
        return std::isfinite(mb) ? mb : 0.0;
    if (!std::isfinite(mb))
        return ma;
    return ma * mb / (ma + mb);
}

cpFloat ChipmunkEngine::effectiveMoment(cpBody *a, cpBody *b)
{
    const auto momentOf = [](cpBody *body) {
        const cpFloat moment = cpBodyGetMoment(body);
        return cpBodyGetType(body) == CP_BODY_TYPE_DYNAMIC && std::isfinite(moment) ? moment
                                                                                    : INFINITY;
    };
    const cpFloat ia = momentOf(a);
    const cpFloat ib = momentOf(b);
    if (!std::isfinite(ia))
        return std::isfinite(ib) ? ib : 0.0;
    if (!std::isfinite(ib))
        return ia;
    return ia * ib / (ia + ib);
}

void ChipmunkEngine::applySpring(JointRecord *joint)
{
    // A mass on a spring swings at sqrt(k / m) radians a second, and is damped
    // critically at 2 sqrt(k m) -- so these are the stiffness and damping that
    // give the frequency and ratio asked for, for what is actually on it.
    const cpFloat omega = 2.0 * M_PI * std::max(0.0, joint->hertz);
    const cpFloat stiffness = joint->sprungInertia * omega * omega;
    const cpFloat damping = 2.0 * joint->sprungInertia * std::max(0.0, joint->dampingRatio) * omega;
    if (joint->typeId == QLatin1String("dampedSpring")) {
        cpDampedSpringSetStiffness(joint->constraint, stiffness);
        cpDampedSpringSetDamping(joint->constraint, damping);
    } else {
        cpDampedRotarySpringSetStiffness(joint->constraint, stiffness);
        cpDampedRotarySpringSetDamping(joint->constraint, damping);
    }
}

JointHandle ChipmunkEngine::addJoint(const JointDesc &desc)
{
    if (!m_space)
        return kInvalidJoint;
    BodyRecord *first = bodyAt(desc.bodyA);
    if (!first)
        return kInvalidJoint;

    // No second body: the joint holds this one to the world, which Chipmunk
    // represents by its space's own static body.
    const bool toWorld = desc.bodyB < 0;
    BodyRecord *second = toWorld ? nullptr : bodyAt(desc.bodyB);
    if (!toWorld && (!second || second == first))
        return kInvalidJoint;

    const bool mouse = desc.typeId == QLatin1String("mouse");
    cpBody *bodyA = toWorld ? cpSpaceGetStaticBody(m_space) : first->body;
    cpBody *bodyB = toWorld || mouse ? first->body : second->body;

    // Between two things that cannot move a constraint has nothing to solve
    // for, and Chipmunk divides by that nothing. Refused, the run reports the
    // joint as skipped instead.
    if (!mouse && cpBodyGetType(bodyA) != CP_BODY_TYPE_DYNAMIC
        && cpBodyGetType(bodyB) != CP_BODY_TYPE_DYNAMIC)
        return kInvalidJoint;

    const QPointF sceneA = desc.anchors.isEmpty() ? QPointF() : desc.anchors.first();
    const QPointF sceneB = desc.anchors.size() > 1 ? desc.anchors.at(1) : sceneA;
    // Chipmunk holds anchors in each body's own frame; the editor gives them in
    // the scene's. With none at all, each body's own origin.
    const cpVect localA = desc.anchors.isEmpty() ? cpvzero
                                                 : cpBodyWorldToLocal(bodyA, toMeters(sceneA));
    const cpVect localB = desc.anchors.isEmpty() ? cpvzero
                                                 : cpBodyWorldToLocal(bodyB, toMeters(sceneB));
    const cpFloat separation = cpvdist(toMeters(sceneA), toMeters(sceneB));

    auto owned = std::make_unique<JointRecord>();
    JointRecord *joint = owned.get();
    joint->typeId = desc.typeId;
    joint->referenceAngle = cpBodyGetAngle(bodyB) - cpBodyGetAngle(bodyA);
    const QVariantMap &params = desc.params;
    cpConstraint *made = nullptr;

    if (desc.typeId == QLatin1String("pivot")) {
        made = cpPivotJointNew2(bodyA, bodyB, localA, localB);

    } else if (desc.typeId == QLatin1String("pin")) {
        // Chipmunk measures the distance from where the anchors are; a length
        // asked for outright replaces it.
        made = cpPinJointNew(bodyA, bodyB, localA, localB);
        const qreal distance = realValue(params, "distance");
        if (distance > 0.0)
            cpPinJointSetDist(made, metres(distance));

    } else if (desc.typeId == QLatin1String("slide")) {
        cpFloat lower = std::max(0.0, metres(realValue(params, "minLength")));
        const qreal maxLength = realValue(params, "maxLength");
        cpFloat upper = maxLength > 0.0 ? metres(maxLength) : separation;
        if (lower > upper)
            std::swap(lower, upper);
        made = cpSlideJointNew(bodyA, bodyB, localA, localB, lower, upper);

    } else if (desc.typeId == QLatin1String("groove")) {
        cpVect axis = cpv(desc.axis.x(), desc.axis.y());
        axis = cpvlengthsq(axis) > 1e-12 ? cpvnormalize(axis) : cpv(1.0, 0.0);
        cpFloat lower = metres(realValue(params, "lowerTranslation", -100.0));
        cpFloat upper = metres(realValue(params, "upperTranslation", 100.0));
        if (lower > upper)
            std::swap(lower, upper);
        // A slot of no length has no direction, and Chipmunk normalises it.
        upper = std::max(upper, lower + metres(0.01));
        const cpVect anchor = toMeters(sceneA);
        joint->grooveOrigin = cpBodyWorldToLocal(bodyA, anchor);
        joint->grooveAxis = cpvnormalize(cpvunrotate(axis, cpBodyGetRotation(bodyA)));
        made = cpGrooveJointNew(bodyA, bodyB,
                                cpBodyWorldToLocal(bodyA, cpvadd(anchor, cpvmult(axis, lower))),
                                cpBodyWorldToLocal(bodyA, cpvadd(anchor, cpvmult(axis, upper))),
                                localB);

    } else if (desc.typeId == QLatin1String("dampedSpring")) {
        const qreal restLength = realValue(params, "restLength");
        made = cpDampedSpringNew(bodyA, bodyB, localA, localB,
                                 restLength > 0.0 ? metres(restLength) : separation, 0.0, 0.0);
        joint->sprungInertia = effectiveMass(bodyA, bodyB);
        joint->hertz = realValue(params, "hertz", 4.0);
        joint->dampingRatio = realValue(params, "dampingRatio", 0.5);

    } else if (desc.typeId == QLatin1String("dampedRotarySpring")) {
        made = cpDampedRotarySpringNew(bodyA, bodyB,
                                       joint->referenceAngle + radians(params, "restAngle"),
                                       0.0, 0.0);
        joint->sprungInertia = effectiveMoment(bodyA, bodyB);
        joint->hertz = realValue(params, "hertz", 4.0);
        joint->dampingRatio = realValue(params, "dampingRatio", 0.5);

    } else if (desc.typeId == QLatin1String("rotaryLimit")) {
        cpFloat lower = joint->referenceAngle + radians(params, "lowerAngle", -45.0);
        cpFloat upper = joint->referenceAngle + radians(params, "upperAngle", 45.0);
        if (lower > upper)
            std::swap(lower, upper);
        made = cpRotaryLimitJointNew(bodyA, bodyB, lower, upper);

    } else if (desc.typeId == QLatin1String("ratchet")) {
        // Chipmunk counts clicks by dividing by this.
        cpFloat click = radians(params, "ratchet", 90.0);
        if (std::abs(click) < 1e-6)
            click = qDegreesToRadians(90.0);
        made = cpRatchetJointNew(bodyA, bodyB, radians(params, "phase"), click);

    } else if (desc.typeId == QLatin1String("gear")) {
        // And the gear by this.
        cpFloat ratio = realValue(params, "ratio", 1.0);
        if (std::abs(ratio) < 1e-6)
            ratio = 1.0;
        // Chipmunk holds b.angle * ratio - a.angle at the phase; this phase is
        // what they stand at now, so nothing turns the moment the run starts.
        joint->gearBase = cpBodyGetAngle(bodyB) * ratio - cpBodyGetAngle(bodyA);
        made = cpGearJointNew(bodyA, bodyB, joint->gearBase + radians(params, "phase"), ratio);

    } else if (desc.typeId == QLatin1String("simpleMotor")) {
        made = cpSimpleMotorNew(bodyA, bodyB, radians(params, "rate", 90.0));

    } else if (mouse) {
        // The body is pinned, at the point being held, to a kinematic body of
        // its own -- which is what moves when the target does. Created on the
        // held point and moved to the target after, or it would grab whatever
        // stands at the target instead.
        joint->hand = cpBodyNewKinematic();
        cpBodySetPosition(joint->hand, toMeters(sceneA));
        cpSpaceAddBody(m_space, joint->hand);
        made = cpPivotJointNew2(joint->hand, bodyB, cpvzero,
                                cpBodyWorldToLocal(bodyB, toMeters(sceneA)));
        const QPointF target(realValue(params, "targetX"), realValue(params, "targetY"));
        cpBodySetPosition(joint->hand, toMeters(target.isNull() ? sceneB : target));

    } else {
        return kInvalidJoint;   // a type this backend doesn't offer
    }

    joint->constraint = made;
    cpConstraintSetUserData(made, joint);
    // Chipmunk lets connected bodies collide unless told otherwise; the scene
    // says which it wants.
    cpConstraintSetCollideBodies(made, desc.collideConnected);
    cpConstraintSetMaxForce(made, unlimited(realValue(params, "maxForce", mouse ? 1.0 : 0.0)));
    cpConstraintSetErrorBias(
        made, qBound(0.0, realValue(params, "errorBias", std::pow(mouse ? 0.85 : 0.9, 60.0)), 1.0));
    const qreal maxBias = realValue(params, "maxBias");
    cpConstraintSetMaxBias(made, unlimited(actsOnAngles(desc.typeId) ? qDegreesToRadians(maxBias)
                                                                     : metres(maxBias)));
    if (isSpring(desc.typeId))
        applySpring(joint);

    // A disabled body is out of the space, and its joints wait with it.
    const auto present = [](cpBody *body) {
        const auto *owner = static_cast<BodyRecord *>(cpBodyGetUserData(body));
        return !owner || owner->inSpace;
    };
    if (present(bodyA) && present(bodyB))
        cpSpaceAddConstraint(m_space, made);

    m_joints.push_back(std::move(owned));
    return static_cast<JointHandle>(m_joints.size() - 1);
}

// --- live changes -----------------------------------------------------------

void ChipmunkEngine::setJointParam(JointHandle handle, const QString &key, const QVariant &value)
{
    JointRecord *joint = jointAt(handle);
    if (!m_space || !joint)
        return;
    cpConstraint *c = joint->constraint;
    cpBody *bodyA = cpConstraintGetBodyA(c);
    cpBody *bodyB = cpConstraintGetBodyB(c);
    const QString &type = joint->typeId;

    const double number = value.toDouble();
    const cpFloat length = metres(number);
    const cpFloat angle = qDegreesToRadians(number);

    // A settled body ignores its joints, so a motor switched on by a rule would
    // do nothing until something else came along.
    wake(bodyA);
    wake(bodyB);

    if (key == QLatin1String("maxForce")) {
        cpConstraintSetMaxForce(c, unlimited(number));
    } else if (key == QLatin1String("errorBias")) {
        cpConstraintSetErrorBias(c, qBound(0.0, number, 1.0));
    } else if (key == QLatin1String("maxBias")) {
        cpConstraintSetMaxBias(c, unlimited(actsOnAngles(type) ? angle : length));
    } else if (key == QLatin1String("collideConnected")) {
        cpConstraintSetCollideBodies(c, value.toBool());
    } else if (key.startsWith(QLatin1String("anchor")) && key.size() == 8) {
        // One coordinate of a point held in a body's frame: out to the scene,
        // amended, and back in.
        const bool endA = key.at(6) == QLatin1Char('A');
        cpVect local;
        if (!localAnchor(c, type, endA, &local))
            return;
        cpBody *body = endA ? bodyA : bodyB;
        cpVect world = cpBodyLocalToWorld(body, local);
        (key.endsWith(QLatin1Char('X')) ? world.x : world.y) = length;
        setLocalAnchor(c, type, endA, cpBodyWorldToLocal(body, world));
    } else if (type == QLatin1String("pin")) {
        if (key == QLatin1String("distance"))
            cpPinJointSetDist(c, std::max(0.0, length));
    } else if (type == QLatin1String("slide")) {
        if (key == QLatin1String("minLength") || key == QLatin1String("maxLength")) {
            cpFloat lower = cpSlideJointGetMin(c);
            cpFloat upper = cpSlideJointGetMax(c);
            (key == QLatin1String("minLength") ? lower : upper) = std::max(0.0, length);
            if (lower > upper)
                std::swap(lower, upper);
            cpSlideJointSetMin(c, lower);
            cpSlideJointSetMax(c, upper);
        }
    } else if (type == QLatin1String("groove")) {
        // The slot is rebuilt from the anchor it was made at, so its two ends
        // and its direction can each be changed without moving the others.
        cpFloat lower = cpvdot(cpvsub(cpGrooveJointGetGrooveA(c), joint->grooveOrigin), joint->grooveAxis);
        cpFloat upper = cpvdot(cpvsub(cpGrooveJointGetGrooveB(c), joint->grooveOrigin), joint->grooveAxis);
        if (key == QLatin1String("lowerTranslation"))
            lower = length;
        else if (key == QLatin1String("upperTranslation"))
            upper = length;
        else if (key == QLatin1String("axisAngle"))
            joint->grooveAxis = cpvunrotate(cpvforangle(angle), cpBodyGetRotation(bodyA));
        else
            return;
        if (lower > upper)
            std::swap(lower, upper);
        upper = std::max(upper, lower + metres(0.01));
        cpGrooveJointSetGrooveA(c, cpvadd(joint->grooveOrigin, cpvmult(joint->grooveAxis, lower)));
        cpGrooveJointSetGrooveB(c, cpvadd(joint->grooveOrigin, cpvmult(joint->grooveAxis, upper)));
    } else if (type == QLatin1String("dampedSpring")
               || type == QLatin1String("dampedRotarySpring")) {
        if (key == QLatin1String("hertz")) {
            joint->hertz = number;
            applySpring(joint);
        } else if (key == QLatin1String("dampingRatio")) {
            joint->dampingRatio = number;
            applySpring(joint);
        } else if (key == QLatin1String("restLength")) {
            cpDampedSpringSetRestLength(c, std::max(0.0, length));
        } else if (key == QLatin1String("restAngle")) {
            cpDampedRotarySpringSetRestAngle(c, joint->referenceAngle + angle);
        }
    } else if (type == QLatin1String("rotaryLimit")) {
        if (key == QLatin1String("lowerAngle") || key == QLatin1String("upperAngle")) {
            cpFloat lower = cpRotaryLimitJointGetMin(c);
            cpFloat upper = cpRotaryLimitJointGetMax(c);
            (key == QLatin1String("lowerAngle") ? lower : upper) = joint->referenceAngle + angle;
            if (lower > upper)
                std::swap(lower, upper);
            cpRotaryLimitJointSetMin(c, lower);
            cpRotaryLimitJointSetMax(c, upper);
        }
    } else if (type == QLatin1String("ratchet")) {
        if (key == QLatin1String("ratchet") && std::abs(angle) > 1e-6)
            cpRatchetJointSetRatchet(c, angle);
        else if (key == QLatin1String("phase"))
            cpRatchetJointSetPhase(c, angle);
    } else if (type == QLatin1String("gear")) {
        if (key == QLatin1String("ratio") && std::abs(number) > 1e-6)
            cpGearJointSetRatio(c, number);
        else if (key == QLatin1String("phase"))
            cpGearJointSetPhase(c, joint->gearBase + angle);
    } else if (type == QLatin1String("simpleMotor")) {
        if (key == QLatin1String("rate"))
            cpSimpleMotorSetRate(c, angle);
    } else if (type == QLatin1String("mouse")) {
        // Moving the target is the whole point of this joint: it leads a body
        // somewhere softly instead of placing it there.
        if (key == QLatin1String("targetX") || key == QLatin1String("targetY")) {
            cpVect target = cpBodyGetPosition(joint->hand);
            (key == QLatin1String("targetX") ? target.x : target.y) = length;
            cpBodySetPosition(joint->hand, target);
        }
    }
}

QVariant ChipmunkEngine::jointValue(JointHandle handle, const QString &key) const
{
    JointRecord *joint = jointAt(handle);
    if (!joint)
        return {};
    const cpConstraint *c = joint->constraint;
    const cpBody *bodyA = cpConstraintGetBodyA(c);
    const cpBody *bodyB = cpConstraintGetBodyB(c);
    const QString &type = joint->typeId;
    const auto degrees = [](cpFloat radians) { return qRadiansToDegrees(radians); };
    const cpFloat relativeAngle = cpBodyGetAngle(bodyB) - cpBodyGetAngle(bodyA);

    // The impulse the constraint applied over the last step, which divided by
    // the step is the force it is carrying -- a torque for the angular ones.
    if (key == QLatin1String("constraintForce"))
        return cpConstraintGetImpulse(const_cast<cpConstraint *>(c)) / m_lastStep;
    if (key == QLatin1String("collideConnected"))
        return bool(cpConstraintGetCollideBodies(c));
    // Where the limit events come from, read as a state: worked out after each
    // step to decide whether the joint has just arrived somewhere.
    if (key == QLatin1String("atLowerLimit"))
        return joint->atLower;
    if (key == QLatin1String("atUpperLimit"))
        return joint->atUpper;
    if (key == QLatin1String("maxForce"))
        return limitedOrZero(cpConstraintGetMaxForce(c));
    if (key == QLatin1String("errorBias"))
        return cpConstraintGetErrorBias(c);
    if (key == QLatin1String("maxBias")) {
        const cpFloat bias = limitedOrZero(cpConstraintGetMaxBias(c));
        return actsOnAngles(type) ? degrees(bias) : bias * m_pixelsPerMeter;
    }
    if (key.startsWith(QLatin1String("anchor")) && key.size() == 8 && hasAnchors(type)) {
        const bool endA = key.at(6) == QLatin1Char('A');
        cpVect local;
        if (!localAnchor(c, type, endA, &local))
            return {};
        const QPointF world = toScene(cpBodyLocalToWorld(endA ? bodyA : bodyB, local));
        return key.endsWith(QLatin1Char('X')) ? world.x() : world.y();
    }
    if (key == QLatin1String("hertz") && isSpring(type))
        return joint->hertz;
    if (key == QLatin1String("dampingRatio") && isSpring(type))
        return joint->dampingRatio;
    // What an angle-based joint measures: how far body B has turned against
    // body A since the run started.
    if (key == QLatin1String("angle") && type != QLatin1String("groove"))
        return degrees(relativeAngle - joint->referenceAngle);
    if (key == QLatin1String("angularSpeed"))
        return degrees(cpBodyGetAngularVelocity(bodyB) - cpBodyGetAngularVelocity(bodyA));
    if (key == QLatin1String("currentLength"))
        return currentLength(c, type) * m_pixelsPerMeter;

    if (type == QLatin1String("pin") && key == QLatin1String("distance"))
        return cpPinJointGetDist(c) * m_pixelsPerMeter;
    if (type == QLatin1String("slide")) {
        if (key == QLatin1String("minLength")) return cpSlideJointGetMin(c) * m_pixelsPerMeter;
        if (key == QLatin1String("maxLength")) return cpSlideJointGetMax(c) * m_pixelsPerMeter;
    }
    if (type == QLatin1String("groove")) {
        const cpVect anchor = cpBodyLocalToWorld(bodyB, cpGrooveJointGetAnchorB(c));
        const cpVect axis = cpvrotate(joint->grooveAxis, cpBodyGetRotation(bodyA));
        // Where the pin is along the slot, measured from where it started.
        if (key == QLatin1String("translation"))
            return cpvdot(cpvsub(cpBodyWorldToLocal(bodyA, anchor), joint->grooveOrigin),
                          joint->grooveAxis) * m_pixelsPerMeter;
        if (key == QLatin1String("speed")) {
            const cpVect relative = cpvsub(cpBodyGetVelocityAtWorldPoint(bodyB, anchor),
                                           cpBodyGetVelocityAtWorldPoint(bodyA, anchor));
            return cpvdot(relative, axis) * m_pixelsPerMeter;
        }
        if (key == QLatin1String("lowerTranslation"))
            return cpvdot(cpvsub(cpGrooveJointGetGrooveA(c), joint->grooveOrigin), joint->grooveAxis)
                   * m_pixelsPerMeter;
        if (key == QLatin1String("upperTranslation"))
            return cpvdot(cpvsub(cpGrooveJointGetGrooveB(c), joint->grooveOrigin), joint->grooveAxis)
                   * m_pixelsPerMeter;
        if (key == QLatin1String("axisAngle"))
            return degrees(cpvtoangle(axis));
    }
    if (type == QLatin1String("dampedSpring")) {
        if (key == QLatin1String("restLength")) return cpDampedSpringGetRestLength(c) * m_pixelsPerMeter;
        if (key == QLatin1String("stiffness"))  return cpDampedSpringGetStiffness(c);
        if (key == QLatin1String("damping"))    return cpDampedSpringGetDamping(c);
    }
    if (type == QLatin1String("dampedRotarySpring")) {
        if (key == QLatin1String("restAngle"))
            return degrees(cpDampedRotarySpringGetRestAngle(c) - joint->referenceAngle);
        if (key == QLatin1String("stiffness")) return cpDampedRotarySpringGetStiffness(c);
        if (key == QLatin1String("damping"))   return cpDampedRotarySpringGetDamping(c);
    }
    if (type == QLatin1String("rotaryLimit")) {
        if (key == QLatin1String("lowerAngle"))
            return degrees(cpRotaryLimitJointGetMin(c) - joint->referenceAngle);
        if (key == QLatin1String("upperAngle"))
            return degrees(cpRotaryLimitJointGetMax(c) - joint->referenceAngle);
    }
    if (type == QLatin1String("ratchet")) {
        if (key == QLatin1String("ratchetAngle")) return degrees(cpRatchetJointGetAngle(c));
        if (key == QLatin1String("ratchet"))      return degrees(cpRatchetJointGetRatchet(c));
        if (key == QLatin1String("phase"))        return degrees(cpRatchetJointGetPhase(c));
    }
    if (type == QLatin1String("gear")) {
        if (key == QLatin1String("ratio")) return cpGearJointGetRatio(c);
        if (key == QLatin1String("phase")) return degrees(cpGearJointGetPhase(c) - joint->gearBase);
    }
    if (type == QLatin1String("simpleMotor") && key == QLatin1String("rate"))
        return degrees(cpSimpleMotorGetRate(c));
    if (type == QLatin1String("mouse")) {
        if (key == QLatin1String("targetX")) return cpBodyGetPosition(joint->hand).x * m_pixelsPerMeter;
        if (key == QLatin1String("targetY")) return cpBodyGetPosition(joint->hand).y * m_pixelsPerMeter;
    }
    return {};
}

void ChipmunkEngine::performJointAction(const QString &id, JointHandle target,
                                        const QVariantMap &params)
{
    Q_UNUSED(params);
    JointRecord *joint = jointAt(target);
    if (!m_space || !joint || id != QLatin1String("breakJoint"))
        return;
    // Asleep against each other as often as not, and a sleeping body would not
    // notice the joint had gone.
    wake(cpConstraintGetBodyA(joint->constraint));
    wake(cpConstraintGetBodyB(joint->constraint));
    if (cpSpaceContainsConstraint(m_space, joint->constraint))
        cpSpaceRemoveConstraint(m_space, joint->constraint);
    joint->broken = true;
}

void ChipmunkEngine::detectLimitEvents()
{
    for (int i = 0; i < static_cast<int>(m_joints.size()); ++i) {
        JointRecord *joint = m_joints[i].get();
        if (joint->broken || !cpSpaceContainsConstraint(m_space, joint->constraint))
            continue;
        const cpConstraint *c = joint->constraint;

        // How far the joint has travelled and the bounds it travels between.
        // A soft constraint settles a hair inside or outside its bound, so a
        // slack is allowed: half a scene unit, or about a third of a degree.
        cpFloat value = 0.0, lower = 0.0, upper = 0.0, slack = 0.0;
        if (joint->typeId == QLatin1String("slide")) {
            value = currentLength(c, joint->typeId);
            lower = cpSlideJointGetMin(c);
            upper = cpSlideJointGetMax(c);
            slack = metres(0.5);
        } else if (joint->typeId == QLatin1String("groove")) {
            const cpBody *bodyA = cpConstraintGetBodyA(c);
            const cpVect anchor = cpBodyLocalToWorld(cpConstraintGetBodyB(c), cpGrooveJointGetAnchorB(c));
            const cpVect local = cpBodyWorldToLocal(bodyA, anchor);
            value = cpvdot(cpvsub(local, joint->grooveOrigin), joint->grooveAxis);
            lower = cpvdot(cpvsub(cpGrooveJointGetGrooveA(c), joint->grooveOrigin), joint->grooveAxis);
            upper = cpvdot(cpvsub(cpGrooveJointGetGrooveB(c), joint->grooveOrigin), joint->grooveAxis);
            slack = metres(0.5);
        } else if (joint->typeId == QLatin1String("rotaryLimit")) {
            value = cpBodyGetAngle(cpConstraintGetBodyB(c)) - cpBodyGetAngle(cpConstraintGetBodyA(c));
            lower = cpRotaryLimitJointGetMin(c);
            upper = cpRotaryLimitJointGetMax(c);
            slack = 0.005;
        } else {
            continue;
        }

        const bool atLower = value <= lower + slack;
        const bool atUpper = value >= upper - slack;

        // Edge-triggered, and the first sample only establishes the baseline:
        // a joint made already against a limit has not arrived at it.
        if (!joint->sampled) {
            joint->sampled = true;
            joint->atLower = atLower;
            joint->atUpper = atUpper;
            continue;
        }

        const auto raiseLimit = [this, i](const QString &id) {
            EngineEvent event;
            event.joint = i;
            event.eventId = id;
            m_pendingEvents.append(event);
        };
        if (atLower && !joint->atLower) {
            raiseLimit(QStringLiteral("limitLower"));
            raiseLimit(QStringLiteral("limitEither"));
        }
        if (atUpper && !joint->atUpper) {
            raiseLimit(QStringLiteral("limitUpper"));
            raiseLimit(QStringLiteral("limitEither"));
        }
        joint->atLower = atLower;
        joint->atUpper = atUpper;
    }
}

} // namespace physics
