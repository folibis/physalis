#include "EngineRegistry.h"
#include "IPhysicsEngine.h"

#include <gtest/gtest.h>
#include <cmath>

// The Chipmunk2D plugin, driven directly through IPhysicsEngine the way the
// simulation controller drives it. Every scene is built here in code.

using namespace physics;

namespace {

const QString kChipmunk = QStringLiteral("Chipmunk2D");
constexpr qreal kStep = 1.0 / 60.0;

std::unique_ptr<IPhysicsEngine> chipmunkWorld(QPointF gravity = QPointF(0.0, 9.81))
{
    auto engine = EngineRegistry::create(kChipmunk);
    if (engine) {
        WorldDesc world;
        world.params["gravityX"] = gravity.x();
        world.params["gravityY"] = gravity.y();
        engine->createWorld(world);
    }
    return engine;
}

ShapePart boxPart(const QString &name, qreal halfWidth, qreal halfHeight)
{
    ShapePart part;
    part.name = name;
    part.geometry.kind = GeometryKind::Box;
    part.geometry.halfExtents = QPointF(halfWidth, halfHeight);
    return part;
}

ShapePart circlePart(const QString &name, qreal radius)
{
    ShapePart part;
    part.name = name;
    part.geometry.kind = GeometryKind::Circle;
    part.geometry.radius = radius;
    return part;
}

BodyDesc bodyOf(const ShapePart &part, const QPointF &position, BodyType type)
{
    BodyDesc body;
    body.name = part.name + QStringLiteral("_body");
    body.position = position;
    body.type = type;
    body.parts.append(part);
    return body;
}

void run(IPhysicsEngine *engine, int steps, QVector<EngineEvent> *events = nullptr)
{
    for (int i = 0; i < steps; ++i) {
        engine->step(kStep);
        const QVector<EngineEvent> raised = engine->pollEvents();
        if (events)
            *events += raised;
    }
}

bool raised(const QVector<EngineEvent> &events, const QString &id, const QString &subject,
            const QString &other = QString())
{
    for (const EngineEvent &event : events) {
        if (event.eventId == id && event.subjectShape == subject
            && (other.isEmpty() || event.otherShape == other))
            return true;
    }
    return false;
}

} // namespace

TEST(Chipmunk, IsFoundBesideTheExecutable)
{
    EXPECT_TRUE(EngineRegistry::availableEngines().contains(kChipmunk))
        << "the plugin is discovered";
    // Box2D's file sorts first, so it stays the engine a new scene starts with.
    EXPECT_EQ(EngineRegistry::availableEngines().value(0), QStringLiteral("Box2D"))
        << "Box2D is still the default engine";

    auto engine = EngineRegistry::create(kChipmunk);
    ASSERT_TRUE(engine) << "the plugin hands out an engine";
    EXPECT_EQ(engine->name(), kChipmunk);
    EXPECT_FALSE(engine->jointTypes().isEmpty()) << "and declares its joint types";

    bool versioned = false;
    for (const auto &plugin : EngineRegistry::loadedPlugins())
        versioned = versioned || (plugin.name == kChipmunk && plugin.version.startsWith('7'));
    EXPECT_TRUE(versioned) << "it reports Chipmunk's own version";
}

TEST(Chipmunk, BoxFallsAndLandsOnTheFloor)
{
    auto engine = chipmunkWorld();
    ASSERT_TRUE(engine);

    // The floor's top edge is at y = 180; the box is 40 tall.
    const BodyHandle floor = engine->addBody(bodyOf(boxPart("floor", 300, 20), QPointF(0, 200),
                                                    BodyType::Static));
    ShapePart crate = boxPart("crate", 20, 20);
    crate.params["enableContactEvents"] = true;
    const BodyHandle box = engine->addBody(bodyOf(crate, QPointF(0, 0), BodyType::Dynamic));
    ASSERT_NE(floor, kInvalidBody);
    ASSERT_NE(box, kInvalidBody);

    QVector<EngineEvent> events;
    run(engine.get(), 240, &events);

    const BodyState state = engine->bodyState(box);
    EXPECT_NEAR(state.position.y(), 160.0, 1.5) << "it rests on the floor, not in or above it";
    EXPECT_NEAR(state.position.x(), 0.0, 0.5) << "and did not slide";
    EXPECT_TRUE(raised(events, "contactBegin", "crate", "floor")) << "the landing was reported";
    EXPECT_GT(engine->bodyValue(box, "mass").toDouble(), 0.0) << "density gave it mass";
}

