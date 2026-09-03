#include "CanvasScene.h"
#include "PhysicsBody.h"
#include "RectangleItem.h"
#include "Rule.h"
#include "SimulationController.h"

#include <QApplication>
#include <gtest/gtest.h>

using namespace physics;

namespace {

PhysicsBody *loneBox(CanvasScene *scene)
{
    scene->setSimulationEngineName(QStringLiteral("Box2D"));

    auto *shape = new RectangleItem;
    shape->setRect(QRectF(0, 0, 60, 40));
    shape->setPos(0, 0);
    scene->addItem(shape);
    scene->notifyShapesChanged();
    scene->setEditorMode(EditorMode::Physics);

    scene->selectForPhysics(shape, true);
    PhysicsBody *body = scene->createBodyFromSelection();
    scene->clearPhysicsSelection();
    return body;
}

// Runs the scene with one rule and answers how fast the body is turning by
// the end of it.
qreal spinAfter(CanvasScene *scene, PhysicsBody *body, const Rule &rule, int frames)
{
    scene->setRules({ rule });
    SimulationController sim(scene, nullptr);
    sim.setEngineName(QStringLiteral("Box2D"));
    sim.start();
    for (int i = 0; i < frames; ++i)
        sim.stepFrame();
    const qreal spin = sim.readValue(body->name(), QStringLiteral("angularVelocity")).toDouble();
    sim.stop();
    return spin;
}

Rule after(qreal seconds, const QString &target, const QString &key, const QVariant &value)
{
    Rule rule;
    rule.subjectName = Rule::world();
    rule.conditionKey = QStringLiteral("time");
    rule.compare = Rule::Compare::Greater;
    rule.conditionValue = seconds;
    rule.targetName = target;
    rule.propertyKey = key;
    rule.op = Rule::Op::Set;
    rule.value = value;
    return rule;
}

} // namespace

// b2Body_ApplyAngularImpulse. Nothing in the editor could make anything spin
// before this: there was no torque, no angular impulse, and the angular
// velocity was published read-only.
TEST(BodySpin, AngularImpulseSetsSomethingTurning)
{
    CanvasScene scene;
    PhysicsBody *box = loneBox(&scene);

    const qreal spin = spinAfter(
        &scene, box, after(0.05, box->name(), QStringLiteral("angularImpulse"), 40.0), 20);

    EXPECT_GT(qAbs(spin), 1.0)
        << "a twist from a rule leaves the box turning -- it read " << spin << " deg/s";
}

// b2Body_ApplyTorque. The turning half of the force pair, and just as brief:
// it is applied for the one step the rule fires on.
TEST(BodySpin, TorqueTurnsItToo)
{
    CanvasScene scene;
    PhysicsBody *box = loneBox(&scene);

    const qreal spin = spinAfter(
        &scene, box, after(0.05, box->name(), QStringLiteral("torque"), 400.0), 20);

    EXPECT_GT(qAbs(spin), 0.1) << "it read " << spin << " deg/s";
}

// b2Body_SetAngularVelocity. The catalogue offered angularVelocity as a
// reading only, though Box2D has had the setter all along.
TEST(BodySpin, AngularVelocityIsWritable)
{
    CanvasScene scene;
    PhysicsBody *box = loneBox(&scene);

    const qreal spin = spinAfter(
        &scene, box, after(0.05, box->name(), QStringLiteral("angularVelocity"), 200.0), 10);

    EXPECT_NEAR(spin, 200.0, 5.0) << "set in degrees per second, and read back in them";
}
