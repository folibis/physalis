#include "CanvasScene.h"
#include "EngineRegistry.h"
#include "Joint.h"
#include "PhysicsBody.h"
#include "RectangleItem.h"
#include "Rule.h"
#include "SimulationController.h"

#include <QApplication>
#include <QVariantMap>
#include <gtest/gtest.h>

namespace {

Joint *twoBodiesAndAJoint(CanvasScene *scene, const QVariantMap &params)
{
    auto *a = new RectangleItem;
    a->setRect(QRectF(0, 0, 200, 20));
    a->setPos(0, 0);
    scene->addItem(a);
    auto *b = new RectangleItem;
    b->setRect(QRectF(0, 0, 40, 40));
    b->setPos(80, -60);
    scene->addItem(b);
    scene->notifyShapesChanged();
    scene->setEditorMode(EditorMode::Physics);
    scene->selectForPhysics(a, true);
    PhysicsBody *bodyA = scene->createBodyFromSelection();
    scene->clearPhysicsSelection();
    scene->selectForPhysics(b, true);
    PhysicsBody *bodyB = scene->createBodyFromSelection();
    scene->clearPhysicsSelection();
    Joint *joint = scene->createJoint(QStringLiteral("revolute"), bodyA, bodyB, 1, params);
    if (joint)
        joint->params() = params;
    return joint;
}

} // namespace

// A joint is set by one name and read by another: enableMotor going in,
// motorEnabled coming out. Only the second was answered, so anything watching
// the switch it had actually ticked fell through to the stored setting and
// reported the value the run started with for as long as the run lasted --
// while a rule was turning it on and off underneath.
TEST(WatchLabel, Behaves)
{
    CanvasScene scene;
    QVariantMap params;
    params.insert(QStringLiteral("enableMotor"), false);
    params.insert(QStringLiteral("enableLimit"), true);
    params.insert(QStringLiteral("motorSpeed"), 30.0);
    params.insert(QStringLiteral("maxMotorTorque"), 1.0);
    Joint *joint = twoBodiesAndAJoint(&scene, params);
    ASSERT_TRUE(joint != nullptr);

    SimulationController sim(&scene, nullptr);
    sim.setEngineName(QStringLiteral("Box2D"));
    sim.start();
    sim.stepFrame();

    const QString name = joint->name();
    const auto live = [&](const char *key) {
        return sim.readValue(name, QString::fromLatin1(key));
    };

    // The name it is set by answers, and answers the same as the name it is
    // read by.
    ASSERT_TRUE(live("enableMotor").isValid()) << "the setting name is readable at all";
    EXPECT_EQ(live("enableMotor").typeId(), QMetaType::Bool) << "as a flag, not a number";
    EXPECT_FALSE(live("enableMotor").toBool()) << "off, as the joint was built";
    EXPECT_EQ(live("enableMotor").toBool(), live("motorEnabled").toBool())
        << "both spellings agree";
    EXPECT_TRUE(live("enableLimit").toBool()) << "and the limit is on";

    // Turned on underneath by a rule: the reading has to follow, not go on
    // reporting what the file said.
    sim.stop();
    Rule turnOn;
    turnOn.subjectName = Rule::world();
    turnOn.conditionKey = QStringLiteral("frame");
    turnOn.compare = Rule::Compare::Greater;
    turnOn.conditionValue = 3.0;
    turnOn.targetName = name;
    turnOn.propertyKey = QStringLiteral("enableMotor");
    turnOn.op = Rule::Op::Set;
    turnOn.value = true;
    scene.setRules({turnOn});
    sim.start();
    for (int i = 0; i < 12; ++i)
        sim.stepFrame();
    EXPECT_TRUE(live("enableMotor").toBool())
        << "the switch reads as it now is, not as it started";
    EXPECT_EQ(live("enableMotor").toBool(), live("motorEnabled").toBool());

    sim.stop();
    EXPECT_FALSE(joint->params().value(QStringLiteral("enableMotor")).toBool())
        << "and stopping puts the document back to what the run started from";
}