TEST(Chipmunk, SensorReportsEntryAndExit)
{
    auto engine = chipmunkWorld();
    ASSERT_TRUE(engine);

    ShapePart gate = boxPart("gate", 50, 50);
    gate.params["isSensor"] = true;
    engine->addBody(bodyOf(gate, QPointF(0, 100), BodyType::Static));

    ShapePart ball = circlePart("ball", 10);
    ball.params["enableSensorEvents"] = true;
    engine->addBody(bodyOf(ball, QPointF(0, 0), BodyType::Dynamic));

    QVector<EngineEvent> events;
    bool sawInside = false;
    for (int i = 0; i < 120; ++i) {
        run(engine.get(), 1, &events);
        sawInside = sawInside || engine->shapeValue("gate", "sensorOverlapCount").toInt() == 1;
    }
    EXPECT_TRUE(raised(events, "sensorBegin", "gate", "ball")) << "entering was reported";
    EXPECT_TRUE(raised(events, "sensorEnd", "gate", "ball")) << "and so was leaving";
    EXPECT_TRUE(sawInside) << "the sensor counted it while it was inside";
    EXPECT_EQ(engine->shapeValue("gate", "sensorOverlapCount").toInt(), 0)
        << "and nothing once it had gone";
}

TEST(Chipmunk, EveryJointTypeHoldsWithoutBlowingUp)
{
    const auto types = EngineRegistry::create(kChipmunk)->jointTypes();
    for (const JointType &type : types) {
        auto engine = chipmunkWorld();
        ASSERT_TRUE(engine);
        const BodyHandle anchor = engine->addBody(
            bodyOf(boxPart("post", 10, 10), QPointF(0, 0), BodyType::Static));
        const BodyHandle swing = engine->addBody(
            bodyOf(boxPart("weight", 10, 10), QPointF(60, 0), BodyType::Dynamic));

        JointDesc joint;
        joint.typeId = type.id;
        joint.params = type.defaultValues();
        if (type.bodyCount == 1) {
            joint.bodyA = swing;
        } else {
            joint.bodyA = anchor;
            joint.bodyB = swing;
        }
        const QVector<QPointF> ends { QPointF(0, 0), QPointF(60, 0) };
        joint.anchors = ends.mid(0, type.anchorCount);

        const JointHandle handle = engine->addJoint(joint);
        EXPECT_NE(handle, kInvalidJoint) << "made a " << type.id.toStdString();

        // Chipmunk's constraints are soft: a swinging weight stretches a pin a
        // little, by a pixel or so at the bottom of the swing. What must not
        // happen is drift, so the worst stretch over ten seconds is measured.
        qreal worstStretch = 0.0;
        for (int i = 0; i < 600; ++i) {
            run(engine.get(), 1);
            if (type.id == QLatin1String("pin")) {
                worstStretch = qMax(worstStretch,
                                    qAbs(engine->jointValue(handle, "currentLength").toDouble() - 60.0));
            }
        }
        const BodyState state = engine->bodyState(swing);
        EXPECT_TRUE(std::isfinite(state.position.x()) && std::isfinite(state.position.y()))
            << type.id.toStdString() << " left the body somewhere real";
        if (type.id == QLatin1String("pin"))
            EXPECT_LT(worstStretch, 2.0) << "a pin holds its length while the weight swings";
    }
}

TEST(Chipmunk, SlideJointRaisesItsLimit)
{
    auto engine = chipmunkWorld();
    ASSERT_TRUE(engine);
    const BodyHandle hook = engine->addBody(
        bodyOf(boxPart("hook", 5, 5), QPointF(0, 0), BodyType::Static));
    const BodyHandle ball = engine->addBody(
        bodyOf(circlePart("ball", 10), QPointF(0, 50), BodyType::Dynamic));

    JointDesc rope;
    rope.typeId = QStringLiteral("slide");
    rope.bodyA = hook;
    rope.bodyB = ball;
    rope.anchors = { QPointF(0, 0), QPointF(0, 50) };
    rope.params = { { "minLength", 0.0 }, { "maxLength", 100.0 } };
    const JointHandle handle = engine->addJoint(rope);
    ASSERT_NE(handle, kInvalidJoint);

    QVector<EngineEvent> events;
    run(engine.get(), 90, &events);

    int upper = 0;
    for (const EngineEvent &event : events)
        upper += event.joint == handle && event.eventId == QLatin1String("limitUpper");
    EXPECT_EQ(upper, 1) << "the rope going taut is one arrival, not one per step";
    EXPECT_NEAR(engine->bodyState(ball).position.y(), 100.0, 2.0) << "and it holds there";
    EXPECT_NEAR(engine->jointValue(handle, "maxLength").toDouble(), 100.0, 1e-6);
}

