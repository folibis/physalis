#include "CanvasScene.h"
#include "MainWindow.h"
#include "PhysicsBody.h"
#include "RectangleItem.h"
#include "Rule.h"
#include "RulesPanel.h"
#include "SimulationController.h"

#include <QApplication>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QTabWidget>
#include <gtest/gtest.h>

// A rule that ends the run, and the shape of the card that writes one.

namespace {

void settle()
{
    for (int i = 0; i < 30; ++i)
        QCoreApplication::processEvents();
}

PhysicsBody *fallingBlock(CanvasScene *scene)
{
    auto *shape = new RectangleItem;
    shape->setRect(QRectF(0, 0, 60, 60));
    shape->setName(QStringLiteral("block"));
    scene->addItem(shape);
    scene->notifyShapesChanged();
    scene->setEditorMode(EditorMode::Physics);
    scene->selectForPhysics(shape, true);
    PhysicsBody *body = scene->createBodyFromSelection();
    scene->clearPhysicsSelection();
    return body;
}

} // namespace

TEST(StopRule, TheWorldCanBeToldToStop)
{
    CanvasScene scene;
    scene.setSimulationEngineName(QStringLiteral("Box2D"));
    PhysicsBody *block = fallingBlock(&scene);
    ASSERT_TRUE(block);
    const QPointF start = block->originScenePos();

    // Half a second in, that is enough.
    Rule enough;
    enough.subjectName = Rule::world();
    enough.conditionKey = QStringLiteral("time");
    enough.compare = Rule::Compare::Greater;
    enough.conditionValue = 0.5;
    enough.targetName = Rule::world();
    enough.actionId = Rule::stopRunAction();
    enough.once = true;
    scene.setRules({ enough });

    SimulationController sim(&scene, nullptr);
    sim.setEngineName(QStringLiteral("Box2D"));
    sim.start();
    for (int i = 0; i < 120 && sim.isActive(); ++i)
        sim.stepFrame();

    EXPECT_FALSE(sim.isActive()) << "the rule ended the run without anyone pressing Stop";
    EXPECT_LT(sim.readValue(Rule::world(), QStringLiteral("time")).toDouble(), 0.6)
        << "at the moment it asked, not at the end of the loop";
    EXPECT_EQ(block->originScenePos(), start)
        << "and ending a run puts the scene back where it started, as Stop does";
}

TEST(StopRule, HoldingLeavesTheRunWhereItIs)
{
    CanvasScene scene;
    scene.setSimulationEngineName(QStringLiteral("Box2D"));
    PhysicsBody *block = fallingBlock(&scene);
    ASSERT_TRUE(block);
    const QPointF start = block->originScenePos();

    Rule hold;
    hold.subjectName = Rule::world();
    hold.conditionKey = QStringLiteral("time");
    hold.compare = Rule::Compare::Greater;
    hold.conditionValue = 0.5;
    hold.targetName = Rule::world();
    hold.actionId = Rule::holdRunAction();
    hold.once = true;
    scene.setRules({ hold });

    SimulationController sim(&scene, nullptr);
    sim.setEngineName(QStringLiteral("Box2D"));
    sim.start();
    for (int i = 0; i < 60; ++i)
        sim.stepFrame();

    EXPECT_TRUE(sim.isActive()) << "held, not ended";
    EXPECT_FALSE(sim.isRunning()) << "and not running either";
    EXPECT_NE(block->originScenePos(), start) << "the block stays where it fell to";
    sim.stop();
}

TEST(StopRule, TheCardOffersItOnTheWorld)
{
    MainWindow window;
    window.resize(1300, 800);
    window.show();
    settle();

    auto *scene = window.findChild<CanvasScene *>();
    ASSERT_TRUE(fallingBlock(scene));

    Rule rule;
    rule.subjectName = Rule::world();
    rule.conditionKey = QStringLiteral("time");
    rule.compare = Rule::Compare::Greater;
    rule.conditionValue = 1.0;
    rule.targetName = Rule::world();
    rule.propertyKey = QStringLiteral("gravityY");
    rule.value = 0.0;
    scene->setRules({ rule });

    for (QTabWidget *tabs : window.findChildren<QTabWidget *>())
        for (int i = 0; i < tabs->count(); ++i)
            if (tabs->tabText(i).contains(QStringLiteral("Rule")))
                tabs->setCurrentIndex(i);
    settle();

    auto *rules = window.findChild<RulesPanel *>();
    ASSERT_TRUE(rules);

    QComboBox *what = nullptr;
    for (QComboBox *combo : rules->findChildren<QComboBox *>())
        if (combo->findData(QStringLiteral("@action:") + Rule::stopRunAction()) >= 0)
            what = combo;
    ASSERT_TRUE(what) << "the world's list of things to change offers ending the run";

    what->setCurrentIndex(what->findData(QStringLiteral("@action:") + Rule::stopRunAction()));
    settle();
    EXPECT_EQ(scene->rules().first().actionId, Rule::stopRunAction())
        << "and picking it writes it into the rule";
    window.close();
}

// The instruction reads as one line -- "Do: Set to 12.5" -- rather than the
// operation on one line and the number under it.
TEST(StopRule, TheValueSitsBesideTheOperation)
{
    MainWindow window;
    window.resize(1300, 800);
    window.show();
    settle();

    auto *scene = window.findChild<CanvasScene *>();
    PhysicsBody *block = fallingBlock(scene);

    Rule rule;
    rule.subjectName = block->name();
    rule.conditionKey = QStringLiteral("positionY");
    rule.compare = Rule::Compare::Greater;
    rule.conditionValue = 100.0;
    rule.targetName = block->name();
    rule.propertyKey = QStringLiteral("gravityScale");
    rule.op = Rule::Op::Set;
    rule.value = 0.0;
    scene->setRules({ rule });

    for (QTabWidget *tabs : window.findChildren<QTabWidget *>())
        for (int i = 0; i < tabs->count(); ++i)
            if (tabs->tabText(i).contains(QStringLiteral("Rule")))
                tabs->setCurrentIndex(i);
    settle();

    auto *rules = window.findChild<RulesPanel *>();
    ASSERT_TRUE(rules);

    QComboBox *op = nullptr;
    for (QComboBox *combo : rules->findChildren<QComboBox *>())
        if (combo->isVisible() && combo->findText(QStringLiteral("Set to")) >= 0)
            op = combo;
    ASSERT_TRUE(op) << "the operation is on the card";

    QWidget *editor = nullptr;
    for (QDoubleSpinBox *spin : rules->findChildren<QDoubleSpinBox *>())
        if (spin->isVisible())
            editor = spin;
    ASSERT_TRUE(editor) << "and so is the number it sets";

    const QPoint opAt = op->mapTo(rules, QPoint(0, 0));
    const QPoint valueAt = editor->mapTo(rules, QPoint(0, 0));
    EXPECT_LT(qAbs(opAt.y() - valueAt.y()), op->height())
        << "they share a line rather than stacking";
    EXPECT_GT(valueAt.x(), opAt.x()) << "with the number after the operation";

    bool renamed = false;
    for (QLabel *label : rules->findChildren<QLabel *>())
        renamed = renamed || label->text() == QStringLiteral("Then:");
    EXPECT_TRUE(renamed) << "and the caption is just \"Then:\"";
    window.close();
}
