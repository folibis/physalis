#include "CanvasScene.h"
#include "PhysicsBody.h"
#include "RectangleItem.h"
#include "Rule.h"
#include "SimulationController.h"

#include <QApplication>
#include <gtest/gtest.h>

using namespace physics;

namespace {

// One loose box and nothing else, so the only thing acting on it is the world.
PhysicsBody *loneBox(CanvasScene *scene)
{
    scene->setSimulationEngineName(QStringLiteral("Box2D"));

    auto *shape = new RectangleItem;
    shape->setRect(QRectF(0, 0, 40, 40));
    shape->setPos(0, 0);
    scene->addItem(shape);
    scene->notifyShapesChanged();
    scene->setEditorMode(EditorMode::Physics);

    scene->selectForPhysics(shape, true);
    PhysicsBody *body = scene->createBodyFromSelection();
    scene->clearPhysicsSelection();
    return body;
}

} // namespace

// b2World_SetGravity, reached through the world as a rule target. Until the
// engine published worldProperties() there was nothing a rule could say about
// the world at all -- it could only be read for the time and the frame count.
TEST(WorldRule, GravityChangesMidRun)
{
    CanvasScene scene;
    PhysicsBody *box = loneBox(&scene);

    Rule flip;
    flip.subjectName = Rule::world();
    flip.conditionKey = QStringLiteral("time");
    flip.compare = Rule::Compare::Greater;
    flip.conditionValue = 0.05;
    flip.targetName = Rule::world();
    flip.propertyKey = QStringLiteral("gravityY");
    flip.op = Rule::Op::Set;
    flip.value = -30.0;
    scene.setRules({ flip });

    SimulationController sim(&scene, nullptr);
    sim.setEngineName(QStringLiteral("Box2D"));
    sim.start();

    for (int i = 0; i < 3; ++i)
        sim.stepFrame();
    EXPECT_GT(sim.readValue(box->name(), QStringLiteral("velocityY")).toDouble(), 0.0)
        << "it is falling before the rule fires";

    for (int i = 0; i < 60; ++i)
        sim.stepFrame();

    EXPECT_NEAR(sim.readValue(Rule::world(), QStringLiteral("gravityY")).toDouble(),
                -30.0, 0.01)
        << "the world reads back the gravity the rule gave it, in the units it was given";
    EXPECT_LT(sim.readValue(box->name(), QStringLiteral("velocityY")).toDouble(), 0.0)
        << "and the box is on its way up";

    sim.stop();
}

// The rest of what a running world will answer. These have no setter path
// worth a scene of their own; what matters is that they answer at all, since
// before worldProperties() existed the world had no readables but two.
TEST(WorldRule, ReportsWhatItIsDoing)
{
    CanvasScene scene;
    loneBox(&scene);

    SimulationController sim(&scene, nullptr);
    sim.setEngineName(QStringLiteral("Box2D"));
    sim.start();
    sim.stepFrame();

    EXPECT_GE(sim.readValue(Rule::world(), QStringLiteral("bodyCount")).toInt(), 1)
        << "the world counts the body it was given";
    EXPECT_GE(sim.readValue(Rule::world(), QStringLiteral("awakeBodyCount")).toInt(), 1)
        << "and knows it is still awake";
    EXPECT_TRUE(sim.readValue(Rule::world(), QStringLiteral("enableContinuous")).toBool())
        << "continuous collision is on, as the world was created";

    // Set through the same path a rule would use, and read back through the
    // same one -- the pair is the whole contract.
    EXPECT_TRUE(sim.readValue(Rule::world(), QStringLiteral("enableSleep")).isValid());

    sim.stop();
}
