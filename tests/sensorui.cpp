#include "CanvasScene.h"
#include "MainWindow.h"
#include "PhysicsBody.h"
#include "RectangleItem.h"
#include "Rule.h"
#include "ShapeItem.h"
#include "SimulationController.h"

#include <QAction>
#include <QApplication>
#include <gtest/gtest.h>
#include <cstdio>

static void settle() { for (int i = 0; i < 20; ++i) QCoreApplication::processEvents(); }

TEST(SensorUi, Behaves)
{
    MainWindow window;
    window.resize(1100, 700);
    window.show();
    settle();
    auto *scene = window.findChild<CanvasScene *>();
    scene->setEditorMode(EditorMode::Physics);
    scene->world().gravity = QPointF(0.0, 9.81);   // not whatever a settings file holds

    // A trigger zone, and a block that will fall through it.
    auto *zone = new RectangleItem;
    zone->setRect(QRectF(0, 0, 300, 40)); zone->setPos(-150, 150);
    zone->setName(QStringLiteral("zone"));
    auto *faller = new RectangleItem;
    faller->setRect(QRectF(0, 0, 40, 40)); faller->setPos(-20, -150);
    faller->setName(QStringLiteral("faller"));
    scene->addItem(zone); scene->addItem(faller);
    scene->notifyShapesChanged();
    settle();

    scene->selectForPhysics(zone, true);
    settle();
    window.findChild<QAction *>(QStringLiteral("actionCreateBody"))->trigger();
    settle();
    // A sensor is a shape with the flag set; there is no separate kind.
    zone->part().isSensor = true;
    zone->part().enableSensorEvents = true;
    zone->body()->props().type = physics::BodyType::Static;
    zone->body()->notifyPropertyChanged();
    scene->clearPhysicsSelection();

    EXPECT_TRUE(zone->body() != nullptr) << "a body was made";
    EXPECT_TRUE(zone->part().isSensor) << "its shape is a sensor";
    EXPECT_TRUE(zone->part().enableSensorEvents) << "and can be seen by things entering";
    EXPECT_TRUE(zone->body() && zone->body()->props().type == physics::BodyType::Static) << "it defaults to Static";

    EXPECT_TRUE(scene->sensorColor() != scene->bodyColor(physics::BodyType::Static)) << "sensors have their own colour" << " -- " << (QStringLiteral("%1 vs %2").arg(scene->sensorColor().name())
              .arg(scene->bodyColor(physics::BodyType::Static).name())).toStdString();

    scene->selectForPhysics(faller, true);
    PhysicsBody *fallBody = scene->createBodyFromSelection();
    faller->part().enableSensorEvents = true;   // the visitor opts in
    scene->clearPhysicsSelection();

    Rule entered;
    entered.subjectName = zone->name();
    entered.eventId = QStringLiteral("sensorBegin");
    entered.conditionValue = faller->name();
    entered.targetName = fallBody->name();
    entered.propertyKey = QStringLiteral("gravityScale");
    entered.op = Rule::Op::Set;
    entered.value = 0.0;
    scene->setRules({ entered });

    auto *sim = window.findChild<SimulationController *>();
    sim->start();
    bool passed = false;
    for (int f = 0; f < 300; ++f) {
        sim->stepFrame();
        if (faller->sceneBoundingRect().top() > zone->sceneBoundingRect().bottom())
            passed = true;
    }
    const QVariant scale = sim->readValue(fallBody->name(), QStringLiteral("gravityScale"));
    sim->stop();
    settle();

    EXPECT_TRUE(passed) << "the block fell through rather than landing";
    EXPECT_TRUE(scale.isValid() && qFuzzyIsNull(scale.toDouble())) << "entering it fired the rule" << " -- " << (scale.isValid() ? QStringLiteral("gravityScale %1").arg(scale.toDouble())
                          : QStringLiteral("unreadable")).toStdString();

    window.close();
}
