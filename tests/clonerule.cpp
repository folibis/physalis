#include "CanvasScene.h"
#include "CircleItem.h"
#include "PhysicsBody.h"
#include "Rule.h"
#include "SceneSerializer.h"
#include "SimulationController.h"

#include <QJsonArray>
#include <QJsonObject>
#include <gtest/gtest.h>

// "Clone" makes a body just like another -- its body and shape properties --
// at the X and Y the rule gives. It works on a body a rule already removed,
// lasts only as long as the run, and is never saved.

namespace {

struct Setup {
    CanvasScene scene;
    CircleItem *ballShape = nullptr;
    PhysicsBody *ball = nullptr;
};

void build(Setup &s, const QString &engine, bool removeFirst)
{
    s.scene.setSimulationEngineName(engine);
    s.scene.world().params["gravityY"] = 0.0;
    s.scene.setEditorMode(EditorMode::Physics);

    s.ballShape = s.scene.addCircle(QPointF(0, 0));
    s.ballShape->setName(QStringLiteral("ball"));
    s.ballShape->part().params["friction"] = 0.05;
    s.scene.notifyShapesChanged();
    s.scene.selectForPhysics(s.ballShape, true);
    s.ball = s.scene.createBodyFromSelection();
    s.scene.clearPhysicsSelection();
    ASSERT_TRUE(s.ball);
    s.ball->props().params["linearDamping"] = 0.6;

    QVector<Rule> rules;
    if (removeFirst) {
        Rule remove;
        remove.subjectName = Rule::world();
        remove.eventId = Rule::runStartedEvent();
        remove.targetName = s.ball->name();
        remove.actionId = QStringLiteral("removeBody");
        rules << remove;
    }
    Rule clone;
    clone.subjectName = Rule::world();
    clone.eventId = Rule::runStartedEvent();
    clone.targetName = s.ball->name();
    clone.actionId = Rule::cloneAction();
    clone.actionParams.insert(Rule::cloneXParam(), 400.0);
    clone.actionParams.insert(Rule::cloneYParam(), 250.0);
    rules << clone;
    s.scene.setRules(rules);
}

PhysicsBody *cloneOf(const CanvasScene &scene, const PhysicsBody *parent)
{
    for (PhysicsBody *body : scene.bodies()) {
        if (body != parent)
            return body;
    }
    return nullptr;
}

void check(const QString &engine, bool removeFirst)
{
    Setup s;
    build(s, engine, removeFirst);
    if (!s.ball)
        return;

    SimulationController sim(&s.scene, nullptr);
    sim.setEngineName(engine);
    sim.start();
    sim.stepFrame();

    ASSERT_EQ(s.scene.bodies().size(), 2) << "no clone was made";
    PhysicsBody *clone = cloneOf(s.scene, s.ball);
    ASSERT_NE(clone, nullptr);
    EXPECT_NE(clone->name(), s.ball->name());
    ASSERT_EQ(clone->shapes().size(), 1);
    EXPECT_NE(clone->shapes().first()->name(), s.ballShape->name());
    EXPECT_TRUE(clone->shapes().first()->isVisible()) << "the clone of a removed body is hidden";
    EXPECT_FALSE(clone->isRemoved());

    const QPointF at = clone->originScenePos();
    EXPECT_NEAR(at.x(), 400.0, 1.0);
    EXPECT_NEAR(at.y(), 250.0, 1.0);

    EXPECT_EQ(clone->props().params.value("linearDamping").toDouble(), 0.6);
    EXPECT_EQ(clone->shapes().first()->part().params.value("friction").toDouble(), 0.05);
    const double parentMass = sim.readValue(s.ballShape->name(), QStringLiteral("mass")).toDouble();
    const double cloneMass = sim.readValue(clone->shapes().first()->name(), QStringLiteral("mass")).toDouble();
    EXPECT_GT(cloneMass, 0.0) << "the engine has no body for the clone";
    if (!removeFirst)
        EXPECT_DOUBLE_EQ(cloneMass, parentMass);
    EXPECT_EQ(s.ball->isRemoved(), removeFirst);

    // Saving mid-run writes the scene, not the run.
    const QJsonObject saved = SceneSerializer::save(&s.scene);
    EXPECT_EQ(saved.value("bodies").toArray().size(), 1) << "the clone was saved";
    EXPECT_EQ(saved.value("shapes").toArray().size(), 1) << "the clone's shape was saved";

    sim.stop();
    EXPECT_EQ(s.scene.bodies().size(), 1) << "the clone outlived the run";
    EXPECT_EQ(s.scene.shapes().size(), 1);
    EXPECT_EQ(s.scene.bodies().first(), s.ball);
}

} // namespace

TEST(CloneRule, Box2DClonesABody) { check(QStringLiteral("Box2D"), false); }
TEST(CloneRule, Box2DClonesARemovedBody) { check(QStringLiteral("Box2D"), true); }
TEST(CloneRule, ChipmunkClonesABody) { check(QStringLiteral("Chipmunk2D"), false); }
TEST(CloneRule, ChipmunkClonesARemovedBody) { check(QStringLiteral("Chipmunk2D"), true); }
