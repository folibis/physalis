#include "ChipmunkEngine.h"

#include <chipmunk/chipmunk_structs.h>

#include <QLineF>
#include <algorithm>
#include <cmath>
#include <limits>

// Contacts, sensors, rays and blasts: everything that comes out of the
// collision callbacks or asks the space what is where.

namespace physics {

namespace {

// What an arbiter reported when it began, kept in its user data so the
// separation reports the same -- Chipmunk calls separate for every pair it
// began, including those a callback turned away.
enum ArbiterFlags : uintptr_t {
    kContactReported = 1,
    kSensorReported = 2,
    kSensorIsSecond = 4,
};

ChipmunkEngine::ShapeRecord *recordOf(const cpShape *shape)
{
    return static_cast<ChipmunkEngine::ShapeRecord *>(cpShapeGetUserData(shape));
}

// Box2D's rule, kept so that a scene's collision groups mean the same in either
// engine: a shared non-zero group decides outright, positive together and
// negative apart; otherwise each shape's mask has to accept the other.
bool shouldCollide(const Filter &a, const Filter &b)
{
    if (a.groupIndex == b.groupIndex && a.groupIndex != 0)
        return a.groupIndex > 0;
    return (a.maskBits & b.categoryBits) != 0 && (a.categoryBits & b.maskBits) != 0;
}

bool isDynamic(cpBody *body)
{
    return cpBodyGetType(body) == CP_BODY_TYPE_DYNAMIC;
}

// How fast the two sides are closing along the contact normal, in solver
// units. Box2D's thresholds are measured against the same.
cpFloat approachSpeed(const cpArbiter *arbiter)
{
    const cpContactPointSet contacts = cpArbiterGetContactPointSet(arbiter);
    if (contacts.count == 0)
        return 0.0;
    CP_ARBITER_GET_BODIES(arbiter, bodyA, bodyB);
    const cpVect point = contacts.points[0].pointA;
    const cpVect relative = cpvsub(cpBodyGetVelocityAtWorldPoint(bodyB, point),
                                   cpBodyGetVelocityAtWorldPoint(bodyA, point));
    return std::abs(cpvdot(relative, contacts.normal));
}

// The width a shape presents across `direction`, which is what Box2D scales a
// blast's push by: a big object takes a bigger shove than a small one standing
// the same distance away.
cpFloat projectedWidth(const cpShape *shape, cpVect direction)
{
    const cpVect across = cpvperp(direction);
    const cpBody *body = cpShapeGetBody(shape);
    switch (shape->klass->type) {
    case CP_CIRCLE_SHAPE:
        return 2.0 * cpCircleShapeGetRadius(shape);
    case CP_SEGMENT_SHAPE: {
        const cpVect a = cpBodyLocalToWorld(body, cpSegmentShapeGetA(shape));
        const cpVect b = cpBodyLocalToWorld(body, cpSegmentShapeGetB(shape));
        return std::abs(cpvdot(cpvsub(b, a), across)) + 2.0 * cpSegmentShapeGetRadius(shape);
    }
    case CP_POLY_SHAPE: {
        cpFloat lower = std::numeric_limits<cpFloat>::max();
        cpFloat upper = -lower;
        for (int i = 0; i < cpPolyShapeGetCount(shape); ++i) {
            const cpFloat along =
                cpvdot(cpBodyLocalToWorld(body, cpPolyShapeGetVert(shape, i)), across);
            lower = std::min(lower, along);
            upper = std::max(upper, along);
        }
        return (upper - lower) + 2.0 * cpPolyShapeGetRadius(shape);
    }
    default:
        return 0.0;
    }
}

} // namespace

void ChipmunkEngine::raise(ShapeRecord *subject, ShapeRecord *other, const QString &id)
{
    // A body a rule removed takes its contacts with it, and Box2D does not
    // report those as ended; neither does this.
    if (subject->owner->removed || other->owner->removed)
        return;
    // Named fields: EngineEvent is not safe to build positionally.
    EngineEvent event;
    event.body = subject->owner->handle;
    event.otherBody = other->owner->handle;
    event.subjectShape = subject->name;
    event.otherShape = other->name;
    event.eventId = id;
    m_pendingEvents.append(event);
}

void ChipmunkEngine::raiseBoth(ShapeRecord *a, ShapeRecord *b, const QString &id)
{
    // Once each way round, so either side can be the one a rule watches.
    raise(a, b, id);
    raise(b, a, id);
}

cpBool ChipmunkEngine::onBegin(cpArbiter *arbiter)
{
    CP_ARBITER_GET_SHAPES(arbiter, a, b);
    ShapeRecord *first = recordOf(a);
    ShapeRecord *second = recordOf(b);
    if (!first || !second)
        return cpTrue;

    // Turned away for as long as the two stay overlapped.
    if (!shouldCollide(first->filter, second->filter))
        return cpFalse;

    const bool sensorA = cpShapeGetSensor(a);
    const bool sensorB = cpShapeGetSensor(b);
    // Box2D's sensors do not notice one another.
    if (sensorA && sensorB)
        return cpFalse;

    uintptr_t flags = 0;
    if (sensorA || sensorB) {
        ShapeRecord *sensor = sensorA ? first : second;
        ShapeRecord *visitor = sensorA ? second : first;
        // Scenery wandering into scenery is not a visit.
        if (cpBodyGetType(cpShapeGetBody(visitor->shape)) == CP_BODY_TYPE_STATIC)
            return cpFalse;
        // As in Box2D, it is whatever enters that has to ask to be noticed.
        if (visitor->sensorEvents) {
            ++sensor->overlaps;
            raiseBoth(sensor, visitor, QStringLiteral("sensorBegin"));
            flags = kSensorReported | (sensorA ? 0 : kSensorIsSecond);
        }
        cpArbiterSetUserData(arbiter, reinterpret_cast<cpDataPointer>(flags));
        return cpTrue;
    }

    // Chipmunk reports a kinematic body meeting scenery, and another kinematic
    // body, without doing anything about it. Box2D does not make those
    // contacts at all, and a rule should not fire on one.
    if (!isDynamic(cpShapeGetBody(a)) && !isDynamic(cpShapeGetBody(b)))
        return cpFalse;

    if (first->contactEvents || second->contactEvents) {
        raiseBoth(first, second, QStringLiteral("contactBegin"));
        flags |= kContactReported;
    }

    // A hit is a beginning that arrived hard enough. It comes with numbers --
    // how fast, and where -- and an event carries only names, so they are kept
    // per shape for shapeValue to answer.
    if (first->hitEvents || second->hitEvents) {
        const cpFloat speed = approachSpeed(arbiter);
        const cpContactPointSet contacts = cpArbiterGetContactPointSet(arbiter);
        if (speed >= m_hitEventThreshold && contacts.count > 0) {
            HitRecord hit;
            hit.speed = speed * m_pixelsPerMeter;
            hit.point = toScene(contacts.points[0].pointA);
            hit.normal = QPointF(contacts.normal.x, contacts.normal.y);
            if (!first->name.isEmpty())
                m_lastHit.insert(first->name, hit);
            // The normal points from the first shape to the second, so the
            // second sees it the other way.
            hit.normal = -hit.normal;
            if (!second->name.isEmpty())
                m_lastHit.insert(second->name, hit);
            raiseBoth(first, second, QStringLiteral("contactHit"));
        }
    }

    cpArbiterSetUserData(arbiter, reinterpret_cast<cpDataPointer>(flags));
    return cpTrue;
}

cpBool ChipmunkEngine::onPreSolve(cpArbiter *arbiter)
{
    CP_ARBITER_GET_SHAPES(arbiter, a, b);
    ShapeRecord *first = recordOf(a);
    ShapeRecord *second = recordOf(b);
    if (!first || !second)
        return cpTrue;

    // Asked every step as well as at the start, so a filter a rule changes
    // takes effect on shapes already touching.
    if (!shouldCollide(first->filter, second->filter))
        return cpFalse;
    if (cpShapeGetSensor(a) || cpShapeGetSensor(b))
        return cpTrue;

    // Chipmunk multiplies both surfaces' friction and bounce; Box2D takes the
    // geometric mean of friction and the larger bounce. Box2D's, so a bouncy
    // ball still bounces off a dead floor, as the property tooltips promise.
    cpArbiterSetFriction(arbiter, std::sqrt(cpShapeGetFriction(a) * cpShapeGetFriction(b)));
    cpFloat restitution = std::max(cpShapeGetElasticity(a), cpShapeGetElasticity(b));
    // Box2D's restitution threshold: slower than this, nothing bounces.
    if (restitution > 0.0 && approachSpeed(arbiter) < m_restitutionThreshold)
        restitution = 0.0;
    cpArbiterSetRestitution(arbiter, restitution);

    if (first->preSolveEvents || second->preSolveEvents)
        raiseBoth(first, second, QStringLiteral("preSolve"));
    return cpTrue;
}

void ChipmunkEngine::onSeparate(cpArbiter *arbiter)
{
    const auto flags = static_cast<uintptr_t>(
        reinterpret_cast<intptr_t>(cpArbiterGetUserData(arbiter)));
    if (flags == 0)
        return;
    cpArbiterSetUserData(arbiter, nullptr);

    CP_ARBITER_GET_SHAPES(arbiter, a, b);
    ShapeRecord *first = recordOf(a);
    ShapeRecord *second = recordOf(b);
    if (!first || !second)
        return;

    if (flags & kContactReported)
        raiseBoth(first, second, QStringLiteral("contactEnd"));
    if (flags & kSensorReported) {
        // Which side was the sensor is remembered rather than asked again: a
        // rule may have changed it since.
        ShapeRecord *sensor = (flags & kSensorIsSecond) ? second : first;
        ShapeRecord *visitor = (flags & kSensorIsSecond) ? first : second;
        sensor->overlaps = std::max(0, sensor->overlaps - 1);
        raiseBoth(sensor, visitor, QStringLiteral("sensorEnd"));
    }
}

QVector<EngineEvent> ChipmunkEngine::pollEvents()
{
    QVector<EngineEvent> drained;
    drained.swap(m_pendingEvents);
    return drained;
}

void ChipmunkEngine::collectBodyEvents()
{
    // What Box2D's body events say: every body the step carried, and the one
    // step on which a body settled. Nothing is remembered but whether it was
    // asleep -- "starts moving" is the rules' own rising edge.
    for (const auto &record : m_bodies) {
        if (record->removed || !record->inSpace)
            continue;
        const cpBodyType type = cpBodyGetType(record->body);
        if (type == CP_BODY_TYPE_STATIC)
            continue;

        const bool sleeping = cpBodyIsSleeping(record->body);
        const bool fellAsleep = sleeping && !record->wasSleeping;
        record->wasSleeping = sleeping;

        // A kinematic body never sleeps in Chipmunk; one standing still has
        // not been carried anywhere.
        const bool carried = type == CP_BODY_TYPE_DYNAMIC
                                 ? !sleeping
                                 : (cpvlengthsq(cpBodyGetVelocity(record->body)) > 0.0
                                    || cpBodyGetAngularVelocity(record->body) != 0.0);
        if (!fellAsleep && !carried)
            continue;

        EngineEvent event;
        event.body = record->handle;
        event.eventId = fellAsleep ? QStringLiteral("bodyFellAsleep")
                                   : QStringLiteral("bodyMoved");
        m_pendingEvents.append(event);
    }
}

RayHit ChipmunkEngine::castRay(const QPointF &origin, const QPointF &translation,
                               quint64 maskBits) const
{
    RayHit result;
    if (!m_space)
        return result;

    struct Nearest {
        quint64 maskBits = 0;
        cpVect start;
        cpFloat alpha = 2.0;
        const cpShape *shape = nullptr;
        cpVect point;
        cpVect normal;
    } nearest;
    nearest.maskBits = maskBits;
    nearest.start = toMeters(origin);

    // Every shape along the line, keeping the nearest one Box2D's ray would
    // have seen. Chipmunk's own filter has half the bits, so it is not used.
    cpSpaceSegmentQuery(
        m_space, nearest.start, toMeters(origin + translation), 0.0, CP_SHAPE_FILTER_ALL,
        [](cpShape *shape, cpVect point, cpVect normal, cpFloat alpha, void *data) {
            auto *found = static_cast<Nearest *>(data);
            const ShapeRecord *record = recordOf(shape);
            if (!record || alpha >= found->alpha)
                return;
            // Rays pass through sensors, as they do in Box2D.
            if (cpShapeGetSensor(shape))
                return;
            // A query is category 1 asking for maskBits.
            if ((record->filter.categoryBits & found->maskBits) == 0
                || (record->filter.maskBits & 1u) == 0)
                return;
            // Box2D does not report the shape a ray starts inside; Chipmunk
            // reports it at the very start.
            if (alpha <= 0.0) {
                cpPointQueryInfo inside;
                if (cpShapePointQuery(shape, found->start, &inside) < 0.0)
                    return;
            }
            found->alpha = alpha;
            found->shape = shape;
            found->point = point;
            found->normal = normal;
        },
        &nearest);

    if (!nearest.shape)
        return result;
    result.hit = true;
    result.point = toScene(nearest.point);
    result.normal = QPointF(nearest.normal.x, nearest.normal.y);
    result.distance = QLineF(origin, result.point).length();
    result.shapeName = recordOf(nearest.shape)->name;
    return result;
}

void ChipmunkEngine::explodeAt(cpVect position, const QVariantMap &params)
{
    // Chipmunk has no explosion, so this is Box2D's b2World_Explode: every
    // shape within reach is pushed away from the centre, in proportion to the
    // width it presents to it, fading to nothing over the falloff.
    const cpFloat radius = metres(params.value(QStringLiteral("radius"), 0.0).toDouble());
    const cpFloat falloff = std::max(0.0, metres(params.value(QStringLiteral("falloff"), 0.0).toDouble()));
    const cpFloat impulsePerLength =
        params.value(QStringLiteral("impulse"), 0.0).toDouble() / m_pixelsPerMeter;
    const double maskValue = params.value(QStringLiteral("maskBits"), 0.0).toDouble();
    // Zero means everything, so an older scene with no such setting is unchanged.
    const quint64 mask = maskValue > 0.0 ? static_cast<quint64>(maskValue) : ~quint64(0);
    if (!m_space || radius <= 0.0)
        return;

    const cpFloat reach = radius + falloff;
    QVector<cpShape *> near;
    cpSpaceBBQuery(
        m_space, cpBBNewForCircle(position, reach), CP_SHAPE_FILTER_ALL,
        [](cpShape *shape, void *data) { static_cast<QVector<cpShape *> *>(data)->append(shape); },
        &near);

    for (cpShape *shape : near) {
        const ShapeRecord *record = recordOf(shape);
        cpBody *body = cpShapeGetBody(shape);
        if (!record || !isDynamic(body) || cpShapeGetSensor(shape)
            || (record->filter.categoryBits & mask) == 0)
            continue;

        cpPointQueryInfo closest;
        cpShapePointQuery(shape, position, &closest);
        if (closest.distance > reach)
            continue;

        // Overlapping the centre, the nearest point says nothing about which
        // way is out; Box2D pushes from the shape's own centre then.
        cpVect point = closest.point;
        if (closest.distance <= 0.0)
            point = cpBodyLocalToWorld(body, cpShapeGetCenterOfGravity(shape));
        cpVect direction = cpvsub(point, position);
        direction = cpvlengthsq(direction) > 1e-24 ? cpvnormalize(direction) : cpv(1.0, 0.0);

        cpFloat scale = 1.0;
        if (closest.distance > radius && falloff > 0.0)
            scale = qBound(0.0, (reach - closest.distance) / falloff, 1.0);

        const cpFloat magnitude = impulsePerLength * projectedWidth(shape, direction) * scale;
        cpBodyApplyImpulseAtWorldPoint(body, cpvmult(direction, magnitude), point);
    }
}

void ChipmunkEngine::performAction(const QString &id, BodyHandle target,
                                   const QVariantMap &params)
{
    BodyRecord *record = bodyAt(target);
    if (!m_space || !record)
        return;
    cpBody *body = record->body;

    if (id == QLatin1String("explode")) {
        // The named body says only where.
        explodeAt(cpBodyGetPosition(body), params);
        return;
    }

    // The same kick as the impulse property, landing off the centre of mass
    // so that it turns the body as well.
    if (id == QLatin1String("pushAt")) {
        const auto amount = [this, &params](const char *key) {
            return metres(params.value(QLatin1String(key), 0.0).toDouble());
        };
        const cpVect centre = cpBodyLocalToWorld(body, cpBodyGetCenterOfGravity(body));
        const cpVect point = cpvadd(centre, cpv(amount("offsetX"), amount("offsetY")));
        cpBodyApplyImpulseAtWorldPoint(body, cpv(amount("impulseX"), amount("impulseY")), point);
        return;
    }

    // The same push as above, but a force -- gone again after this step.
    if (id == QLatin1String("pushForceAt")) {
        const auto amount = [this, &params](const char *key) {
            return metres(params.value(QLatin1String(key), 0.0).toDouble());
        };
        const cpVect centre = cpBodyLocalToWorld(body, cpBodyGetCenterOfGravity(body));
        const cpVect point = cpvadd(centre, cpv(amount("offsetX"), amount("offsetY")));
        cpBodyApplyForceAtWorldPoint(body, cpv(amount("impulseX"), amount("impulseY")), point);
        return;
    }

    // Chipmunk recomputes a body's mass from its shapes whenever one of them
    // changes, so setting a density to what it already is throws away a mass
    // that was set by hand.
    if (id == QLatin1String("resetMass")) {
        if (cpBodyGetType(body) == CP_BODY_TYPE_DYNAMIC) {
            for (cpShape *shape : record->shapes)
                cpShapeSetDensity(shape, cpShapeGetDensity(shape));
            applyFixedRotation(record);
        }
        return;
    }

    if (id == QLatin1String("removeBody")) {
        // Marked first, so the contacts the removal ends are not reported as
        // ending -- the thing on one side of them is gone.
        record->removed = true;
        for (const auto &joint : m_joints) {
            if (joint->broken)
                continue;
            if (cpConstraintGetBodyA(joint->constraint) != body
                && cpConstraintGetBodyB(joint->constraint) != body)
                continue;
            if (cpSpaceContainsConstraint(m_space, joint->constraint))
                cpSpaceRemoveConstraint(m_space, joint->constraint);
            joint->broken = true;
        }
        if (record->inSpace) {
            for (cpShape *shape : record->shapes)
                cpSpaceRemoveShape(m_space, shape);
            cpSpaceRemoveBody(m_space, body);
            record->inSpace = false;
        }
    }
}

void ChipmunkEngine::performActionAt(const QString &id, const QPointF &position,
                                     const QVariantMap &params)
{
    if (id == QLatin1String("explode"))
        explodeAt(toMeters(position), params);
}

} // namespace physics
