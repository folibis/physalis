#include "ChipmunkEngine.h"

#include <chipmunk/chipmunk_structs.h>
#include <chipmunk/chipmunk_unsafe.h>

#include <QSet>
#include <QtMath>
#include <algorithm>
#include <cmath>

// What a running world can be asked, and told, about its bodies, shapes and
// itself. Everything is in the units the editor shows: scene units for lengths
// and speeds, degrees for angles -- the same as the Box2D plugin answers in, so
// a rule means the same whichever engine runs it.

namespace physics {

namespace {

// The catalogue lists body types in Box2D's order, which the editor stores as
// the index. Chipmunk numbers them the other way round.
int choiceOf(cpBodyType type)
{
    switch (type) {
    case CP_BODY_TYPE_STATIC:    return 0;
    case CP_BODY_TYPE_KINEMATIC: return 1;
    case CP_BODY_TYPE_DYNAMIC:   return 2;
    }
    return 0;
}

qreal finiteOrZero(cpFloat value)
{
    return std::isfinite(value) ? value : 0.0;
}

bool isCircle(const cpShape *shape)
{
    return shape->klass->type == CP_CIRCLE_SHAPE;
}

void wake(cpBody *body)
{
    if (cpBodyGetSpace(body) && cpBodyGetType(body) == CP_BODY_TYPE_DYNAMIC)
        cpBodyActivate(body);
}

} // namespace

QVariant ChipmunkEngine::bodyValue(BodyHandle handle, const QString &key) const
{
    const BodyRecord *record = bodyAt(handle);
    if (!record)
        return {};
    const cpBody *body = record->body;
    const qreal ppm = m_pixelsPerMeter;

    if (key == QLatin1String("positionX"))       return cpBodyGetPosition(body).x * ppm;
    if (key == QLatin1String("positionY"))       return cpBodyGetPosition(body).y * ppm;
    if (key == QLatin1String("angle"))           return qRadiansToDegrees(cpBodyGetAngle(body));
    if (key == QLatin1String("velocityX"))       return cpBodyGetVelocity(body).x * ppm;
    if (key == QLatin1String("velocityY"))       return cpBodyGetVelocity(body).y * ppm;
    if (key == QLatin1String("speed"))           return cpvlength(cpBodyGetVelocity(body)) * ppm;
    if (key == QLatin1String("angularVelocity"))
        return qRadiansToDegrees(cpBodyGetAngularVelocity(body));
    if (key == QLatin1String("isAwake"))         return !cpBodyIsSleeping(body);
    if (key == QLatin1String("isEnabled"))       return record->inSpace;
    if (key == QLatin1String("gravityScale"))    return record->gravityScale;
    if (key == QLatin1String("linearDamping"))   return record->linearDamping;
    if (key == QLatin1String("angularDamping"))  return record->angularDamping;
    if (key == QLatin1String("fixedRotation"))   return record->fixedRotation;
    if (key == QLatin1String("enableSleep"))     return record->enableSleep;
    if (key == QLatin1String("bodyType"))
        return choiceOf(cpBodyGetType(const_cast<cpBody *>(body)));

    // Infinite for anything that is not dynamic, and for a moment fixed
    // rotation has set -- which a property table cannot show, so zero.
    if (key == QLatin1String("mass"))              return finiteOrZero(cpBodyGetMass(body));
    if (key == QLatin1String("rotationalInertia")) return finiteOrZero(cpBodyGetMoment(body));
    if (key == QLatin1String("localCenterOfMassX"))
        return cpBodyGetCenterOfGravity(body).x * ppm;
    if (key == QLatin1String("localCenterOfMassY"))
        return cpBodyGetCenterOfGravity(body).y * ppm;
    if (key == QLatin1String("enableContactEvents") || key == QLatin1String("enableHitEvents")) {
        // Whatever the body's shapes were last told, which is what it was set
        // to: Chipmunk keeps these per shape and the body switch is ours.
        for (const auto &shape : m_shapes) {
            if (shape->owner != record)
                continue;
            return key == QLatin1String("enableContactEvents") ? shape->contactEvents
                                                               : shape->hitEvents;
        }
        return false;
    }
    if (key == QLatin1String("jointCount")) {
        int joints = 0;
        cpBodyEachConstraint(
            const_cast<cpBody *>(body),
            [](cpBody *, cpConstraint *, void *count) { ++*static_cast<int *>(count); }, &joints);
        return joints;
    }
    if (key == QLatin1String("contactCount")) {
        int contacts = 0;
        cpBodyEachArbiter(
            const_cast<cpBody *>(body),
            [](cpBody *, cpArbiter *arbiter, void *count) {
                CP_ARBITER_GET_SHAPES(arbiter, a, b);
                // Touching, and actually colliding: an overlap with a sensor
                // is not a contact.
                if (cpArbiterGetCount(arbiter) > 0 && !cpShapeGetSensor(a) && !cpShapeGetSensor(b))
                    ++*static_cast<int *>(count);
            },
            &contacts);
        return contacts;
    }
    if (key.startsWith(QLatin1String("bounds"))) {
        // Chipmunk keeps a box per shape rather than per body, so this is all
        // of them together.
        if (record->shapes.empty())
            return 0.0;
        cpBB box = cpShapeGetBB(record->shapes.front());
        for (cpShape *shape : record->shapes)
            box = cpBBMerge(box, cpShapeGetBB(shape));
        if (key == QLatin1String("boundsMinX")) return box.l * ppm;
        if (key == QLatin1String("boundsMinY")) return box.b * ppm;
        if (key == QLatin1String("boundsMaxX")) return box.r * ppm;
        if (key == QLatin1String("boundsMaxY")) return box.t * ppm;
    }
    if (key == QLatin1String("centerOfMassX") || key == QLatin1String("centerOfMassY")) {
        const cpVect centre = cpBodyLocalToWorld(body, cpBodyGetCenterOfGravity(body));
        return (key == QLatin1String("centerOfMassX") ? centre.x : centre.y) * ppm;
    }
    // cpBodyKineticEnergy is m·v² + I·ω², without the half; and in millijoules.
    if (key == QLatin1String("kineticEnergy"))     return finiteOrZero(500.0 * cpBodyKineticEnergy(body));
    return {};
}

void ChipmunkEngine::setBodyParam(BodyHandle handle, const QString &key, const QVariant &value)
{
    BodyRecord *record = bodyAt(handle);
    if (!m_space || !record)
        return;
    cpBody *body = record->body;
    const bool dynamic = cpBodyGetType(body) == CP_BODY_TYPE_DYNAMIC;
    const double number = value.toDouble();
    const bool flag = value.toBool();

    // Placing a body: it arrives without travelling. Chipmunk has to be told
    // its shapes moved, or it goes on colliding them where they were.
    if (key == QLatin1String("positionX") || key == QLatin1String("positionY")
        || key == QLatin1String("angle")) {
        if (key == QLatin1String("angle")) {
            cpBodySetAngle(body, qDegreesToRadians(number));
        } else {
            cpVect position = cpBodyGetPosition(body);
            (key == QLatin1String("positionX") ? position.x : position.y) = metres(number);
            cpBodySetPosition(body, position);
        }
        if (record->inSpace)
            cpSpaceReindexShapesForBody(m_space, body);
        return;
    }

    // Box2D's b2Body_SetTargetTransform: the velocity that arrives there by the
    // end of the step, so the body travels and shoves what is in the way. The
    // rest of the transform is where it already is, so it asks for no other
    // motion -- and a move slower than the sleep speed is ignored, as there.
    if (key == QLatin1String("targetX") || key == QLatin1String("targetY")
        || key == QLatin1String("targetAngle")) {
        if (cpBodyGetType(body) == CP_BODY_TYPE_STATIC || m_lastStep <= 0.0)
            return;
        const cpVect position = cpBodyGetPosition(body);
        cpVect target = position;
        cpFloat angle = cpBodyGetAngle(body);
        if (key == QLatin1String("targetX"))
            target.x = metres(number);
        else if (key == QLatin1String("targetY"))
            target.y = metres(number);
        else
            angle = qDegreesToRadians(number);
        const cpVect velocity = cpvmult(cpvsub(target, position), 1.0 / m_lastStep);
        const cpFloat spin = (angle - cpBodyGetAngle(body)) / m_lastStep;
        if (cpvlength(velocity) < cpSpaceGetIdleSpeedThreshold(m_space) && spin == 0.0)
            return;
        cpBodySetVelocity(body, velocity);
        cpBodySetAngularVelocity(body, spin);
        return;
    }

    // A push, rather than a setting, through the centre of mass.
    if (key == QLatin1String("impulseX") || key == QLatin1String("impulseY")
        || key == QLatin1String("forceX") || key == QLatin1String("forceY")) {
        const cpFloat amount = metres(number);
        const cpVect along = key.endsWith(QLatin1Char('X')) ? cpv(amount, 0.0) : cpv(0.0, amount);
        const cpVect centre = cpBodyLocalToWorld(body, cpBodyGetCenterOfGravity(body));
        // A force lasts one step: Chipmunk clears it after integrating.
        if (key.startsWith(QLatin1String("impulse")))
            cpBodyApplyImpulseAtWorldPoint(body, along, centre);
        else
            cpBodyApplyForceAtWorldPoint(body, along, centre);
        return;
    }

    // A torque is a force times a distance, both quoted in scene units, so it
    // comes down by the scale twice -- as it does in the Box2D plugin.
    if (key == QLatin1String("torque") || key == QLatin1String("angularImpulse")) {
        const cpFloat amount = number / (m_pixelsPerMeter * m_pixelsPerMeter);
        if (key == QLatin1String("torque")) {
            cpBodySetTorque(body, cpBodyGetTorque(body) + amount);
        } else if (dynamic && std::isfinite(cpBodyGetMoment(body))) {
            // Chipmunk has no angular impulse of its own; this is what one does.
            cpBodySetAngularVelocity(body, cpBodyGetAngularVelocity(body)
                                               + amount / cpBodyGetMoment(body));
        }
        return;
    }

    if (key == QLatin1String("velocityX") || key == QLatin1String("velocityY")) {
        if (cpBodyGetType(body) == CP_BODY_TYPE_STATIC)
            return;
        cpVect velocity = cpBodyGetVelocity(body);
        (key == QLatin1String("velocityX") ? velocity.x : velocity.y) = metres(number);
        cpBodySetVelocity(body, velocity);
        return;
    }
    if (key == QLatin1String("angularVelocity")) {
        if (cpBodyGetType(body) != CP_BODY_TYPE_STATIC)
            cpBodySetAngularVelocity(body, qDegreesToRadians(number));
        return;
    }

    // Normally worked out from the shapes. Chipmunk asserts on a mass that is
    // not positive and finite, or on any mass for a body that is not dynamic.
    if (key == QLatin1String("mass")) {
        if (dynamic && number > 0.0 && std::isfinite(number))
            cpBodySetMass(body, number);
        return;
    }
    if (key == QLatin1String("rotationalInertia")) {
        if (dynamic && number > 0.0 && !record->fixedRotation)
            cpBodySetMoment(body, number);
        return;
    }

    if (key == QLatin1String("bodyType")) {
        static const cpBodyType kTypes[] = { CP_BODY_TYPE_STATIC, CP_BODY_TYPE_KINEMATIC,
                                             CP_BODY_TYPE_DYNAMIC };
        const int index = value.toInt();
        if (index < 0 || index > 2)
            return;
        cpBodySetType(body, kTypes[index]);
        // Made dynamic out of shapes with no density, it would have nothing to
        // divide a force by. Box2D would leave it massless and moving only as
        // told, which is what kinematic is.
        if (kTypes[index] == CP_BODY_TYPE_DYNAMIC && !(cpBodyGetMass(body) > 0.0))
            cpBodySetType(body, CP_BODY_TYPE_KINEMATIC);
        applyFixedRotation(record);
        if (record->inSpace)
            cpSpaceReindexShapesForBody(m_space, body);
        return;
    }

    // One switch for every shape the body has: Chipmunk reports contacts per
    // shape, so the body-wide setting is applied to each of them.
    if (key == QLatin1String("enableContactEvents") || key == QLatin1String("enableHitEvents")) {
        for (const auto &shape : m_shapes) {
            if (shape->owner != record)
                continue;
            if (key == QLatin1String("enableContactEvents"))
                shape->contactEvents = flag;
            else
                shape->hitEvents = flag;
        }
        return;
    }

    if (key == QLatin1String("gravityScale")) {
        record->gravityScale = number;
        wake(body);
    } else if (key == QLatin1String("linearDamping")) {
        record->linearDamping = std::max(0.0, number);
    } else if (key == QLatin1String("angularDamping")) {
        record->angularDamping = std::max(0.0, number);
    } else if (key == QLatin1String("fixedRotation")) {
        record->fixedRotation = flag;
        applyFixedRotation(record);
    } else if (key == QLatin1String("enableSleep")) {
        record->enableSleep = flag;
        if (!flag)
            wake(body);
    } else if (key == QLatin1String("isAwake")) {
        if (flag)
            wake(body);
        // Chipmunk asserts on putting to sleep anything it could not have put
        // to sleep itself.
        else if (dynamic && record->inSpace && m_enableSleep && !cpBodyIsSleeping(body))
            cpBodySleep(body);
    } else if (key == QLatin1String("isEnabled")) {
        setInSpace(record, flag);
    }
}

QVariant ChipmunkEngine::shapeValue(const QString &name, const QString &key) const
{
    const QVector<ShapeRecord *> parts = shapesNamed(name);
    if (parts.isEmpty())
        return {};
    const ShapeRecord *first = parts.first();
    cpShape *shape = first->shape;

    if (key == QLatin1String("density"))      return cpShapeGetDensity(shape);
    if (key == QLatin1String("friction"))     return cpShapeGetFriction(shape);
    if (key == QLatin1String("restitution"))  return cpShapeGetElasticity(shape);
    if (key == QLatin1String("tangentSpeed")) return first->tangentSpeed * m_pixelsPerMeter;
    if (key == QLatin1String("isSensor"))     return bool(cpShapeGetSensor(shape));
    if (key == QLatin1String("enableContactEvents"))  return first->contactEvents;
    if (key == QLatin1String("enableHitEvents"))      return first->hitEvents;
    if (key == QLatin1String("enableSensorEvents"))   return first->sensorEvents;
    if (key == QLatin1String("enablePreSolveEvents")) return first->preSolveEvents;
    if (key == QLatin1String("categoryBits")) return static_cast<double>(first->filter.categoryBits);
    if (key == QLatin1String("maskBits"))     return static_cast<double>(first->filter.maskBits);
    if (key == QLatin1String("groupIndex"))   return first->filter.groupIndex;

    if (key == QLatin1String("rotationalInertia")) return cpShapeGetMoment(shape);
    if (key == QLatin1String("centerOfMassX") || key == QLatin1String("centerOfMassY")) {
        const cpVect centre = cpBodyLocalToWorld(cpShapeGetBody(shape),
                                                 cpShapeGetCenterOfGravity(shape));
        return (key == QLatin1String("centerOfMassX") ? centre.x : centre.y) * m_pixelsPerMeter;
    }
    if (key.startsWith(QLatin1String("bounds"))) {
        // Every piece a part became, together: an outline is a segment per
        // edge, and all of them are this one shape as far as the editor knows.
        cpBB box = cpShapeGetBB(shape);
        for (const ShapeRecord *piece : parts)
            box = cpBBMerge(box, cpShapeGetBB(piece->shape));
        if (key == QLatin1String("boundsMinX")) return box.l * m_pixelsPerMeter;
        if (key == QLatin1String("boundsMinY")) return box.b * m_pixelsPerMeter;
        if (key == QLatin1String("boundsMaxX")) return box.r * m_pixelsPerMeter;
        if (key == QLatin1String("boundsMaxY")) return box.t * m_pixelsPerMeter;
    }
    if (key == QLatin1String("contactCount")) {
        // Chipmunk keeps contacts on the body, so its arbiters are asked which
        // of them name one of this part's pieces.
        struct Counting {
            const QVector<ShapeRecord *> *parts;
            int found = 0;
        } counting { &parts, 0 };
        cpBodyEachArbiter(
            cpShapeGetBody(shape),
            [](cpBody *, cpArbiter *arbiter, void *data) {
                auto *counter = static_cast<Counting *>(data);
                CP_ARBITER_GET_SHAPES(arbiter, a, b);
                if (cpArbiterGetCount(arbiter) == 0 || cpShapeGetSensor(a) || cpShapeGetSensor(b))
                    return;
                for (const ShapeRecord *piece : *counter->parts) {
                    if (piece->shape == a || piece->shape == b) {
                        ++counter->found;
                        return;
                    }
                }
            },
            &counting);
        return counting.found;
    }
    if (key == QLatin1String("radius")) {
        if (!isCircle(shape))
            return {};
        return cpCircleShapeGetRadius(shape) * m_pixelsPerMeter;
    }

    // A part that became several Chipmunk shapes answers for all of them.
    if (key == QLatin1String("mass") || key == QLatin1String("area")
        || key == QLatin1String("sensorOverlapCount")) {
        double total = 0.0;
        for (const ShapeRecord *part : parts) {
            if (key == QLatin1String("mass"))
                total += cpShapeGetMass(part->shape);
            else if (key == QLatin1String("area"))
                total += cpShapeGetArea(part->shape) * m_pixelsPerMeter * m_pixelsPerMeter;
            else
                total += part->overlaps;
        }
        return key == QLatin1String("sensorOverlapCount") ? QVariant(int(total)) : QVariant(total);
    }

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

void ChipmunkEngine::setShapeParam(const QString &name, const QString &key, const QVariant &value)
{
    const QVector<ShapeRecord *> parts = shapesNamed(name);
    if (!m_space || parts.isEmpty())
        return;
    const double number = value.toDouble();
    const bool flag = value.toBool();

    for (ShapeRecord *part : parts) {
        cpShape *shape = part->shape;
        // A settled body does not notice what its shapes became -- a conveyor
        // switched on would carry nothing until something disturbed it.
        wake(part->owner->body);

        if (key == QLatin1String("density")) {
            cpShapeSetDensity(shape, std::max(0.0, number));
        } else if (key == QLatin1String("friction")) {
            cpShapeSetFriction(shape, std::max(0.0, number));
        } else if (key == QLatin1String("restitution")) {
            cpShapeSetElasticity(shape, std::max(0.0, number));
        } else if (key == QLatin1String("tangentSpeed")) {
            part->tangentSpeed = metres(number);
        } else if (key == QLatin1String("radius")) {
            // Replaced where it stands; what is drawn does not follow.
            if (isCircle(shape)) {
                cpCircleShapeSetRadius(shape, metres(std::max(0.0, number)));
                if (cpShapeGetSpace(shape))
                    cpSpaceReindexShape(m_space, shape);
            }
        } else if (key == QLatin1String("isSensor")) {
            // Unlike Box2D, Chipmunk lets a shape become a sensor, or stop being
            // one, while it runs.
            cpShapeSetSensor(shape, flag);
        } else if (key == QLatin1String("enableContactEvents")) {
            part->contactEvents = flag;
        } else if (key == QLatin1String("enableHitEvents")) {
            part->hitEvents = flag;
        } else if (key == QLatin1String("enableSensorEvents")) {
            part->sensorEvents = flag;
        } else if (key == QLatin1String("enablePreSolveEvents")) {
            part->preSolveEvents = flag;
        } else if (key == QLatin1String("categoryBits")) {
            part->filter.categoryBits = static_cast<quint64>(std::max(0.0, number));
        } else if (key == QLatin1String("maskBits")) {
            part->filter.maskBits = static_cast<quint64>(std::max(0.0, number));
        } else if (key == QLatin1String("groupIndex")) {
            part->filter.groupIndex = value.toInt();
        }
    }

    // Density changes the body's mass, which Chipmunk works out again from
    // scratch -- losing a fixed rotation's infinite moment, and dividing by
    // zero if nothing is left with any density.
    if (key == QLatin1String("density")) {
        BodyRecord *owner = parts.first()->owner;
        if (cpBodyGetType(owner->body) == CP_BODY_TYPE_DYNAMIC && !(cpBodyGetMass(owner->body) > 0.0))
            cpBodySetType(owner->body, CP_BODY_TYPE_KINEMATIC);
        applyFixedRotation(owner);
    }
}

QVariant ChipmunkEngine::worldValue(const QString &key) const
{
    if (!m_space)
        return {};

    // Speeds and accelerations went in scaled so that the scene's scale leaves
    // the pace on screen alone; they come back out the same way.
    const auto unscaled = [this](cpFloat value) { return value / m_motionScale; };

    if (key == QLatin1String("gravityX"))  return unscaled(cpSpaceGetGravity(m_space).x);
    if (key == QLatin1String("gravityY"))  return unscaled(cpSpaceGetGravity(m_space).y);
    if (key == QLatin1String("restitutionThreshold")) return unscaled(m_restitutionThreshold);
    if (key == QLatin1String("hitEventThreshold"))    return unscaled(m_hitEventThreshold);
    if (key == QLatin1String("maximumLinearSpeed"))   return unscaled(m_maximumSpeed);
    if (key == QLatin1String("idleSpeedThreshold"))
        return unscaled(cpSpaceGetIdleSpeedThreshold(m_space));
    if (key == QLatin1String("enableSleep"))          return m_enableSleep;
    if (key == QLatin1String("sleepTimeThreshold"))   return m_sleepTime;
    if (key == QLatin1String("iterations"))           return cpSpaceGetIterations(m_space);
    if (key == QLatin1String("damping"))              return cpSpaceGetDamping(m_space);
    if (key == QLatin1String("collisionSlop"))
        return cpSpaceGetCollisionSlop(m_space) * m_pixelsPerMeter;
    if (key == QLatin1String("collisionBias"))        return cpSpaceGetCollisionBias(m_space);
    if (key == QLatin1String("collisionPersistence"))
        return static_cast<int>(cpSpaceGetCollisionPersistence(m_space));

    if (key == QLatin1String("bodyCount") || key == QLatin1String("awakeBodyCount")
        || key == QLatin1String("contactCount")) {
        int bodies = 0;
        int awake = 0;
        QSet<cpArbiter *> contacts;
        for (const auto &record : m_bodies) {
            if (record->removed || !record->inSpace)
                continue;
            ++bodies;
            if (cpBodyGetType(record->body) == CP_BODY_TYPE_DYNAMIC && !cpBodyIsSleeping(record->body))
                ++awake;
            if (key == QLatin1String("contactCount")) {
                cpBodyEachArbiter(
                    record->body,
                    [](cpBody *, cpArbiter *arbiter, void *data) {
                        CP_ARBITER_GET_SHAPES(arbiter, a, b);
                        // Touching, and actually colliding: an overlap with a
                        // sensor is not a contact.
                        if (cpArbiterGetCount(arbiter) > 0 && !cpShapeGetSensor(a)
                            && !cpShapeGetSensor(b))
                            static_cast<QSet<cpArbiter *> *>(data)->insert(arbiter);
                    },
                    &contacts);
            }
        }
        if (key == QLatin1String("bodyCount"))
            return bodies;
        if (key == QLatin1String("awakeBodyCount"))
            return awake;
        return static_cast<int>(contacts.size());
    }
    if (key == QLatin1String("shapeCount")) {
        int shapes = 0;
        for (const auto &shape : m_shapes)
            shapes += !shape->owner->removed && shape->owner->inSpace;
        return shapes;
    }
    if (key == QLatin1String("jointCount")) {
        int joints = 0;
        for (const auto &joint : m_joints)
            joints += !joint->broken && cpSpaceContainsConstraint(m_space, joint->constraint);
        return joints;
    }
    return {};
}

void ChipmunkEngine::setWorldParam(const QString &key, const QVariant &value)
{
    if (!m_space)
        return;
    const double number = value.toDouble();
    const cpFloat scaled = number * m_motionScale;

    if (key == QLatin1String("gravityX") || key == QLatin1String("gravityY")) {
        cpVect gravity = cpSpaceGetGravity(m_space);
        (key == QLatin1String("gravityX") ? gravity.x : gravity.y) = scaled;
        cpSpaceSetGravity(m_space, gravity);
    } else if (key == QLatin1String("restitutionThreshold")) {
        m_restitutionThreshold = std::max(0.0, scaled);
    } else if (key == QLatin1String("hitEventThreshold")) {
        m_hitEventThreshold = std::max(0.0, scaled);
    } else if (key == QLatin1String("maximumLinearSpeed")) {
        m_maximumSpeed = std::max(0.0, scaled);
    } else if (key == QLatin1String("idleSpeedThreshold")) {
        cpSpaceSetIdleSpeedThreshold(m_space, std::max(0.0, scaled));
    } else if (key == QLatin1String("enableSleep") || key == QLatin1String("sleepTimeThreshold")) {
        if (key == QLatin1String("enableSleep"))
            m_enableSleep = value.toBool();
        else
            m_sleepTime = std::max(0.0, number);
        cpSpaceSetSleepTimeThreshold(m_space, m_enableSleep ? m_sleepTime : INFINITY);
        // Turned off, anything already asleep would stay that way.
        if (!m_enableSleep) {
            for (const auto &record : m_bodies) {
                if (!record->removed && record->inSpace)
                    wake(record->body);
            }
        }
    } else if (key == QLatin1String("iterations")) {
        cpSpaceSetIterations(m_space, std::max(1, value.toInt()));
    } else if (key == QLatin1String("damping")) {
        cpSpaceSetDamping(m_space, std::max(0.0, number));
    } else if (key == QLatin1String("collisionSlop")) {
        cpSpaceSetCollisionSlop(m_space, metres(std::max(0.0, number)));
    } else if (key == QLatin1String("collisionBias")) {
        cpSpaceSetCollisionBias(m_space, qBound(0.0, number, 1.0));
    } else if (key == QLatin1String("collisionPersistence")) {
        cpSpaceSetCollisionPersistence(m_space, static_cast<cpTimestamp>(std::max(0, value.toInt())));
    }
}

} // namespace physics