TEST(Chipmunk, RayFindsTheNearestShapeItMaySee)
{
    auto engine = chipmunkWorld(QPointF());
    ASSERT_TRUE(engine);

    ShapePart nearPart = boxPart("near", 20, 20);
    nearPart.params["categoryBits"] = 2;
    engine->addBody(bodyOf(nearPart, QPointF(100, 0), BodyType::Static));
    engine->addBody(bodyOf(boxPart("far", 20, 20), QPointF(200, 0), BodyType::Static));
    engine->step(kStep);

    RayHit hit = engine->castRay(QPointF(0, 0), QPointF(300, 0), ~quint64(0));
    ASSERT_TRUE(hit.hit);
    EXPECT_EQ(hit.shapeName, QStringLiteral("near"));
    EXPECT_NEAR(hit.distance, 80.0, 0.01) << "measured to the face it struck";
    EXPECT_NEAR(hit.normal.x(), -1.0, 1e-6) << "which faces back along the ray";

    hit = engine->castRay(QPointF(0, 0), QPointF(300, 0), 1);
    ASSERT_TRUE(hit.hit);
    EXPECT_EQ(hit.shapeName, QStringLiteral("far")) << "the mask looks past the other group";

    hit = engine->castRay(QPointF(100, 0), QPointF(300, 0), ~quint64(0));
    EXPECT_EQ(hit.shapeName, QStringLiteral("far")) << "a ray starting inside a shape ignores it";

    EXPECT_FALSE(engine->castRay(QPointF(0, 100), QPointF(300, 0), ~quint64(0)).hit)
        << "and one that passes everything finds nothing";
}

TEST(Chipmunk, ExplosionPushesThingsAway)
{
    auto engine = chipmunkWorld(QPointF());
    ASSERT_TRUE(engine);
    const BodyHandle right = engine->addBody(
        bodyOf(circlePart("right", 10), QPointF(60, 0), BodyType::Dynamic));
    const BodyHandle left = engine->addBody(
        bodyOf(circlePart("left", 10), QPointF(-60, 0), BodyType::Dynamic));
    const BodyHandle far = engine->addBody(
        bodyOf(circlePart("far", 10), QPointF(1000, 0), BodyType::Dynamic));

    engine->performActionAt(QStringLiteral("explode"), QPointF(0, 0),
                            { { "impulse", 3.0 }, { "radius", 200.0 }, { "falloff", 100.0 } });
    engine->step(kStep);

    EXPECT_GT(engine->bodyValue(right, "velocityX").toDouble(), 1.0) << "pushed right";
    EXPECT_LT(engine->bodyValue(left, "velocityX").toDouble(), -1.0) << "pushed left";
    EXPECT_DOUBLE_EQ(engine->bodyValue(far, "velocityX").toDouble(), 0.0) << "out of reach";
}

TEST(Chipmunk, RemovedBodyIsGoneAndStepsGoOn)
{
    auto engine = chipmunkWorld();
    ASSERT_TRUE(engine);
    const BodyHandle floor = engine->addBody(
        bodyOf(boxPart("floor", 300, 20), QPointF(0, 60), BodyType::Static));
    ShapePart crate = boxPart("crate", 20, 20);
    crate.params["enableContactEvents"] = true;
    const BodyHandle box = engine->addBody(bodyOf(crate, QPointF(0, 20), BodyType::Dynamic));
    const BodyHandle other = engine->addBody(
        bodyOf(boxPart("other", 20, 20), QPointF(100, 0), BodyType::Dynamic));

    JointDesc pin;
    pin.typeId = QStringLiteral("pivot");
    pin.bodyA = box;
    pin.bodyB = other;
    pin.anchors = { QPointF(50, 0) };
    const JointHandle joint = engine->addJoint(pin);
    ASSERT_NE(joint, kInvalidJoint);

    run(engine.get(), 30);
    engine->performAction(QStringLiteral("removeBody"), box, {});
    QVector<EngineEvent> events;
    run(engine.get(), 30, &events);

    EXPECT_FALSE(engine->bodyState(box).exists) << "the body is gone";
    EXPECT_TRUE(engine->bodyState(floor).exists) << "what it stood on is not";
    EXPECT_FALSE(engine->bodyValue(box, "positionX").isValid()) << "nothing answers for it";
    EXPECT_FALSE(engine->jointValue(joint, "constraintForce").isValid()) << "its joint went too";
    EXPECT_FALSE(raised(events, "contactEnd", "crate")) << "no contact reported ending on it";
}

