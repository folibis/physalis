#include "Box2DEngine.h"

#include <QByteArray>
#include <QFileInfo>
#include <QObject>
#include <QSet>
#include <QtMath>
#include <algorithm>

namespace physics {

namespace {

// What the editor stored for this object, under the names this engine
// published in its catalogue. A key that is not there was never changed from
// what the catalogue said it starts as, which is the fallback given here.
double number(const QVariantMap &params, const char *key, double fallback)
{
    return params.value(QLatin1String(key), fallback).toDouble();
}

bool flag(const QVariantMap &params, const char *key, bool fallback)
{
    return params.value(QLatin1String(key), fallback).toBool();
}

// Box2D checks its own arithmetic and, left alone, ends the process when a
// check fails -- which takes the editor with it. A scene is allowed to ask for
// the impossible, so the check is caught instead: the message is kept, the
// step is allowed to finish, and step() cleans up after it. Returning zero is
// what tells Box2D not to break.
QString g_lastAssertion;

int rememberAssertion(const char *condition, const char *fileName, int lineNumber)
{
    if (g_lastAssertion.isEmpty()) {
        g_lastAssertion = QStringLiteral("%1 (%2:%3)")
                              .arg(QString::fromLatin1(condition),
                                   QFileInfo(QString::fromLatin1(fileName)).fileName())
                              .arg(lineNumber);
    }
    return 0;
}

b2BodyType toB2BodyType(BodyType type)
{
    switch (type) {
    case BodyType::Static:    return b2_staticBody;
    case BodyType::Kinematic: return b2_kinematicBody;
    case BodyType::Dynamic:   return b2_dynamicBody;
    }
    return b2_staticBody;
}

} // namespace

Box2DEngine::~Box2DEngine()
{
    destroyWorld();
}

b2Vec2 Box2DEngine::toMeters(const QPointF &scenePoint) const
{
    return b2Vec2 { static_cast<float>(scenePoint.x() / m_pixelsPerMeter),
                    static_cast<float>(scenePoint.y() / m_pixelsPerMeter) };
}

QPointF Box2DEngine::toScene(b2Vec2 meters) const
{
    return QPointF(meters.x * m_pixelsPerMeter, meters.y * m_pixelsPerMeter);
}

void Box2DEngine::createWorld(const WorldDesc &desc)
{
    destroyWorld();

    // Geometry is converted by the scene's own scale, so the solver really does
    // get a 0.2 m box when the scene says 1000 px per metre.
    m_pixelsPerMeter = desc.pixelsPerMeter > 0.0 ? desc.pixelsPerMeter
                                                 : kReferencePixelsPerMeter;

    // ... but the *pace* must not depend on that. Gravity and velocity are
    // quoted at kReferencePixelsPerMeter, so scaling them by the ratio keeps
    // pixels-per-second on screen identical at every scale: the shrinking
    // world and the shrinking gravity cancel exactly.
    m_motionScale = kReferencePixelsPerMeter / m_pixelsPerMeter;

    // Box2D's tolerances are lengths too, and fixed for a world measured in
    // metres: 5 mm of slop, and a contact made once two shapes are within
    // 2 cm. At 1000 px per metre a drawn box is a few centimetres, so those
    // show on screen -- "begins contact" arrived while shapes were still 20 px
    // apart. Box2D scales them by its length unit, which is set to the same
    // reference the pace is quoted at, so they are what they would be at
    // 50 px per metre whatever the scene's scale. Set before the world is made.
    b2SetLengthUnitsPerMeter(static_cast<float>(m_motionScale));

    const QVariantMap &world = desc.params;
    b2WorldDef worldDef = b2DefaultWorldDef();
    // Box2D already works in meters and seconds, so an m/s^2 acceleration
    // needs no conversion -- unlike the geometry, which is in scene units.
    worldDef.gravity = b2Vec2 { static_cast<float>(number(world, "gravityX", 0.0) * m_motionScale),
                                static_cast<float>(number(world, "gravityY", 9.81) * m_motionScale) };

    // Box2D's own tuning thresholds are speeds in m/s, so they have to move
    // with the scale as well -- otherwise a small scene trips every one of
    // them at once and a large scene trips none.
    worldDef.maximumLinearSpeed =
        static_cast<float>(number(world, "maximumLinearSpeed", 400.0) * m_motionScale);
    worldDef.maxContactPushSpeed =
        static_cast<float>(number(world, "maxContactPushSpeed", 3.0) * m_motionScale);
    worldDef.restitutionThreshold =
        static_cast<float>(number(world, "restitutionThreshold", 1.0) * m_motionScale);
    worldDef.hitEventThreshold =
        static_cast<float>(number(world, "hitEventThreshold", 1.0) * m_motionScale);

    // Stiffness and damping are not speeds, so the scale leaves them alone.
    worldDef.contactHertz = static_cast<float>(number(world, "contactHertz", 30.0));
    worldDef.contactDampingRatio = static_cast<float>(number(world, "contactDampingRatio", 10.0));

    worldDef.enableSleep = flag(world, "enableSleep", true);
    worldDef.enableContinuous = flag(world, "enableContinuous", true);

    // b2World_SetContactTuning takes all three at once and Box2D has no
    // getters for them, so what the world starts with is remembered here.
    m_contactTuning = { worldDef.contactHertz, worldDef.contactDampingRatio,
                        worldDef.maxContactPushSpeed };
    m_speculative = true;
    // Not part of b2WorldDef -- it is an argument to every b2World_Step, so it
    // is kept rather than handed over.
    m_subStepCount = qBound(1, static_cast<int>(number(world, "subStepCount", 4.0)), 64);

    b2SetAssertFcn(rememberAssertion);
    g_lastAssertion.clear();
    m_worldId = b2CreateWorld(&worldDef);

    // Without this the shapes' Pre-Solve Events flag reaches Box2D and then
    // has nowhere to go. Box2D calls it only for shapes that asked, and only
    // for awake dynamic bodies, so it costs nothing where it is not wanted.
    // No task system is supplied, so the step runs serially on this thread and
    // the callback can queue an event without locking.
    b2World_SetPreSolveCallback(
        m_worldId,
        [](b2ShapeId shapeA, b2ShapeId shapeB, b2Manifold *, void *context) {
            return static_cast<Box2DEngine *>(context)->preSolve(shapeA, shapeB);
        },
        this);
}

void Box2DEngine::destroyWorld()
{
    if (b2World_IsValid(m_worldId)) {
        // Destroying the world destroys every body in it, so the handles just
        // go away with it rather than needing individual cleanup.
        b2DestroyWorld(m_worldId);
    }
    m_worldId = b2_nullWorldId;
    m_bodies.clear();
    m_joints.clear();
    m_travelOrigins.clear();
    m_jointLimits.clear();
    m_pendingEvents.clear();
    m_shapeNames.clear();
    m_shapesByName.clear();
    m_lastHit.clear();
    m_problems.clear();
    m_ruined.clear();
}

bool Box2DEngine::attachSmoothChain(b2BodyId bodyId, const Geometry &geometry,
                                    const b2ShapeDef &shapeDef) const
{
    QVector<b2Vec2> points;
    points.reserve(geometry.points.size());
    for (const QPointF &point : geometry.points)
        points.append(toMeters(point));

    b2ChainDef chainDef = b2DefaultChainDef();
    chainDef.points = points.constData();
    chainDef.count = points.size();
    chainDef.isLoop = geometry.closed;
    chainDef.filter = shapeDef.filter;
    chainDef.enableSensorEvents = shapeDef.enableSensorEvents;
    // Every other shape carries the index of its name here, and the segments a
    // chain is made of are shapes like any other. Left unset they come back
    // with none, which reads as index zero -- so the chain answered to the
    // first shape's name, that shape stopped answering to its own, and the
    // chain itself could not be named by a rule at all.
    chainDef.userData = shapeDef.userData;

    // A chain carries its material per segment rather than on the shape def.
    b2SurfaceMaterial material = b2DefaultSurfaceMaterial();
    material.friction = shapeDef.material.friction;
    material.restitution = shapeDef.material.restitution;
    material.rollingResistance = shapeDef.material.rollingResistance;
    material.tangentSpeed = shapeDef.material.tangentSpeed;
    chainDef.materials = &material;
    chainDef.materialCount = 1;

    b2CreateChain(bodyId, &chainDef);
    return true;
}

bool Box2DEngine::attachEdgeChain(b2BodyId bodyId, const QVector<QPointF> &points, bool closed,
                                  const b2ShapeDef &shapeDef) const
{
    if (points.size() < 2)
        return false;

    // One b2Segment per edge, rather than b2CreateChain. Chain shapes are
    // one-sided (they only collide from the right of their winding) and need
    // at least 4 points; a segment collides from both sides at any point
    // count, which is what someone who just drew a line across the canvas
    // expects when they drop something onto either side of it.
    const int lastEdge = closed ? points.size() : points.size() - 1;
    for (int i = 0; i < lastEdge; ++i) {
        const b2Segment segment { toMeters(points[i]), toMeters(points[(i + 1) % points.size()]) };
        b2CreateSegmentShape(bodyId, &shapeDef, &segment);
    }
    return true;
}

bool Box2DEngine::attachShape(b2BodyId bodyId, const ShapePart &part, BodyType bodyType,
                              const b2ShapeDef &shapeDef) const
{
    const Geometry &geometry = part.geometry;

    // An outline with no interior has no area, so density gives it no mass.
    // Box2D leaves a massless dynamic body with invMass == 0 -- frozen in
    // place rather than falling -- so refuse it rather than simulate
    // something that silently doesn't move.
    const bool massless = bodyType == BodyType::Dynamic;

    switch (geometry.kind) {
    case GeometryKind::Box: {
        const float halfW = static_cast<float>(geometry.halfExtents.x() / m_pixelsPerMeter);
        const float halfH = static_cast<float>(geometry.halfExtents.y() / m_pixelsPerMeter);
        if (halfW <= 0.0f || halfH <= 0.0f)
            return false;
        const b2Rot rotation =
            b2MakeRot(static_cast<float>(qDegreesToRadians(geometry.rotationDegrees)));

        // Box2D grows a rounded polygon outward by its radius, so the hull is
        // inset by the same amount -- otherwise the shape would collide larger
        // than it is drawn. At the cap the inset hull is a line and the result
        // is a capsule, which is exactly right.
        const float radius = static_cast<float>(
            qBound(0.0, geometry.cornerRadius, qMin(geometry.halfExtents.x(),
                                                    geometry.halfExtents.y()))
            / m_pixelsPerMeter);

        const b2Polygon box =
            radius > 0.0f
                ? b2MakeOffsetRoundedBox(qMax(halfW - radius, 1e-5f),
                                         qMax(halfH - radius, 1e-5f),
                                         toMeters(geometry.center), rotation, radius)
                : b2MakeOffsetBox(halfW, halfH, toMeters(geometry.center), rotation);
        b2CreatePolygonShape(bodyId, &shapeDef, &box);
        return true;
    }
    case GeometryKind::Circle: {
        const float radius = static_cast<float>(geometry.radius / m_pixelsPerMeter);
        if (radius <= 0.0f)
            return false;
        const b2Circle circle { toMeters(geometry.center), radius };
        b2CreateCircleShape(bodyId, &shapeDef, &circle);
        return true;
    }
    case GeometryKind::Polygon: {
        if (geometry.points.size() < 3)
            return false;

        // A solid polygon needs to be convex and within Box2D's vertex cap.
        // Anything else can still collide correctly as an edge chain around
        // the same outline -- but only if it doesn't need mass.
        if (geometry.points.size() <= B2_MAX_POLYGON_VERTICES && physics::isConvex(geometry.points)) {
            b2Vec2 points[B2_MAX_POLYGON_VERTICES];
            for (int i = 0; i < geometry.points.size(); ++i)
                points[i] = toMeters(geometry.points[i]);

            const b2Hull hull = b2ComputeHull(points, geometry.points.size());
            if (hull.count >= 3) {
                const b2Polygon polygon = b2MakePolygon(&hull, 0.0f);
                b2CreatePolygonShape(bodyId, &shapeDef, &polygon);
                return true;
            }
        }

        if (massless)
            return false;
        return attachEdgeChain(bodyId, geometry.points, true, shapeDef);
    }
    case GeometryKind::Chain: {
        if (massless)
            return false;
        // b2CreateChain asserts below four points rather than failing, so the
        // count is checked here and not left to it.
        if (geometry.smoothChain && geometry.points.size() >= 4)
            return attachSmoothChain(bodyId, geometry, shapeDef);
        return attachEdgeChain(bodyId, geometry.points, geometry.closed, shapeDef);
    }
    }
    return false;
}

BodyHandle Box2DEngine::addBody(const BodyDesc &desc)
{
    if (!b2World_IsValid(m_worldId))
        return kInvalidBody;

    const QVariantMap &body = desc.params;
    b2BodyDef bodyDef = b2DefaultBodyDef();
    bodyDef.type = toB2BodyType(desc.type);
    bodyDef.position = toMeters(desc.position);
    bodyDef.rotation = b2MakeRot(static_cast<float>(qDegreesToRadians(desc.rotationDegrees)));
    // Same reasoning as gravity: quoted at the reference scale.
    bodyDef.linearVelocity =
        b2Vec2 { static_cast<float>(number(body, "velocityX", 0.0) * m_motionScale),
                 static_cast<float>(number(body, "velocityY", 0.0) * m_motionScale) };
    bodyDef.angularVelocity =
        static_cast<float>(qDegreesToRadians(number(body, "angularVelocity", 0.0)));
    bodyDef.linearDamping = static_cast<float>(number(body, "linearDamping", 0.0));
    bodyDef.angularDamping = static_cast<float>(number(body, "angularDamping", 0.0));
    bodyDef.gravityScale = static_cast<float>(number(body, "gravityScale", 1.0));
    bodyDef.enableSleep = flag(body, "enableSleep", true);
    bodyDef.isAwake = flag(body, "isAwake", true);
    // A speed, so quoted at the reference scale like gravity and velocity.
    // Without this a small-scale scene puts bodies to sleep in mid-air: the
    // default 0.05 m/s threshold is never exceeded when the whole world is
    // only centimetres across, so everything "stops moving" while falling.
    bodyDef.sleepThreshold =
        static_cast<float>(number(body, "sleepThreshold", 0.05) * m_motionScale);
    bodyDef.fixedRotation = flag(body, "fixedRotation", false);
    bodyDef.isBullet = flag(body, "isBullet", false);
    bodyDef.allowFastRotation = flag(body, "allowFastRotation", false);
    bodyDef.isEnabled = desc.isEnabled;
    // b2BodyDef borrows the name rather than copying it, so it has to outlive
    // b2CreateBody -- keep the encoded bytes alive until after the call.
    const QByteArray nameBytes = desc.name.toUtf8();
    if (!nameBytes.isEmpty())
        bodyDef.name = nameBytes.constData();

    const b2BodyId bodyId = b2CreateBody(m_worldId, &bodyDef);
    // The handle this body will be known by, stashed where a contact event
    // can find it: Box2D reports contacts as shape ids, and this is the only
    // way back from a shape's body to the number the editor uses.
    b2Body_SetUserData(bodyId, reinterpret_cast<void *>(
                                   static_cast<intptr_t>(m_bodies.size())));

    // Every part becomes its own Box2D shape on the one body. All of them
    // have to be representable -- a body that silently dropped one of its
    // pieces would collide differently from what's drawn.
    for (const ShapePart &part : desc.parts) {
        const QVariantMap &shape = part.params;
        b2ShapeDef shapeDef = b2DefaultShapeDef();
        shapeDef.density = static_cast<float>(std::max(0.0, number(shape, "density", 1.0)));
        shapeDef.material.friction =
            static_cast<float>(std::max(0.0, number(shape, "friction", 0.6)));
        shapeDef.material.restitution =
            static_cast<float>(std::max(0.0, number(shape, "restitution", 0.0)));
        shapeDef.material.rollingResistance =
            static_cast<float>(std::max(0.0, number(shape, "rollingResistance", 0.0)));
        shapeDef.material.tangentSpeed = static_cast<float>(number(shape, "tangentSpeed", 0.0));
        shapeDef.filter.categoryBits =
            static_cast<uint64_t>(std::max(0.0, number(shape, "categoryBits", 1.0)));
        shapeDef.filter.maskBits = static_cast<uint64_t>(
            std::max(0.0, number(shape, "maskBits", 9007199254740991.0)));
        shapeDef.filter.groupIndex = static_cast<int>(number(shape, "groupIndex", 0.0));
        shapeDef.isSensor = flag(shape, "isSensor", false);
        shapeDef.enableSensorEvents = flag(shape, "enableSensorEvents", false);
        shapeDef.enableContactEvents = flag(shape, "enableContactEvents", false);
        shapeDef.enableHitEvents = flag(shape, "enableHitEvents", false);
        shapeDef.enablePreSolveEvents = flag(shape, "enablePreSolveEvents", false);
        // A rule watches this shape. Box2D reports nothing about a shape that
        // did not ask, so whatever it takes is switched on here rather than by
        // an editor that would have to know these flags exist.
        if (part.watchedByRules) {
            shapeDef.enableContactEvents = true;
            shapeDef.enableHitEvents = true;
        }
        // The name travels in the shape's user data, so a contact event can
        // report which shape it was rather than only which body.
        m_shapeNames.append(part.name);
        shapeDef.userData = reinterpret_cast<void *>(
            static_cast<intptr_t>(m_shapeNames.size() - 1));

        if (!attachShape(bodyId, part, desc.type, shapeDef)) {
            b2DestroyBody(bodyId);
            return kInvalidBody;
        }
    }

    if (desc.parts.isEmpty()) {
        b2DestroyBody(bodyId);
        return kInvalidBody;
    }

    // Indexed by name so a rule can change a shape while the world runs.
    {
        b2ShapeId shapes[64];
        const int count = b2Body_GetShapes(bodyId, shapes, 64);
        for (int i = 0; i < count; ++i) {
            const auto index = static_cast<int>(
                reinterpret_cast<intptr_t>(b2Shape_GetUserData(shapes[i])));
            if (index >= 0 && index < m_shapeNames.size()
                && !m_shapeNames[index].isEmpty()) {
                m_shapesByName.insert(m_shapeNames[index], shapes[i]);
            }
        }
    }

    m_bodies.push_back(bodyId);
    return static_cast<BodyHandle>(m_bodies.size() - 1);
}

void Box2DEngine::step(qreal dt)
{
    if (!b2World_IsValid(m_worldId))
        return;

    // Kept because b2Body_SetTargetTransform asks how long it has to get
    // there, and a property write has no other way of knowing.
    m_lastStep = static_cast<float>(dt);

    b2World_Step(m_worldId, static_cast<float>(dt), m_subStepCount);
    collectWreckage();
    // After the solver, so a joint reports where it actually ended up rather
    // than where it was before the step that carried it into its limit.
    detectLimitEvents();
    collectContactEvents();
    collectBodyEvents();
}

void Box2DEngine::collectWreckage()
{
    // A body whose position or velocity is no longer a number cannot be
    // brought back: whatever it was doing is gone, and every contact it takes
    // part in spreads the damage. It is taken out of the world and named, once.
    for (size_t i = 0; i < m_bodies.size(); ++i) {
        const b2BodyId body = m_bodies[i];
        if (!b2Body_IsValid(body) || !b2Body_IsEnabled(body))
            continue;
        const b2Vec2 position = b2Body_GetPosition(body);
        const b2Vec2 velocity = b2Body_GetLinearVelocity(body);
        if (b2IsValidVec2(position) && b2IsValidVec2(velocity)
            && b2IsValidFloat(b2Body_GetAngularVelocity(body)))
            continue;

        const QString name = QString::fromUtf8(b2Body_GetName(body));
        b2Body_Disable(body);
        if (m_ruined.contains(name))
            continue;
        m_ruined.insert(name);
        m_problems.append(
            QObject::tr("%1 was thrown out of the world by the solver and has been taken"
                        " out of the run. A joint pulling far harder than what it holds"
                        " weighs is the usual cause.")
                .arg(name.isEmpty() ? QObject::tr("A body") : name));
    }

    if (!g_lastAssertion.isEmpty()) {
        m_problems.append(QObject::tr("Box2D could not finish a step: %1.")
                              .arg(g_lastAssertion));
        g_lastAssertion.clear();
    }
}

QStringList Box2DEngine::takeProblems()
{
    QStringList problems;
    problems.swap(m_problems);
    return problems;
}

BodyState Box2DEngine::bodyState(BodyHandle handle) const
{
    BodyState state;
    if (handle < 0 || handle >= static_cast<BodyHandle>(m_bodies.size()))
        return state;

    const b2BodyId bodyId = m_bodies[handle];
    // A rule may have removed it. The handle survives so the editor's indices
    // stay put, but there is nothing behind it any more.
    if (!b2Body_IsValid(bodyId)) {
        state.exists = false;
        return state;
    }
    state.position = toScene(b2Body_GetPosition(bodyId));
    state.rotationDegrees = qRadiansToDegrees(b2Rot_GetAngle(b2Body_GetRotation(bodyId)));
    state.centerOfMass = toScene(b2Body_GetWorldCenterOfMass(bodyId));
    state.awake = b2Body_IsAwake(bodyId);
    return state;
}

} // namespace physics
