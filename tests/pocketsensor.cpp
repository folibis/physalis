#include "CanvasScene.h"
#include "CircleItem.h"
#include "PhysicsBody.h"
#include "Rule.h"
#include "SimulationController.h"

#include <QApplication>
#include <gtest/gtest.h>

// A pocket: a sensor a rule watches, and a ball rolling into it. Box2D reports
// an overlap only when the sensor and the shape entering it both have sensor
// events switched on, and neither is by default -- so a rule on a sensor never
// fired unless every ball had been set up by hand. The engine switches them on
// itself, the way it does contact events for a shape a rule watches.

namespace {

// Rolls a ball through a sensor pocket that removes whatever enters it. The
// pocket is made first or last: the engine has to catch the shapes that already
// exist as well as the ones that come after.
bool pocketTakesTheBall(const QString &engineName, bool pocketFirst)
{
    CanvasScene scene;
    scene.setSimulationEngineName(engineName);
    scene.world().params["gravityY"] = 0.0;
    scene.setEditorMode(EditorMode::Physics);

    CircleItem *pocketShape = nullptr;
    CircleItem *ballShape = nullptr;
    if (pocketFirst) {
        pocketShape = scene.addCircle(QPointF(300, 0));
        ballShape = scene.addCircle(QPointF(0, 0));
    } else {
        ballShape = scene.addCircle(QPointF(0, 0));
        pocketShape = scene.addCircle(QPointF(300, 0));
    }
    pocketShape->setName(QStringLiteral("pocket"));
    ballShape->setName(QStringLiteral("ball"));
    scene.notifyShapesChanged();

    const auto bodyOf = [&scene](CircleItem *shape) {
        scene.selectForPhysics(shape, true);
        PhysicsBody *body = scene.createBodyFromSelection();
        scene.clearPhysicsSelection();
        return body;
    };
    PhysicsBody *first = bodyOf(pocketFirst ? pocketShape : ballShape);
    PhysicsBody *second = bodyOf(pocketFirst ? ballShape : pocketShape);
    PhysicsBody *pocket = pocketFirst ? first : second;
    PhysicsBody *ball = pocketFirst ? second : first;
    EXPECT_TRUE(pocket && ball);
    if (!pocket || !ball)
        return false;

    pocket->props().type = physics::BodyType::Static;
    pocketShape->part().params["isSensor"] = true;
    ball->props().params["velocityX"] = 4.0;   // about 200 px/s, through the pocket and out

    Rule sink;
    sink.subjectName = QStringLiteral("pocket");
    sink.eventId = QStringLiteral("sensorBegin");
    sink.targetName = Rule::otherObjectBody();
    sink.actionId = QStringLiteral("removeBody");
    scene.setRules({ sink });

    SimulationController sim(&scene, nullptr);
    sim.setEngineName(engineName);
    sim.start();
    for (int i = 0; i < 180; ++i)
        sim.stepFrame();
    const bool taken = ball->isRemoved();
    sim.stop();
    return taken;
}

} // namespace

TEST(PocketSensor, ARuleOnASensorSeesWhatEntersIt)
{
    // The same in every engine: a scene should not care which one it runs on.
    for (const QString &engine : { QStringLiteral("Box2D"), QStringLiteral("Chipmunk2D") }) {
        EXPECT_TRUE(pocketTakesTheBall(engine, true))
            << engine.toStdString() << ": no sensor-event flag set anywhere, and the pocket still takes the ball";
        EXPECT_TRUE(pocketTakesTheBall(engine, false))
            << engine.toStdString() << ": and it does when the ball existed before the pocket was made";
    }
}
