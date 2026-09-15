#include "ChipmunkEngine.h"

#include <chipmunk/chipmunk_structs.h>
#include <chipmunk/cpPolyline.h>

#include <QtMath>
#include <algorithm>
#include <cmath>

namespace physics {

// Chipmunk's own velocity update, with what Box2D does per body and Chipmunk
// does not: gravity scaled per body, damping as Box2D applies it, a top speed,
// and a body that is not allowed to sleep. Called inside the step, where the
// public setters must not be used -- every one of them wakes the body, and a
// body woken every step never falls asleep -- so the fields are written
// directly.
void updateBodyVelocity(cpBody *body, cpVect gravity, cpFloat damping, cpFloat dt)
{
    const auto *record = static_cast<ChipmunkEngine::BodyRecord *>(cpBodyGetUserData(body));
    if (!record || cpBodyGetType(body) != CP_BODY_TYPE_DYNAMIC) {
        cpBodyUpdateVelocity(body, gravity, damping, dt);
        return;
    }

    cpBodyUpdateVelocity(body, cpvmult(gravity, record->gravityScale), damping, dt);

    // Box2D's form, v / (1 + h c), rather than Chipmunk's space-wide factor, so
    // the same number slows a body the same amount in either engine.
    body->v = cpvmult(body->v, 1.0 / (1.0 + dt * record->linearDamping));
    body->w *= 1.0 / (1.0 + dt * record->angularDamping);

    const cpFloat maximum = record->engine->m_maximumSpeed;
    if (maximum > 0.0 && cpvlengthsq(body->v) > maximum * maximum)
        body->v = cpvmult(cpvnormalize(body->v), maximum);

    // Chipmunk sleeps a body once it has idled long enough, and has no switch
    // per body. Never letting the idle time build up is that switch.
    if (!record->enableSleep)
        body->sleeping.idleTime = 0.0;
}

namespace {

// What the editor stored for this object, under the names this engine
// published. A key that is not there was never changed from what the catalogue
// said it starts as, which is the fallback given here.
double number(const QVariantMap &params, const char *key, double fallback)
{
    return params.value(QLatin1String(key), fallback).toDouble();
}

bool flag(const QVariantMap &params, const char *key, bool fallback)
{
    return params.value(QLatin1String(key), fallback).toBool();
}

} // namespace

ChipmunkEngine::~ChipmunkEngine()
{
    destroyWorld();
}

cpVect ChipmunkEngine::toMeters(const QPointF &scenePoint) const
{
    return cpv(scenePoint.x() / m_pixelsPerMeter, scenePoint.y() / m_pixelsPerMeter);
}

QPointF ChipmunkEngine::toScene(cpVect metres) const
{
    return QPointF(metres.x * m_pixelsPerMeter, metres.y * m_pixelsPerMeter);
}

ChipmunkEngine::BodyRecord *ChipmunkEngine::bodyAt(BodyHandle handle) const
{
    if (handle < 0 || handle >= static_cast<BodyHandle>(m_bodies.size()))
        return nullptr;
    BodyRecord *record = m_bodies[handle].get();
    return record->removed ? nullptr : record;
}

ChipmunkEngine::JointRecord *ChipmunkEngine::jointAt(JointHandle handle) const
{
    if (handle < 0 || handle >= static_cast<JointHandle>(m_joints.size()))
        return nullptr;
    JointRecord *record = m_joints[handle].get();
    return record->broken ? nullptr : record;
}

QVector<ChipmunkEngine::ShapeRecord *> ChipmunkEngine::shapesNamed(const QString &name) const
{
    QVector<ShapeRecord *> alive;
    for (ShapeRecord *shape : m_shapesByName.value(name)) {
        if (!shape->owner->removed)
            alive.append(shape);
    }
    return alive;
}

void ChipmunkEngine::createWorld(const WorldDesc &desc)
{
    destroyWorld();

    // The same two scales as the Box2D plugin: geometry by the scene's own,
    // pace by the reference, so what is on screen moves at the same speed
    // whatever the scene's scale.
    m_pixelsPerMeter = desc.pixelsPerMeter > 0.0 ? desc.pixelsPerMeter
                                                 : kReferencePixelsPerMeter;
    m_motionScale = kReferencePixelsPerMeter / m_pixelsPerMeter;

    const QVariantMap &world = desc.params;
    m_space = cpSpaceNew();
    cpSpaceSetUserData(m_space, this);
    cpSpaceSetGravity(m_space, cpv(number(world, "gravityX", 0.0) * m_motionScale,
                                   number(world, "gravityY", 9.81) * m_motionScale));

    // How many times the whole step is relaxed. Chipmunk's own default is 10.
    cpSpaceSetIterations(m_space, qMax(1, static_cast<int>(number(world, "iterations", 10))));
    cpSpaceSetDamping(m_space, qMax(0.0, number(world, "damping", 1.0)));

    // How far shapes may overlap before they are pushed apart. Chipmunk's
    // default is a tenth of a unit, which in metres is 100 px at this scale;
    // a quarter of a scene unit is what Box2D's slop comes to here.
    cpSpaceSetCollisionSlop(m_space, metres(qMax(0.0, number(world, "collisionSlop", 0.25))));
    cpSpaceSetCollisionBias(m_space,
                            qBound(0.0, number(world, "collisionBias", std::pow(0.9, 60.0)), 1.0));
    cpSpaceSetCollisionPersistence(
        m_space, static_cast<cpTimestamp>(qMax(0.0, number(world, "collisionPersistence", 3))));

    // Box2D's thresholds and top speed, which Chipmunk does not have. Speeds,
    // so quoted at the reference scale.
    m_restitutionThreshold = number(world, "restitutionThreshold", 1.0) * m_motionScale;
    m_hitEventThreshold = number(world, "hitEventThreshold", 1.0) * m_motionScale;
    m_maximumSpeed = number(world, "maximumLinearSpeed", 400.0) * m_motionScale;

    m_enableSleep = flag(world, "enableSleep", true);
    m_sleepTime = qMax(0.0, number(world, "sleepTimeThreshold", 0.5));
    cpSpaceSetSleepTimeThreshold(m_space, m_enableSleep ? m_sleepTime : INFINITY);
    // Chipmunk would otherwise estimate it from gravity. One idle speed for the
    // whole world, quoted like any other speed.
    cpSpaceSetIdleSpeedThreshold(m_space,
                                 number(world, "idleSpeedThreshold", 0.05) * m_motionScale);

    // One handler for every pair: the events, the filter and the way surfaces
    // combine are the same whatever is touching.
    cpCollisionHandler *handler = cpSpaceAddDefaultCollisionHandler(m_space);
    handler->userData = this;
    handler->beginFunc = [](cpArbiter *arbiter, cpSpace *, cpDataPointer engine) -> cpBool {
        return static_cast<ChipmunkEngine *>(engine)->onBegin(arbiter);
    };
    handler->preSolveFunc = [](cpArbiter *arbiter, cpSpace *, cpDataPointer engine) -> cpBool {
        return static_cast<ChipmunkEngine *>(engine)->onPreSolve(arbiter);
    };
    handler->separateFunc = [](cpArbiter *arbiter, cpSpace *, cpDataPointer engine) {
        static_cast<ChipmunkEngine *>(engine)->onSeparate(arbiter);
    };
}

void ChipmunkEngine::destroyWorld()
{
    // The space owns none of what was added to it and calls no callbacks as it
    // goes, so it is freed first and everything that was in it afterwards.
    if (m_space)
        cpSpaceFree(m_space);
    m_space = nullptr;
    m_sensorWatched = false;

    for (const auto &joint : m_joints) {
        cpConstraintFree(joint->constraint);
        if (joint->hand)
            cpBodyFree(joint->hand);
    }
    for (const auto &shape : m_shapes)
        cpShapeFree(shape->shape);
    for (const auto &body : m_bodies)
        cpBodyFree(body->body);

    m_joints.clear();
    m_shapes.clear();
    m_shapesByName.clear();
    m_bodies.clear();
    m_pendingEvents.clear();
    m_lastHit.clear();
}

ChipmunkEngine::ShapeRecord *ChipmunkEngine::addShape(BodyRecord *record, cpShape *shape,
                                                      const ShapePart &part)
{
    const QVariantMap &values = part.params;
    cpShapeSetFriction(shape, std::max(0.0, number(values, "friction", 0.6)));
    cpShapeSetElasticity(shape, std::max(0.0, number(values, "restitution", 0.0)));
    cpShapeSetSensor(shape, flag(values, "isSensor", false));

    auto owned = std::make_unique<ShapeRecord>();
    ShapeRecord *made = owned.get();
    made->name = part.name;
    made->shape = shape;
    made->owner = record;
    made->filter.categoryBits = filterBits(values.value(QStringLiteral("categoryBits")), 1);
    made->filter.maskBits = filterBits(values.value(QStringLiteral("maskBits")), ~quint64(0));
    made->filter.groupIndex = static_cast<int>(number(values, "groupIndex", 0.0));
    made->contactEvents = flag(values, "enableContactEvents", false);
    made->hitEvents = flag(values, "enableHitEvents", false);
    made->sensorEvents = flag(values, "enableSensorEvents", false);
    made->preSolveEvents = flag(values, "enablePreSolveEvents", false);
    // A rule watches this shape, and an engine reports nothing about a shape
    // that did not ask -- so it asks on the shape's behalf.
    if (part.watchedByRules) {
        made->contactEvents = true;
        made->hitEvents = true;
        // As in Box2D, a sensor notices only a shape that asked to be noticed,
        // and none has by default. Once a rule watches a sensor, every shape
        // asks: the ones already made now, the ones to come as they are made.
        if (flag(values, "isSensor", false) && !m_sensorWatched) {
            m_sensorWatched = true;
            for (const auto &existing : m_shapes)
                existing->sensorEvents = true;
        }
    }
    if (m_sensorWatched)
        made->sensorEvents = true;
    made->tangentSpeed = number(values, "tangentSpeed", 0.0);
    cpShapeSetUserData(shape, made);

    cpSpaceAddShape(m_space, shape);
    // After the shape has its body: this is what gives a dynamic body its
    // mass, and Chipmunk works it out again from every shape each time.
    cpShapeSetDensity(shape, std::max(0.0, number(values, "density", 1.0)));

    record->shapes.push_back(shape);
    m_shapes.push_back(std::move(owned));
    return made;
}

bool ChipmunkEngine::attachEdges(BodyRecord *record, const ShapePart &part,
                                 const QVector<cpVect> &points, bool closed, bool smooth)
{
    const int count = points.size();
    if (count < 2)
        return false;

    // A two-sided segment per edge. Chipmunk's segments can be told what joins
    // them at either end, which is what stops a body sliding along the outline
    // catching on the seams -- Box2D needs a one-sided chain for the same.
    const int edges = closed ? count : count - 1;
    bool any = false;
    for (int i = 0; i < edges; ++i) {
        const cpVect a = points[i];
        const cpVect b = points[(i + 1) % count];
        if (cpveql(a, b))
            continue;
        cpShape *segment = cpSegmentShapeNew(record->body, a, b, 0.0);
        if (smooth) {
            const cpVect previous = (closed || i > 0) ? points[(i - 1 + count) % count] : a;
            const cpVect next = (closed || i + 2 < count) ? points[(i + 2) % count] : b;
            cpSegmentShapeSetNeighbors(segment, previous, next);
        }
        addShape(record, segment, part);
        any = true;
    }
    return any;
}

bool ChipmunkEngine::attachConvexPieces(BodyRecord *record, const ShapePart &part,
                                        const QVector<cpVect> &points)
{
    // Box2D can only give mass to a convex outline. Chipmunk ships a convex
    // decomposition, so a concave one becomes several convex shapes on the one
    // body -- solid, with mass, and colliding as drawn.
    const int count = points.size();
    auto *line = static_cast<cpPolyline *>(
        cpcalloc(1, sizeof(cpPolyline) + (count + 1) * sizeof(cpVect)));
    line->count = line->capacity = count + 1;
    std::copy(points.cbegin(), points.cend(), line->verts);
    // It wants the outline wound the way that gives a positive area, and closed
    // by repeating the first point.
    if (cpAreaForPoly(count, line->verts, 0.0) < 0.0)
        std::reverse(line->verts, line->verts + count);
    line->verts[count] = line->verts[0];

    bool any = false;
    // The tolerance is how far a piece may stray from the outline; the collision
    // slop is already more than anyone would see.
    if (cpPolylineSet *pieces = cpPolylineConvexDecomposition(line, metres(0.25))) {
        for (int i = 0; i < pieces->count; ++i) {
            const cpPolyline *hull = pieces->lines[i];
            if (hull->count < 3)
                continue;
            // A closed hull repeats its first point; making the shape takes the
            // convex hull again, which drops the repeat.
            addShape(record, cpPolyShapeNew(record->body, hull->count, hull->verts,
                                            cpTransformIdentity, 0.0),
                     part);
            any = true;
        }
        cpPolylineSetFree(pieces, cpTrue);
    }
    cpPolylineFree(line);
    return any;
}

bool ChipmunkEngine::attachShape(BodyRecord *record, const ShapePart &part, BodyType type)
{
    const Geometry &geometry = part.geometry;
    // An outline with no interior has no area, so no mass. A dynamic body made
    // of one would have nothing to accelerate, and the Box2D plugin refuses it
    // for the same reason.
    const bool needsMass = type == BodyType::Dynamic;

    QVector<cpVect> points;
    points.reserve(geometry.points.size());
    for (const QPointF &point : geometry.points)
        points.append(toMeters(point));

    switch (geometry.kind) {
    case GeometryKind::Box: {
        const cpFloat halfW = metres(geometry.halfExtents.x());
        const cpFloat halfH = metres(geometry.halfExtents.y());
        if (halfW <= 0.0 || halfH <= 0.0)
            return false;

        // A rounded polygon grows outward by its radius, so the corners are
        // inset by it -- the shape collides at the size it is drawn. At half the
        // shorter side what is left is a line, and the result a capsule. The
        // hundredth of a unit keeps that line from collapsing to a point.
        const cpFloat radius = metres(qBound(0.0, geometry.cornerRadius,
                                             qMin(geometry.halfExtents.x(),
                                                  geometry.halfExtents.y())));
        const cpFloat insetW = std::max(halfW - radius, metres(0.01));
        const cpFloat insetH = std::max(halfH - radius, metres(0.01));
        const cpVect rotation = cpvforangle(qDegreesToRadians(geometry.rotationDegrees));
        const cpVect centre = toMeters(geometry.center);
        cpVect corners[4] = { cpv(-insetW, -insetH), cpv(insetW, -insetH),
                              cpv(insetW, insetH), cpv(-insetW, insetH) };
        for (cpVect &corner : corners)
            corner = cpvadd(centre, cpvrotate(corner, rotation));
        addShape(record, cpPolyShapeNew(record->body, 4, corners, cpTransformIdentity, radius),
                 part);
        return true;
    }
    case GeometryKind::Circle: {
        const cpFloat radius = metres(geometry.radius);
        if (radius <= 0.0)
            return false;
        addShape(record, cpCircleShapeNew(record->body, radius, toMeters(geometry.center)), part);
        return true;
    }
    case GeometryKind::Polygon: {
        if (points.size() < 3)
            return false;
        // No vertex cap: Chipmunk takes a convex outline of any size.
        if (physics::isConvex(geometry.points)) {
            addShape(record, cpPolyShapeNew(record->body, points.size(), points.constData(),
                                            cpTransformIdentity, 0.0),
                     part);
            return true;
        }
        if (attachConvexPieces(record, part, points))
            return true;
        if (needsMass)
            return false;
        return attachEdges(record, part, points, true, false);
    }
    case GeometryKind::Chain:
        if (needsMass)
            return false;
        return attachEdges(record, part, points, geometry.closed, geometry.smoothChain);
    }
    return false;
}

BodyHandle ChipmunkEngine::addBody(const BodyDesc &desc)
{
    if (!m_space || desc.parts.isEmpty())
        return kInvalidBody;

    const QVariantMap &values = desc.params;
    auto owned = std::make_unique<BodyRecord>();
    BodyRecord *record = owned.get();
    record->engine = this;
    record->handle = static_cast<BodyHandle>(m_bodies.size());
    record->gravityScale = number(values, "gravityScale", 1.0);
    record->linearDamping = std::max(0.0, number(values, "linearDamping", 0.0));
    record->angularDamping = std::max(0.0, number(values, "angularDamping", 0.0));
    record->enableSleep = flag(values, "enableSleep", true);
    record->fixedRotation = flag(values, "fixedRotation", false);

    switch (desc.type) {
    case BodyType::Static:    record->body = cpBodyNewStatic(); break;
    case BodyType::Kinematic: record->body = cpBodyNewKinematic(); break;
    // Mass and moment come from the shapes' density once they are attached.
    case BodyType::Dynamic:   record->body = cpBodyNew(0.0, 0.0); break;
    }
    cpBody *body = record->body;
    cpBodySetUserData(body, record);
    cpBodySetVelocityUpdateFunc(body, updateBodyVelocity);
    cpBodySetPosition(body, toMeters(desc.position));
    cpBodySetAngle(body, qDegreesToRadians(desc.rotationDegrees));
    cpSpaceAddBody(m_space, body);

    // Every part has to be representable -- a body that silently dropped one of
    // its pieces would collide differently from what is drawn.
    const size_t shapesBefore = m_shapes.size();
    for (const ShapePart &part : desc.parts) {
        if (attachShape(record, part, desc.type))
            continue;
        for (cpShape *shape : record->shapes) {
            cpSpaceRemoveShape(m_space, shape);
            cpShapeFree(shape);
        }
        m_shapes.resize(shapesBefore);
        cpSpaceRemoveBody(m_space, body);
        cpBodyFree(body);
        return kInvalidBody;
    }

    if (desc.type == BodyType::Dynamic) {
        // All density zero. Box2D leaves such a body without an inverse mass,
        // so it keeps whatever velocity it is given and nothing pushes it --
        // which is a kinematic body. Chipmunk would divide by the zero instead.
        const cpFloat mass = cpBodyGetMass(body);
        const cpFloat moment = cpBodyGetMoment(body);
        if (!(mass > 0.0 && std::isfinite(mass) && moment > 0.0 && std::isfinite(moment)))
            cpBodySetType(body, CP_BODY_TYPE_KINEMATIC);
        applyFixedRotation(record);
    }

    // Quoted at the reference scale, the same as gravity. Scenery has none.
    if (desc.type != BodyType::Static) {
        cpBodySetVelocity(body, cpv(number(values, "velocityX", 0.0) * m_motionScale,
                                    number(values, "velocityY", 0.0) * m_motionScale));
        cpBodySetAngularVelocity(body,
                                 qDegreesToRadians(number(values, "angularVelocity", 0.0)));
    }

    for (size_t i = shapesBefore; i < m_shapes.size(); ++i) {
        if (!m_shapes[i]->name.isEmpty())
            m_shapesByName[m_shapes[i]->name].append(m_shapes[i].get());
    }
    m_bodies.push_back(std::move(owned));

    if (!desc.isEnabled)
        setInSpace(record, false);
    else if (!flag(values, "isAwake", true) && m_enableSleep
             && cpBodyGetType(body) == CP_BODY_TYPE_DYNAMIC)
        cpBodySleep(body);

    return record->handle;
}

void ChipmunkEngine::applyFixedRotation(BodyRecord *record)
{
    cpBody *body = record->body;
    if (cpBodyGetType(body) != CP_BODY_TYPE_DYNAMIC)
        return;
    if (record->fixedRotation) {
        cpBodySetMoment(body, INFINITY);
        cpBodySetAngularVelocity(body, 0.0);
    } else if (!std::isfinite(cpBodyGetMoment(body)) && !record->shapes.empty()) {
        // There is no public call to work the moment out again, but setting any
        // shape's density does exactly that.
        cpShape *shape = record->shapes.front();
        cpShapeSetDensity(shape, cpShapeGetDensity(shape));
    }
}

void ChipmunkEngine::setInSpace(BodyRecord *record, bool inSpace)
{
    if (!m_space || record->removed || record->inSpace == inSpace)
        return;

    // Something the space itself owns -- its static body, a mouse joint's hand
    // -- carries no record and is always there.
    const auto present = [](cpBody *body) {
        const auto *owner = static_cast<BodyRecord *>(cpBodyGetUserData(body));
        return !owner || (owner->inSpace && !owner->removed);
    };

    if (!inSpace) {
        for (const auto &joint : m_joints) {
            if (joint->broken || !cpSpaceContainsConstraint(m_space, joint->constraint))
                continue;
            if (cpConstraintGetBodyA(joint->constraint) == record->body
                || cpConstraintGetBodyB(joint->constraint) == record->body)
                cpSpaceRemoveConstraint(m_space, joint->constraint);
        }
        for (cpShape *shape : record->shapes)
            cpSpaceRemoveShape(m_space, shape);
        cpSpaceRemoveBody(m_space, record->body);
        record->inSpace = false;
        return;
    }

    cpSpaceAddBody(m_space, record->body);
    for (cpShape *shape : record->shapes)
        cpSpaceAddShape(m_space, shape);
    record->inSpace = true;
    for (const auto &joint : m_joints) {
        if (joint->broken || cpSpaceContainsConstraint(m_space, joint->constraint))
            continue;
        if (present(cpConstraintGetBodyA(joint->constraint))
            && present(cpConstraintGetBodyB(joint->constraint)))
            cpSpaceAddConstraint(m_space, joint->constraint);
    }
}

void ChipmunkEngine::step(qreal dt)
{
    if (!m_space)
        return;

    // Kept because gliding a body there asks how long it has to arrive.
    m_lastStep = dt;

    // Chipmunk's surface velocity is a world direction. Box2D's tangent speed
    // runs along the surface, so it is turned with the body before each step --
    // and only written when it changed, because writing it wakes the body.
    for (const auto &shape : m_shapes) {
        if (shape->owner->removed)
            continue;
        const cpVect wanted = cpvmult(cpBodyGetRotation(shape->owner->body), shape->tangentSpeed);
        if (!cpveql(wanted, cpShapeGetSurfaceVelocity(shape->shape)))
            cpShapeSetSurfaceVelocity(shape->shape, wanted);
    }

    cpSpaceStep(m_space, dt);

    // After the solver, so a joint reports where it ended up rather than where
    // it was before the step carried it into its limit.
    detectLimitEvents();
    collectBodyEvents();
}

BodyState ChipmunkEngine::bodyState(BodyHandle handle) const
{
    BodyState state;
    if (handle < 0 || handle >= static_cast<BodyHandle>(m_bodies.size()))
        return state;
    const BodyRecord *record = m_bodies[handle].get();
    // A rule may have removed it. The handle survives so the editor's indices
    // stay put, but there is nothing behind it any more.
    if (record->removed) {
        state.exists = false;
        return state;
    }
    const cpBody *body = record->body;
    state.position = toScene(cpBodyGetPosition(body));
    state.rotationDegrees = qRadiansToDegrees(cpBodyGetAngle(body));
    state.centerOfMass = toScene(cpBodyLocalToWorld(body, cpBodyGetCenterOfGravity(body)));
    state.awake = !cpBodyIsSleeping(body);
    return state;
}

} // namespace physics
