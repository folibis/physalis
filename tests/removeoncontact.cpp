#include "CanvasScene.h"
#include "PhysicsBody.h"
#include "CircleItem.h"
#include "RectangleItem.h"
#include "Rule.h"
#include "Joint.h"
#include "ShapeItem.h"
#include "SimulationController.h"

#include <QApplication>
#include <gtest/gtest.h>

using namespace physics;

// A ball dropped onto a floor, removed the moment it lands.
//
// Destroying a body leaves Box2D holding contacts that name its shapes, and it
// reports them ended on the following step -- with shape ids that no longer
// stand for anything. Box2D says as much on b2ContactEndTouchEvent and the
// sensor events were already checking for it; the contact ones were not, and
// asking a destroyed shape which body it belongs to took the application down.
// Nothing could destroy a body mid-run before, so nothing had ever asked.
TEST(RemoveOnContact, SurvivesTheContactEndingAfterwards)
{
    CanvasScene scene;
    scene.setSimulationEngineName(QStringLiteral("Box2D"));

    auto *ballShape = new CircleItem;
    ballShape->setRect(QRectF(0, 0, 60, 60));
    ballShape->setPos(0, 0);
    ballShape->setName(QStringLiteral("ball"));
    auto *floorShape = new RectangleItem;
    floorShape->setRect(QRectF(0, 0, 600, 40));
    floorShape->setPos(-300, 300);
    floorShape->setName(QStringLiteral("floor"));
    scene.addItem(ballShape);
    scene.addItem(floorShape);
    scene.notifyShapesChanged();
    scene.setEditorMode(EditorMode::Physics);

    scene.selectForPhysics(ballShape, true);
    PhysicsBody *ball = scene.createBodyFromSelection();
    scene.clearPhysicsSelection();

    scene.selectForPhysics(floorShape, true);
    scene.createBodyFromSelection()->props().type = BodyType::Static;
    scene.clearPhysicsSelection();

    Rule vanish;
    vanish.subjectName = floorShape->name();
    vanish.eventId = QStringLiteral("contactBegin");
    vanish.conditionValue = ballShape->name();
    vanish.targetName = ball->name();
    vanish.actionId = QStringLiteral("removeBody");
    scene.setRules({ vanish });

    SimulationController sim(&scene, nullptr);
    sim.setEngineName(QStringLiteral("Box2D"));
    sim.start();

    // Long enough to land, be removed, and go on stepping well past the step
    // on which the contact it left behind is reported ended.
    for (int i = 0; i < 240; ++i)
        sim.stepFrame();

    EXPECT_FALSE(ballShape->isVisible()) << "the ball was removed when it landed";
    EXPECT_TRUE(ball->isRemoved())
        << "and the body is marked, so nothing drawn about it -- its axes -- is drawn";
    sim.stop();
    EXPECT_TRUE(ballShape->isVisible()) << "and the scene has it back";
    EXPECT_FALSE(ball->isRemoved());
}

// A body's joints go into the world with it and come out of the world with it:
// Box2D destroys them alongside. Nothing in the scene knows that, so a joint
// left attached to a body that has just vanished went on being drawn between
// it and thin air.
TEST(RemoveOnContact, TakesItsJointsWithIt)
{
    CanvasScene scene;
    scene.setSimulationEngineName(QStringLiteral("Box2D"));

    auto *hookShape = new RectangleItem;
    hookShape->setRect(QRectF(0, 0, 40, 40));
    hookShape->setPos(0, 0);
    hookShape->setName(QStringLiteral("hook"));
    auto *weightShape = new RectangleItem;
    weightShape->setRect(QRectF(0, 0, 40, 40));
    weightShape->setPos(0, 200);
    weightShape->setName(QStringLiteral("weight"));
    scene.addItem(hookShape);
    scene.addItem(weightShape);
    scene.notifyShapesChanged();
    scene.setEditorMode(EditorMode::Physics);

    scene.selectForPhysics(hookShape, true);
    PhysicsBody *hook = scene.createBodyFromSelection();
    hook->props().type = BodyType::Static;
    scene.clearPhysicsSelection();

    scene.selectForPhysics(weightShape, true);
    PhysicsBody *weight = scene.createBodyFromSelection();
    scene.clearPhysicsSelection();

    Joint *rod = scene.createJoint(QStringLiteral("distance"), hook, weight, 2, {});
    ASSERT_NE(rod, nullptr);

    Rule vanish;
    vanish.subjectName = Rule::world();
    vanish.conditionKey = QStringLiteral("time");
    vanish.compare = Rule::Compare::Greater;
    vanish.conditionValue = 0.05;
    vanish.targetName = weight->name();
    vanish.actionId = QStringLiteral("removeBody");
    scene.setRules({ vanish });

    SimulationController sim(&scene, nullptr);
    sim.setEngineName(QStringLiteral("Box2D"));
    sim.start();
    for (int i = 0; i < 30; ++i)
        sim.stepFrame();

    EXPECT_TRUE(weight->isRemoved());
    EXPECT_TRUE(rod->isBroken()) << "the rod hung from it is gone too";

    sim.stop();
    EXPECT_FALSE(rod->isBroken()) << "and both are back when the run ends";
    EXPECT_FALSE(weight->isRemoved());
}
