#include "CanvasScene.h"
#include "Joint.h"
#include "MainWindow.h"
#include "PhysicsBody.h"
#include "RectangleItem.h"
#include "Rule.h"
#include "RulesPanel.h"
#include "SceneSerializer.h"

#include <QApplication>
#include <QJsonObject>
#include <QVariantMap>
#include <gtest/gtest.h>

namespace {

void settle()
{
    for (int i = 0; i < 5; ++i)
        QCoreApplication::processEvents();
}

} // namespace

// The value editor for a new rule shows 0 because an unset QVariant reads as
// zero, but nothing had written 0 into the rule -- and a rule with no value is
// dropped as half-filled: never saved, never run. "When the joint hits its
// limit, set the motor to 0" is exactly the rule that could not be written,
// since leaving the box on the 0 it already showed changed nothing.
TEST(RuleValue, Behaves)
{
    MainWindow window;
    window.resize(1200, 800);
    window.show();
    settle();

    auto *scene = window.findChild<CanvasScene *>();
    ASSERT_TRUE(scene != nullptr);

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
    ASSERT_TRUE(bodyA && bodyB);

    Joint *joint = scene->createJoint(QStringLiteral("revolute"), bodyA, bodyB, 1, QVariantMap());
    ASSERT_TRUE(joint != nullptr);

    // Everything the panel's combo boxes would have set, except the value --
    // that one the user leaves alone, because it already reads 0.
    Rule rule;
    rule.subjectName = joint->name();
    rule.eventId = QStringLiteral("limitLower");
    rule.targetName = joint->name();
    rule.propertyKey = QStringLiteral("motorSpeed");
    rule.op = Rule::Op::Set;
    ASSERT_FALSE(rule.value.isValid()) << "the rule starts with no value at all";

    scene->setRules({rule});
    scene->notifyRulesChanged();       // what the panel listens to
    settle();

    ASSERT_FALSE(scene->rules().isEmpty());
    const Rule &built = scene->rules().first();
    EXPECT_TRUE(built.value.isValid())
        << "the number the editor shows is the number the rule holds";
    EXPECT_TRUE(built.isValid())
        << "so the rule is complete, rather than silently half-filled";

    const QJsonObject document = SceneSerializer::save(scene);
    EXPECT_TRUE(document.contains(QStringLiteral("rules")))
        << "and it reaches the saved file";
}
