#include "CanvasScene.h"
#include "SceneFixtures.h"
#include "Joint.h"
#include "PhysicsBody.h"
#include "ShapeItem.h"
#include "Rule.h"
#include "SceneSerializer.h"
#include "SimulationController.h"

#include <QApplication>
#include <gtest/gtest.h>
#include <cstdio>


TEST(RuleUndo, Behaves)
{
    CanvasScene scene;
    QString error;
    Fixtures::buildCart(&scene);

    // Both ends of a contact must report events, or the rule never fires.
    for (ShapeItem *sh : scene.shapes())
        sh->part().enableContactEvents = true;

    Joint *driven = nullptr;
    for (Joint *j : scene.joints())
        if (j->name() == QLatin1String("wheel_2"))
            driven = j;

    // Set up something for the rules to change. Left at the joint defaults
    // there would be no spring to switch off and no motor speed to reverse,
    // and the test would pass while proving nothing.
    if (driven) {
        driven->params().insert(QStringLiteral("enableSpring"), true);
        driven->params().insert(QStringLiteral("motorSpeed"), 180.0);
        driven->params().insert(QStringLiteral("enableMotor"), true);
    }

    // A rule that turns the spring off and reverses the motor once the chassis
    // touches anything -- the kind of rule that used to outlive its run.
    // A condition that is true from the first step, so the rules certainly
    // fire: the chassis sits well below y = 0.
    QVector<Rule> rules;
    Rule off;
    off.subjectName = QStringLiteral("body_1");
    off.conditionKey = QStringLiteral("positionY");
    off.compare = Rule::Compare::Greater;
    off.conditionValue = 0.0;
    off.targetName = QStringLiteral("wheel_2");
    off.propertyKey = QStringLiteral("enableSpring");
    off.op = Rule::Op::Set;
    off.value = false;
    rules.append(off);
    Rule flip = off;
    flip.propertyKey = QStringLiteral("motorSpeed");
    flip.op = Rule::Op::Negate;
    flip.value = QVariant();
    rules.append(flip);
    scene.setRules(rules);

    const bool springBefore = driven->params().value(QStringLiteral("enableSpring")).toBool();
    const double speedBefore = driven->params().value(QStringLiteral("motorSpeed")).toDouble();

    SimulationController sim(&scene, nullptr);
    sim.setEngineName(QStringLiteral("Box2D"));
    sim.start();
    for (int i = 0; i < 120; ++i)
        sim.stepFrame();
    const bool springDuring = driven->params().value(QStringLiteral("enableSpring")).toBool();
    const double speedDuring = driven->params().value(QStringLiteral("motorSpeed")).toDouble();
    EXPECT_TRUE(springDuring != springBefore) << "the rule takes effect while running";

    sim.stop();
    const bool springAfter = driven->params().value(QStringLiteral("enableSpring")).toBool();
    const double speedAfter = driven->params().value(QStringLiteral("motorSpeed")).toDouble();
    EXPECT_TRUE(springAfter == springBefore) << "enableSpring is back to what you set";
    EXPECT_TRUE(qFuzzyCompare(speedAfter, speedBefore)) << "motorSpeed is back to what you set";

    // And a second run must start from the same place as the first.
    sim.start();
    for (int i = 0; i < 120; ++i)
        sim.stepFrame();
    const bool springSecond = driven->params().value(QStringLiteral("enableSpring")).toBool();
    sim.stop();
    EXPECT_TRUE(springSecond == springDuring) << "a second run behaves like the first";
    EXPECT_TRUE(driven->params().value(QStringLiteral("enableSpring")).toBool() == springBefore) << "and leaves the document alone again";
}
