#include "EngineRegistry.h"
#include "IPhysicsEngine.h"

#include <gtest/gtest.h>

// Travel, and what happens when a joint is asked for the impossible.

using namespace physics;

namespace {

constexpr qreal kStep = 1.0 / 60.0;

ShapePart boxPart(const QString &name, qreal halfWidth, qreal halfHeight)
{
    ShapePart part;
    part.name = name;
    part.geometry.kind = GeometryKind::Box;
    part.geometry.halfExtents = QPointF(halfWidth, halfHeight);
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

} // namespace

// Box2D measures a prismatic joint between its two anchors, so a joint whose
// anchors are 300 apart starts at 300 -- and limits written as "0 to 100"
// would sit behind it, with the motor driving against a limit it started the
// wrong side of. The editor means travel from where the joint starts, and so
// does the engine now.
TEST(JointTravel, LimitsAreMeasuredFromWhereTheJointStarts)
{
    auto engine = EngineRegistry::create(QStringLiteral("Box2D"));
    ASSERT_TRUE(engine);
    WorldDesc world;
    world.params["gravityY"] = 0.0;   // the motor is the only thing moving it
    engine->createWorld(world);

    const BodyHandle post = engine->addBody(
        bodyOf(boxPart("post", 20, 20), QPointF(0, 0), BodyType::Static));
    const BodyHandle plate = engine->addBody(
        bodyOf(boxPart("plate", 30, 20), QPointF(300, 0), BodyType::Dynamic));
    ASSERT_NE(post, kInvalidBody);
    ASSERT_NE(plate, kInvalidBody);

    JointDesc slider;
    slider.typeId = QStringLiteral("prismatic");
    slider.bodyA = post;
    slider.bodyB = plate;
    // Three hundred units apart, which is where the joint starts.
    slider.anchors = { QPointF(0, 0), QPointF(300, 0) };
    slider.axis = QPointF(1, 0);
    slider.params = { { "enableLimit", true },
                      { "lowerTranslation", 0.0 },
                      { "upperTranslation", 100.0 },
                      { "enableMotor", true },
                      { "motorSpeed", 400.0 },
                      { "maxMotorForce", 0.05 } };
    const JointHandle handle = engine->addJoint(slider);
    ASSERT_NE(handle, kInvalidJoint);

    EXPECT_NEAR(engine->jointValue(handle, "translation").toDouble(), 0.0, 1.0)
        << "a joint reads as no travel at all until something moves";

    for (int i = 0; i < 120; ++i)
        engine->step(kStep);

    const double travelled = engine->jointValue(handle, "translation").toDouble();
    EXPECT_NEAR(travelled, 100.0, 6.0)
        << "the motor drove it to the far limit, 100 units of travel from where it"
           " started -- read " << travelled;
    EXPECT_NEAR(engine->bodyState(plate).position.x(), 400.0, 6.0)
        << "which is 300 plus the travel, in the scene";
    EXPECT_TRUE(engine->takeProblems().isEmpty()) << "and nothing went wrong doing it";
}

// A scene can ask for the impossible: a motor pushing a thousand times what
// the thing it drives weighs, into a limit it is already past. Box2D notices
// the arithmetic going wrong and, left to itself, ends the process -- which
// used to take the editor with it, mid-run, with no explanation.
TEST(JointTravel, AnImpossibleJointIsReportedRatherThanFatal)
{
    auto engine = EngineRegistry::create(QStringLiteral("Box2D"));
    ASSERT_TRUE(engine);
    WorldDesc world;
    engine->createWorld(world);

    const BodyHandle rail = engine->addBody(
        bodyOf(boxPart("rail", 20, 200), QPointF(0, 0), BodyType::Static));
    const BodyHandle car = engine->addBody(
        bodyOf(boxPart("car", 25, 20), QPointF(400, 300), BodyType::Dynamic));
    const BodyHandle wall = engine->addBody(
        bodyOf(boxPart("wall", 300, 20), QPointF(400, 400), BodyType::Static));
    ASSERT_NE(car, kInvalidBody);
    ASSERT_NE(wall, kInvalidBody);

    JointDesc lift;
    lift.typeId = QStringLiteral("prismatic");
    lift.bodyA = car;    // the moving one first, as a scene may well have it
    lift.bodyB = rail;
    lift.anchors = { QPointF(400, 300), QPointF(0, 0) };
    lift.axis = QPointF(-0.8, -0.6);
    lift.params = { { "enableLimit", true },
                    { "lowerTranslation", 0.0 },
                    { "upperTranslation", 400.0 },
                    { "enableMotor", true },
                    { "motorSpeed", 900.0 },
                    // Newtons, against a body weighing about a gram.
                    { "maxMotorForce", 50.0 } };
    ASSERT_NE(engine->addJoint(lift), kInvalidJoint);

    // The point of the test: this returns. Before, it did not.
    for (int i = 0; i < 600; ++i)
        engine->step(kStep);

    const QStringList problems = engine->takeProblems();
    ASSERT_FALSE(problems.isEmpty()) << "the run said what went wrong instead of ending";
    EXPECT_TRUE(problems.join(QLatin1Char('|')).contains(QStringLiteral("car_body")))
        << "and named the body it happened to -- " << problems.join(QLatin1Char('|')).toStdString();
    EXPECT_TRUE(engine->takeProblems().isEmpty()) << "asking clears them";

    // The wreck is out of the world, so the rest of the run is undisturbed and
    // the same complaint is not made again every step.
    for (int i = 0; i < 60; ++i)
        engine->step(kStep);
    EXPECT_TRUE(engine->takeProblems().isEmpty()) << "a body is named once, not once a step";
}
