#include "CanvasScene.h"
#include "EngineRegistry.h"
#include "MainWindow.h"
#include "PhysicsBody.h"
#include "RectangleItem.h"
#include "Rule.h"
#include "RulesPanel.h"
#include "SimulationController.h"
#include "ShapeItem.h"

#include <QApplication>
#include <QComboBox>
#include <gtest/gtest.h>

namespace {

void settle()
{
    for (int i = 0; i < 30; ++i)
        QCoreApplication::processEvents();
}

// Everything the "Then change" dropdown is offering, whichever combo it is.
// The panel builds several; the one being looked for is whichever holds the
// key given.
QStringList choicesContaining(RulesPanel *rules, const QString &key)
{
    for (QComboBox *combo : rules->findChildren<QComboBox *>()) {
        QStringList keys;
        for (int i = 0; i < combo->count(); ++i)
            keys << combo->itemData(i).toString();
        if (keys.contains(key))
            return keys;
    }
    return {};
}

} // namespace

// An engine's actions have to be offered on the thing they act on. They were
// reaching the dropdown for explosions only, so Explode, Push at a Point and
// Remove could all be performed on a body by the rule engine and none of them
// could be chosen for one -- the action existed and was unreachable.
TEST(ActionMenu, BodyActionsAreOfferedOnBodies)
{
    MainWindow window;
    window.resize(1300, 800);
    window.show();
    settle();

    auto *scene = window.findChild<CanvasScene *>();
    scene->setEditorMode(EditorMode::Physics);

    auto *block = new RectangleItem;
    block->setRect(QRectF(0, 0, 80, 40));
    block->setName(QStringLiteral("block"));
    scene->addItem(block);
    scene->notifyShapesChanged();
    scene->selectForPhysics(block, true);
    PhysicsBody *body = scene->createBodyFromSelection();
    scene->clearPhysicsSelection();

    Rule rule;
    rule.subjectName = Rule::world();
    rule.conditionKey = QStringLiteral("time");
    rule.conditionValue = 1.0;
    rule.targetName = body->name();
    rule.propertyKey = QStringLiteral("velocityX");
    scene->setRules({ rule });
    settle();

    auto *rules = window.findChild<RulesPanel *>();
    ASSERT_NE(rules, nullptr);

    // Whatever the engine calls its body actions, all of them belong here --
    // the test asks the engine rather than naming them, the same way the panel
    // does.
    auto engine = physics::EngineRegistry::create(scene->simulationEngineName());
    ASSERT_TRUE(engine);
    ASSERT_FALSE(engine->bodyActions().isEmpty());

    const QStringList offered =
        choicesContaining(rules, QStringLiteral("velocityX"));
    ASSERT_FALSE(offered.isEmpty()) << "the property dropdown for a body was found";

    for (const physics::ActionType &action : engine->bodyActions()) {
        EXPECT_TRUE(offered.contains(QStringLiteral("@action:") + action.id))
            << "a rule can choose " << action.id.toStdString() << " on a body";
    }
}

// An action's parameters were never filled in for anything but an explosion
// object, so a rule performing one on a body did it with every number at zero
// -- a blast of radius nothing, which looks exactly like the rule not firing.
TEST(ActionMenu, ActionOnABodyGetsItsDefaults)
{
    CanvasScene scene;
    scene.setSimulationEngineName(QStringLiteral("Box2D"));

    const auto addBox = [&scene](const QString &name, const QPointF &pos) {
        auto *shape = new RectangleItem;
        shape->setRect(QRectF(0, 0, 40, 40));
        shape->setPos(pos);
        shape->setName(name);
        scene.addItem(shape);
        return shape;
    };
    ShapeItem *chargeShape = addBox(QStringLiteral("charge"), QPointF(0, 0));
    ShapeItem *nearbyShape = addBox(QStringLiteral("nearby"), QPointF(90, 0));
    scene.notifyShapesChanged();
    scene.setEditorMode(EditorMode::Physics);

    scene.selectForPhysics(chargeShape, true);
    PhysicsBody *charge = scene.createBodyFromSelection();
    scene.clearPhysicsSelection();
    scene.selectForPhysics(nearbyShape, true);
    scene.createBodyFromSelection();
    scene.clearPhysicsSelection();

    // Stored with no parameters at all, the way every action rule aimed at a
    // body has been stored until now. The engine's own defaults have to fill
    // the gap, or nothing happens.
    Rule bang;
    bang.subjectName = Rule::world();
    bang.conditionKey = QStringLiteral("time");
    bang.compare = Rule::Compare::Greater;
    bang.conditionValue = 0.05;
    bang.targetName = charge->name();
    bang.actionId = QStringLiteral("explode");
    scene.setRules({ bang });

    SimulationController sim(&scene, nullptr);
    sim.setEngineName(QStringLiteral("Box2D"));
    sim.start();
    const qreal before = nearbyShape->pos().x();
    for (int i = 0; i < 30; ++i)
        sim.stepFrame();
    const qreal after = nearbyShape->pos().x();
    sim.stop();

    EXPECT_GT(after, before + 5.0)
        << "the blast pushed the neighbour away, so it had a radius to reach with";
}
