#include "CanvasScene.h"
#include "PhysicsBody.h"
#include "RectangleItem.h"
#include "Rule.h"
#include "ShapeItem.h"
#include "SimulationController.h"
#include "EngineRegistry.h"

#include <QApplication>
#include <gtest/gtest.h>
#include <cstdio>


TEST(SensorEvents, Behaves)
{
    auto engine = physics::EngineRegistry::create(QStringLiteral("Box2D"));
    QStringList ids;
    for (const physics::EventType &e : engine->shapeEvents())
        ids << e.id;
    EXPECT_TRUE(ids.contains(QStringLiteral("sensorBegin"))) << "sensorBegin is offered";
    EXPECT_TRUE(ids.contains(QStringLiteral("sensorEnd"))) << "sensorEnd is offered";

    // A falling box, and a static sensor slab it passes through.
    CanvasScene scene;
    scene.setSimulationEngineName(QStringLiteral("Box2D"));

    auto *pad = new RectangleItem;
    pad->setRect(QRectF(0, 0, 400, 20));
    pad->setPos(-200, 200);
    pad->setName(QStringLiteral("trigger"));
    auto *faller = new RectangleItem;
    faller->setRect(QRectF(0, 0, 40, 40));
    faller->setPos(-20, -100);
    faller->setName(QStringLiteral("box"));
    scene.addItem(pad);
    scene.addItem(faller);
    scene.notifyShapesChanged();
    scene.setEditorMode(EditorMode::Physics);

    scene.selectForPhysics(pad, true);
    PhysicsBody *padBody = scene.createBodyFromSelection();
    padBody->props().type = physics::BodyType::Static;
    pad->part().isSensor = true;              // it should not block
    pad->part().enableSensorEvents = true;
    scene.clearPhysicsSelection();

    scene.selectForPhysics(faller, true);
    PhysicsBody *fallBody = scene.createBodyFromSelection();
    faller->part().enableSensorEvents = true;   // the visitor must opt in
    scene.clearPhysicsSelection();

    // A rule that fires on the sensor being entered.
    Rule entered;
    entered.subjectName = pad->name();
    entered.eventId = QStringLiteral("sensorBegin");
    entered.conditionValue = faller->name();   // specifically this shape
    entered.targetName = Rule::otherObjectBody();
    entered.propertyKey = QStringLiteral("gravityScale");
    entered.op = Rule::Op::Set;
    entered.value = 0.0;
    scene.setRules({ entered });

    SimulationController sim(&scene, nullptr);
    sim.setEngineName(QStringLiteral("Box2D"));
    sim.start();

    bool passedThrough = false;
    for (int frame = 0; frame < 600; ++frame) {
        sim.stepFrame();
        if (faller->sceneBoundingRect().top() > pad->sceneBoundingRect().bottom())
            passedThrough = true;
    }
    const QVariant scale = sim.readValue(fallBody->name(), QStringLiteral("gravityScale"));
    sim.stop();

    EXPECT_TRUE(passedThrough) << "the box passed through rather than landing" << " -- " << (QStringLiteral("box top %1, pad bottom %2")
              .arg(faller->sceneBoundingRect().top(), 0, 'f', 0)
              .arg(pad->sceneBoundingRect().bottom(), 0, 'f', 0)).toStdString();
    EXPECT_TRUE(scale.isValid() && qFuzzyIsNull(scale.toDouble())) << "entering the sensor fired the rule" << " -- " << (scale.isValid() ? QStringLiteral("gravityScale %1").arg(scale.toDouble())
                          : QStringLiteral("unreadable")).toStdString();
}