TEST(Chipmunk, ConcavePolygonIsSolid)
{
    auto engine = chipmunkWorld();
    ASSERT_TRUE(engine);
    engine->addBody(bodyOf(boxPart("floor", 300, 20), QPointF(0, 200), BodyType::Static));

    // An L, which Box2D could only have as an outline with no mass.
    ShapePart ell;
    ell.name = QStringLiteral("ell");
    ell.geometry.kind = GeometryKind::Polygon;
    ell.geometry.points = { QPointF(0, 0), QPointF(60, 0), QPointF(60, 20),
                            QPointF(20, 20), QPointF(20, 60), QPointF(0, 60) };
    const BodyHandle body = engine->addBody(bodyOf(ell, QPointF(0, 0), BodyType::Dynamic));
    ASSERT_NE(body, kInvalidBody) << "a concave outline makes a dynamic body";

    const double expectedArea = 60 * 20 + 20 * 40;
    EXPECT_NEAR(engine->shapeValue("ell", "area").toDouble(), expectedArea, 1.0)
        << "its pieces cover the whole outline, once";

    run(engine.get(), 240);
    const double lowest = engine->bodyValue(body, "positionY").toDouble();
    EXPECT_GT(lowest, 100.0) << "it fell";
    EXPECT_LT(lowest, 180.0) << "and landed on the floor rather than through it";
}

TEST(Chipmunk, MouseJointLeadsTheBody)
{
    auto engine = chipmunkWorld(QPointF());
    ASSERT_TRUE(engine);
    const BodyHandle ball = engine->addBody(
        bodyOf(circlePart("ball", 20), QPointF(0, 0), BodyType::Dynamic));

    JointDesc mouse;
    mouse.typeId = QStringLiteral("mouse");
    mouse.bodyA = ball;
    mouse.anchors = { QPointF(0, 0), QPointF(200, 0) };
    const JointHandle handle = engine->addJoint(mouse);
    ASSERT_NE(handle, kInvalidJoint);

    run(engine.get(), 120);
    EXPECT_NEAR(engine->bodyState(ball).position.x(), 200.0, 5.0) << "pulled to the target";

    engine->setJointParam(handle, QStringLiteral("targetY"), 150.0);
    run(engine.get(), 120);
    EXPECT_NEAR(engine->bodyState(ball).position.y(), 150.0, 5.0) << "and follows it when it moves";
    EXPECT_NEAR(engine->jointValue(handle, "targetY").toDouble(), 150.0, 1e-6);
}

TEST(Chipmunk, DisabledBodyWaitsAndResumes)
{
    auto engine = chipmunkWorld();
    ASSERT_TRUE(engine);
    const BodyHandle ball = engine->addBody(
        bodyOf(circlePart("ball", 10), QPointF(0, 0), BodyType::Dynamic));

    engine->setBodyParam(ball, QStringLiteral("isEnabled"), false);
    run(engine.get(), 30);
    EXPECT_DOUBLE_EQ(engine->bodyState(ball).position.y(), 0.0) << "nothing moves it while off";
    EXPECT_FALSE(engine->bodyValue(ball, "isEnabled").toBool());

    engine->setBodyParam(ball, QStringLiteral("isEnabled"), true);
    run(engine.get(), 30);
    EXPECT_GT(engine->bodyState(ball).position.y(), 10.0) << "and it falls once back on";
}

TEST(Chipmunk, NegativeGroupPassesThrough)
{
    auto engine = chipmunkWorld();
    ASSERT_TRUE(engine);
    ShapePart floor = boxPart("floor", 300, 20);
    floor.params["groupIndex"] = -1;
    engine->addBody(bodyOf(floor, QPointF(0, 60), BodyType::Static));
    ShapePart ghost = circlePart("ghost", 10);
    ghost.params["groupIndex"] = -1;
    const BodyHandle ball = engine->addBody(bodyOf(ghost, QPointF(0, 0), BodyType::Dynamic));

    run(engine.get(), 120);
    EXPECT_GT(engine->bodyState(ball).position.y(), 200.0)
        << "a shared negative group never collides, as in Box2D";
}

TEST(Chipmunk, WorldAnswersAndChanges)
{
    auto engine = chipmunkWorld();
    ASSERT_TRUE(engine);
    const BodyHandle ball = engine->addBody(
        bodyOf(circlePart("ball", 10), QPointF(0, 0), BodyType::Dynamic));

    EXPECT_NEAR(engine->worldValue("gravityY").toDouble(), 9.81, 1e-9)
        << "gravity reads back in the units it was given in";
    EXPECT_EQ(engine->worldValue("bodyCount").toInt(), 1);
    EXPECT_NEAR(engine->worldValue("collisionSlop").toDouble(), 0.25, 1e-9)
        << "the slop is a quarter of a scene unit at any scale";

    engine->setWorldParam(QStringLiteral("gravityY"), -9.81);
    run(engine.get(), 30);
    EXPECT_LT(engine->bodyState(ball).position.y(), -10.0) << "reversed gravity lifts it";
}
