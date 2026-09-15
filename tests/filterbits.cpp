#include "EngineRegistry.h"
#include "IPhysicsEngine.h"

#include <gtest/gtest.h>

// A scene saved when the default mask was "every bit set" carries
// 18446744073709552000 -- what 2^64-1 becomes after a trip through a JSON
// number, which is a double. It is past the top of a uint64, and casting it
// over gave a mask of zero: a shape that collides with nothing, and a car that
// falls through the floor.

using namespace physics;

namespace {

// Exactly what QJsonValue hands back for the number in such a file.
constexpr double kRoundedAllBits = 18446744073709552000.0;

BodyDesc block(const QString &name, const QPointF &at, BodyType type, qreal halfWidth,
               qreal halfHeight, double mask)
{
    ShapePart part;
    part.name = name;
    part.geometry.kind = GeometryKind::Box;
    part.geometry.halfExtents = QPointF(halfWidth, halfHeight);
    part.params["maskBits"] = mask;
    part.params["categoryBits"] = 1.0;

    BodyDesc body;
    body.name = name;
    body.position = at;
    body.type = type;
    body.parts.append(part);
    return body;
}

} // namespace

TEST(FilterBits, AMaskTooBigForItsTypeStillMeansEverything)
{
    for (const QString &name : EngineRegistry::availableEngines()) {
        auto engine = EngineRegistry::create(name);
        ASSERT_TRUE(engine);
        WorldDesc world;
        world.params["gravityY"] = 9.81;
        engine->createWorld(world);

        engine->addBody(block("floor", QPointF(0, 300), BodyType::Static, 400, 20,
                              kRoundedAllBits));
        const BodyHandle car =
            engine->addBody(block("car", QPointF(0, 0), BodyType::Dynamic, 30, 20,
                                  kRoundedAllBits));
        ASSERT_NE(car, kInvalidBody);

        for (int i = 0; i < 240; ++i)
            engine->step(1.0 / 60.0);

        // The floor's top edge is at y = 280 and the car is 40 tall, so it
        // rests with its centre at 260.
        const qreal y = engine->bodyState(car).position.y();
        EXPECT_LT(y, 275.0) << name.toStdString() << ": the car went through the floor -- it"
                               " stopped reading its mask as every bit and read it as none";
        EXPECT_GT(y, 245.0) << name.toStdString() << ": and it is resting on the floor, not"
                               " hovering above it";
    }
}
