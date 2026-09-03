#include "CanvasScene.h"
#include "PhysicsBody.h"
#include "RectangleItem.h"
#include "Rule.h"
#include "ShapeItem.h"
#include "SimulationController.h"

#include <QApplication>
#include <gtest/gtest.h>

using namespace physics;

namespace {

// A box on a floor. Which is what most scenes are, and enough to tell whether
// a change to the floor's surface reaches the box standing on it.
struct Floor
{
    CanvasScene scene;
    ShapeItem *ground = nullptr;
    ShapeItem *crate = nullptr;

    explicit Floor(qreal dropFrom)
    {
        scene.setSimulationEngineName(QStringLiteral("Box2D"));

        auto *groundShape = new RectangleItem;
        groundShape->setRect(QRectF(0, 0, 800, 40));
        groundShape->setPos(-400, 300);
        groundShape->setName(QStringLiteral("ground"));
        auto *crateShape = new RectangleItem;
        crateShape->setRect(QRectF(0, 0, 40, 40));
        crateShape->setPos(0, dropFrom);
        crateShape->setName(QStringLiteral("crate"));
        scene.addItem(groundShape);
        scene.addItem(crateShape);
        scene.notifyShapesChanged();
        scene.setEditorMode(EditorMode::Physics);

        ground = groundShape;
        crate = crateShape;

        scene.selectForPhysics(groundShape, true);
        scene.createBodyFromSelection()->props().type = BodyType::Static;
        scene.clearPhysicsSelection();

        scene.selectForPhysics(crateShape, true);
        scene.createBodyFromSelection();
        scene.clearPhysicsSelection();
    }
};

} // namespace

// b2Shape_SetSurfaceMaterial. Friction and restitution have always been live;
// the other two fields of the same struct were fixed when the shape was made,
// so a conveyor belt could exist but could never be switched on.
TEST(ShapeLive, SurfaceSpeedCarriesWhatIsOnIt)
{
    Floor belt(255.0);

    Rule start;
    start.subjectName = Rule::world();
    start.conditionKey = QStringLiteral("time");
    start.compare = Rule::Compare::Greater;
    start.conditionValue = 0.5;
    start.targetName = belt.ground->name();
    start.propertyKey = QStringLiteral("tangentSpeed");
    start.op = Rule::Op::Set;
    start.value = 400.0;
    belt.scene.setRules({ start });

    SimulationController sim(&belt.scene, nullptr);
    sim.setEngineName(QStringLiteral("Box2D"));
    sim.start();

    // Let it land and settle first, so what follows is the belt and not the
    // fall.
    for (int i = 0; i < 30; ++i)
        sim.stepFrame();
    const qreal settled = belt.crate->pos().x();

    for (int i = 0; i < 120; ++i)
        sim.stepFrame();

    EXPECT_NEAR(sim.readValue(belt.ground->name(), QStringLiteral("tangentSpeed")).toDouble(),
                400.0, 1.0)
        << "the surface reads back the speed the rule gave it";
    // Which way along the surface is Box2D's: the tangent's sign follows the
    // contact normal, not the scene's x axis. That it moves at all is the
    // point -- before b2Shape_SetSurfaceMaterial was wired it could not.
    EXPECT_GT(qAbs(belt.crate->pos().x() - settled), 20.0)
        << "and the crate standing on it has been carried along";

    sim.stop();
}

// b2ContactHitEvent's numbers. The event says a shape was hit; how hard was
// thrown away with the rest of the event, so "when it is hit harder than this"
// could not be written.
TEST(ShapeLive, ImpactSpeedIsReadableAfterAHit)
{
    Floor drop(-200.0);
    drop.crate->part().enableHitEvents = true;
    drop.crate->part().enableContactEvents = true;

    SimulationController sim(&drop.scene, nullptr);
    sim.setEngineName(QStringLiteral("Box2D"));
    sim.start();

    EXPECT_EQ(sim.readValue(drop.crate->name(), QStringLiteral("lastHitSpeed")).toDouble(), 0.0)
        << "nothing has hit it yet, and the reading is answerable rather than absent";

    for (int i = 0; i < 120; ++i)
        sim.stepFrame();

    EXPECT_GT(sim.readValue(drop.crate->name(), QStringLiteral("lastHitSpeed")).toDouble(), 0.0)
        << "landing leaves an impact speed behind it";
    EXPECT_TRUE(sim.readValue(drop.crate->name(), QStringLiteral("enableHitEvents")).toBool())
        << "and the flag that allowed it reads back";

    sim.stop();
}

// b2Shape_SetFilter. What a shape collides with was decided when it was made,
// though Box2D has always let it be changed.
TEST(ShapeLive, CollisionGroupsChangeMidRun)
{
    Floor trapdoor(255.0);

    Rule open;
    open.subjectName = Rule::world();
    open.conditionKey = QStringLiteral("time");
    open.compare = Rule::Compare::Greater;
    open.conditionValue = 0.5;
    open.targetName = trapdoor.ground->name();
    open.propertyKey = QStringLiteral("maskBits");
    open.op = Rule::Op::Set;
    open.value = 0.0;   // collides with nothing at all
    trapdoor.scene.setRules({ open });

    SimulationController sim(&trapdoor.scene, nullptr);
    sim.setEngineName(QStringLiteral("Box2D"));
    sim.start();

    for (int i = 0; i < 30; ++i)
        sim.stepFrame();
    const qreal resting = trapdoor.crate->pos().y();

    for (int i = 0; i < 120; ++i)
        sim.stepFrame();

    EXPECT_GT(trapdoor.crate->pos().y(), resting + 40.0)
        << "the floor stopped colliding with it, so it went through";

    sim.stop();
}
