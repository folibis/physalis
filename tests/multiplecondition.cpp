#include "CanvasScene.h"
#include "CircleItem.h"
#include "PhysicsBody.h"
#include "Rule.h"
#include "SimulationController.h"

#include <gtest/gtest.h>

// "Is a multiple of": with 5 the condition holds at 5, 10, 15... and never at
// 0, so a rule on the frame fires on every fifth one.

TEST(MultipleCondition, FiresOnEveryMultipleAndNotAtZero)
{
    for (const QString &engine : {QStringLiteral("Box2D"), QStringLiteral("Chipmunk2D")}) {
        CanvasScene scene;
        scene.setSimulationEngineName(engine);
        scene.setEditorMode(EditorMode::Physics);
        CircleItem *shape = scene.addCircle(QPointF(0, 0));
        scene.notifyShapesChanged();
        scene.selectForPhysics(shape, true);
        ASSERT_TRUE(scene.createBodyFromSelection());
        scene.world().params["gravityX"] = 0.0;

        // Each firing adds one to the world's sideways gravity: a counter the
        // engine keeps.
        Rule rule;
        rule.subjectName = Rule::world();
        rule.conditionKey = QStringLiteral("frame");
        rule.compare = Rule::Compare::Multiple;
        rule.conditionValue = 5.0;
        rule.targetName = Rule::world();
        rule.propertyKey = QStringLiteral("gravityX");
        rule.op = Rule::Op::Add;
        rule.value = 1.0;
        scene.setRules({rule});

        SimulationController sim(&scene, nullptr);
        sim.setEngineName(engine);
        sim.start();
        for (int i = 0; i < 22; ++i)
            sim.stepFrame();
        const double fired = sim.readValue(Rule::world(), QStringLiteral("gravityX")).toDouble();
        sim.stop();

        EXPECT_NEAR(fired, 4.0, 1e-3) << engine.toStdString() << ": frames 5, 10, 15 and 20 in 22";
    }
}

TEST(MultipleCondition, IsSavedByItsOwnName)
{
    EXPECT_EQ(Rule::compareFromName(Rule::compareName(Rule::Compare::Multiple)), Rule::Compare::Multiple);
}
