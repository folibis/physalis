#include "CanvasScene.h"
#include "EngineRegistry.h"
#include "Joint.h"
#include "JointTypes.h"
#include "PhysicsBody.h"
#include "RectangleItem.h"
#include "Rule.h"
#include "ShapeItem.h"
#include "SimulationController.h"

#include <QApplication>
#include <QVariantMap>
#include <gtest/gtest.h>

using namespace physics;

namespace {

// A weight hanging from a fixed point by a rod. Left alone it stays where it
// was hung; with nothing holding it, it falls.
struct Pendulum
{
    CanvasScene scene;
    Joint *rod = nullptr;
    ShapeItem *weightShape = nullptr;

    Pendulum()
    {
        scene.setSimulationEngineName(QStringLiteral("Box2D"));

        auto *hookShape = new RectangleItem;
        hookShape->setRect(QRectF(0, 0, 40, 40));
        hookShape->setPos(0, 0);
        weightShape = new RectangleItem;
        weightShape->setRect(QRectF(0, 0, 40, 40));
        weightShape->setPos(0, 300);
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

        QVariantMap params;
        int anchors = 0;
        if (auto engine = EngineRegistry::create(QStringLiteral("Box2D"))) {
            for (const JointType &t : engine->jointTypes()) {
                if (t.id != QLatin1String("distance"))
                    continue;
                anchors = t.anchorCount;
                for (const JointParam &p : t.params)
                    params.insert(p.key, p.defaultValue);
            }
        }
        rod = scene.createJoint(QStringLiteral("distance"), hook, weight, anchors, params);
    }
};

} // namespace

// b2DestroyJoint, as a rule action. There is no value that means "no longer
// connected", so breaking a joint could not be said at all before jointActions.
TEST(BreakJoint, LetsGoOfWhatItHeld)
{
    Pendulum held;
    qreal heldY = 0.0;
    {
        SimulationController sim(&held.scene, nullptr);
        sim.setEngineName(QStringLiteral("Box2D"));
        sim.start();
        for (int i = 0; i < 60; ++i)
            sim.stepFrame();
        heldY = held.weightShape->pos().y();
        sim.stop();
    }

    Pendulum cut;
    Rule snap;
    snap.subjectName = Rule::world();
    snap.conditionKey = QStringLiteral("time");
    snap.compare = Rule::Compare::Greater;
    snap.conditionValue = 0.05;
    snap.targetName = cut.rod->name();
    snap.actionId = QStringLiteral("breakJoint");
    cut.scene.setRules({ snap });

    SimulationController sim(&cut.scene, nullptr);
    sim.setEngineName(QStringLiteral("Box2D"));
    sim.start();
    for (int i = 0; i < 60; ++i)
        sim.stepFrame();

    const qreal cutY = cut.weightShape->pos().y();
    EXPECT_TRUE(cut.rod->isBroken()) << "the joint is marked broken while the run lasts";
    EXPECT_GT(cutY, heldY + 20.0) << "and the weight is falling, where the held one is not";

    sim.stop();
    EXPECT_FALSE(cut.rod->isBroken())
        << "stopping puts the joint back -- the document was never changed";
}

// b2DestroyBody, likewise. The shapes stay in the scene; they just stop being
// drawn, because there is nothing left in the world to place them.
TEST(BreakJoint, RemovingABodyHidesItUntilTheRunEnds)
{
    Pendulum scene;

    Rule remove;
    remove.subjectName = Rule::world();
    remove.conditionKey = QStringLiteral("time");
    remove.compare = Rule::Compare::Greater;
    remove.conditionValue = 0.05;
    remove.targetName = scene.weightShape->body()->name();
    remove.actionId = QStringLiteral("removeBody");
    scene.scene.setRules({ remove });

    SimulationController sim(&scene.scene, nullptr);
    sim.setEngineName(QStringLiteral("Box2D"));
    sim.start();
    ASSERT_TRUE(scene.weightShape->isVisible()) << "it starts out on screen";

    for (int i = 0; i < 30; ++i)
        sim.stepFrame();
    EXPECT_FALSE(scene.weightShape->isVisible()) << "and goes once the rule removes it";

    sim.stop();
    EXPECT_TRUE(scene.weightShape->isVisible()) << "and comes back when the run ends";
}
