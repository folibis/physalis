#include "CanvasScene.h"
#include "Joint.h"
#include "MainWindow.h"
#include "PhysicsBody.h"
#include "RectangleItem.h"
#include "Rule.h"

#include <QApplication>
#include <QVariantMap>
#include <gtest/gtest.h>

namespace {

void settle()
{
    for (int i = 0; i < 12; ++i)
        QCoreApplication::processEvents();
}

PhysicsBody *bodyFrom(CanvasScene *scene, const QPointF &at, const char *name)
{
    auto *shape = new RectangleItem;
    shape->setRect(QRectF(0, 0, 40, 40));
    shape->setPos(at);
    shape->setName(QString::fromLatin1(name));
    scene->addItem(shape);
    scene->notifyShapesChanged();
    scene->selectForPhysics(shape, true);
    PhysicsBody *body = scene->createBodyFromSelection();
    scene->clearPhysicsSelection();
    return body;
}

QString propertyOfFirstRule(CanvasScene *scene)
{
    return scene->rules().isEmpty() ? QString() : scene->rules().first().propertyKey;
}

} // namespace

// The list of things a rule can aim at is rebuilt whenever the scene gains an
// object, and that alone moves the target box's current index. The handler
// took that for the user re-aiming the rule, forgot the property, and then the
// property box adopted whatever happened to be on top of its own list. A rule
// that said "stop the motor" came back saying "set the solver damping", and
// saved that way -- so the motor was never stopped again.
TEST(RuleKeeps, Behaves)
{
    MainWindow window;
    window.resize(1200, 800);
    window.show();
    settle();

    auto *scene = window.findChild<CanvasScene *>();
    ASSERT_TRUE(scene != nullptr);
    scene->setEditorMode(EditorMode::Physics);

    PhysicsBody *a = bodyFrom(scene, QPointF(0, 0), "base");
    PhysicsBody *b = bodyFrom(scene, QPointF(100, 0), "arm");
    ASSERT_TRUE(a && b);
    Joint *joint = scene->createJoint(QStringLiteral("revolute"), a, b, 1, QVariantMap());
    ASSERT_TRUE(joint != nullptr);

    Rule stop;
    stop.subjectName = joint->name();
    stop.conditionKey = QStringLiteral("angle");
    stop.compare = Rule::Compare::Less;
    stop.conditionValue = -14.0;
    stop.targetName = joint->name();
    stop.propertyKey = QStringLiteral("motorSpeed");
    stop.op = Rule::Op::Set;
    stop.value = 0.0;
    scene->setRules({stop});
    scene->notifyRulesChanged();
    settle();

    ASSERT_EQ(propertyOfFirstRule(scene), QStringLiteral("motorSpeed"))
        << "the rule starts out aimed at the motor";

    // Everything that rebuilds the choice lists under a card that is already
    // on screen.
    for (int i = 0; i < 4; ++i) {
        bodyFrom(scene, QPointF(200 + 60 * i, 120), qPrintable(QStringLiteral("extra%1").arg(i)));
        settle();
        EXPECT_EQ(propertyOfFirstRule(scene), QStringLiteral("motorSpeed"))
            << "adding an object must not re-aim the rule (after " << (i + 1) << ")";
    }

    scene->notifyRulesChanged();
    settle();
    EXPECT_EQ(propertyOfFirstRule(scene), QStringLiteral("motorSpeed"))
        << "and neither must rebuilding the panel";

    EXPECT_EQ(scene->rules().first().targetName, joint->name()) << "still aimed at the joint";
    EXPECT_TRUE(scene->rules().first().isValid()) << "and still a complete rule";
}
