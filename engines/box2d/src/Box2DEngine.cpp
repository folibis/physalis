#include "Box2DEngine.h"

#include <QByteArray>
#include <QtMath>
#include <algorithm>

namespace physics {

namespace {

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

    b2WorldDef worldDef = b2DefaultWorldDef();
    // Box2D already works in meters and seconds, so an m/s^2 acceleration
    // needs no conversion -- unlike the geometry, which is in scene units.
    worldDef.gravity = b2Vec2 { static_cast<float>(desc.gravity.x() * m_motionScale),
                                static_cast<float>(desc.gravity.y() * m_motionScale) };

    // Box2D's own tuning thresholds are speeds in m/s, so they have to move
    // with the scale as well -- otherwise a small scene trips every one of
    // them at once and a large scene trips none.
    worldDef.maximumLinearSpeed = static_cast<float>(desc.maximumLinearSpeed * m_motionScale);
    worldDef.maxContactPushSpeed = static_cast<float>(desc.maxContactPushSpeed * m_motionScale);
    worldDef.restitutionThreshold = static_cast<float>(desc.restitutionThreshold * m_motionScale);
    worldDef.hitEventThreshold = static_cast<float>(desc.hitEventThreshold * m_motionScale);

    // Stiffness and damping are not speeds, so the scale leaves them alone.
    worldDef.contactHertz = static_cast<float>(desc.contactHertz);
    worldDef.contactDampingRatio = static_cast<float>(desc.contactDampingRatio);

    worldDef.enableSleep = desc.enableSleep;
    worldDef.enableContinuous = desc.enableContinuous;
    m_worldId = b2CreateWorld(&worldDef);
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

    b2BodyDef bodyDef = b2DefaultBodyDef();
    bodyDef.type = toB2BodyType(desc.type);
    bodyDef.position = toMeters(desc.position);
    bodyDef.rotation = b2MakeRot(static_cast<float>(qDegreesToRadians(desc.rotationDegrees)));
    // Same reasoning as gravity: quoted at the reference scale.
    bodyDef.linearVelocity = b2Vec2 { static_cast<float>(desc.linearVelocity.x() * m_motionScale),
                                      static_cast<float>(desc.linearVelocity.y() * m_motionScale) };
    bodyDef.angularVelocity = static_cast<float>(qDegreesToRadians(desc.angularVelocityDegrees));
    bodyDef.linearDamping = static_cast<float>(desc.linearDamping);
    bodyDef.angularDamping = static_cast<float>(desc.angularDamping);
    bodyDef.gravityScale = static_cast<float>(desc.gravityScale);
    bodyDef.enableSleep = desc.enableSleep;
    bodyDef.isAwake = desc.isAwake;
    // A speed, so quoted at the reference scale like gravity and velocity.
    // Without this a small-scale scene puts bodies to sleep in mid-air: the
    // default 0.05 m/s threshold is never exceeded when the whole world is
    // only centimetres across, so everything "stops moving" while falling.
    bodyDef.sleepThreshold = static_cast<float>(desc.sleepThreshold * m_motionScale);
    bodyDef.fixedRotation = desc.fixedRotation;
    bodyDef.isBullet = desc.isBullet;
    bodyDef.allowFastRotation = desc.allowFastRotation;
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
        b2ShapeDef shapeDef = b2DefaultShapeDef();
        shapeDef.density = static_cast<float>(std::max(0.0, part.density));
        shapeDef.material.friction = static_cast<float>(std::max(0.0, part.material.friction));
        shapeDef.material.restitution = static_cast<float>(std::max(0.0, part.material.restitution));
        shapeDef.material.rollingResistance = static_cast<float>(std::max(0.0, part.material.rollingResistance));
        shapeDef.material.tangentSpeed = static_cast<float>(part.material.tangentSpeed);
        shapeDef.filter.categoryBits = part.filter.categoryBits;
        shapeDef.filter.maskBits = part.filter.maskBits;
        shapeDef.filter.groupIndex = part.filter.groupIndex;
        shapeDef.isSensor = part.isSensor;
        shapeDef.enableSensorEvents = part.enableSensorEvents;
        shapeDef.enableContactEvents = part.enableContactEvents;
        shapeDef.enableHitEvents = part.enableHitEvents;
        shapeDef.enablePreSolveEvents = part.enablePreSolveEvents;
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

    b2World_Step(m_worldId, static_cast<float>(dt), kSubStepCount);
    // After the solver, so a joint reports where it actually ended up rather
    // than where it was before the step that carried it into its limit.
    detectLimitEvents();
    collectContactEvents();
}

BodyState Box2DEngine::bodyState(BodyHandle handle) const
{
    BodyState state;
    if (handle < 0 || handle >= static_cast<BodyHandle>(m_bodies.size()))
        return state;

    const b2BodyId bodyId = m_bodies[handle];
    state.position = toScene(b2Body_GetPosition(bodyId));
    state.rotationDegrees = qRadiansToDegrees(b2Rot_GetAngle(b2Body_GetRotation(bodyId)));
    state.centerOfMass = toScene(b2Body_GetWorldCenterOfMass(bodyId));
    state.awake = b2Body_IsAwake(bodyId);
    return state;
}

} // namespace physics
