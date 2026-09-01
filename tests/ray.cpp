#include "CanvasScene.h"
#include "MainWindow.h"
#include "PhysicsBody.h"
#include "RayItem.h"
#include "RectangleItem.h"
#include "Rule.h"
#include "SceneSerializer.h"
#include "ShapeItem.h"
#include "SimulationController.h"

#include <QAction>
#include <QApplication>
#include <gtest/gtest.h>

static void settle() { for (int i = 0; i < 20; ++i) QCoreApplication::processEvents(); }

TEST(Ray, Behaves)
{
    MainWindow window;
    window.resize(1200, 800);
    window.show();
    settle();
    auto *scene = window.findChild<CanvasScene *>();
    auto *sim = window.findChild<SimulationController *>();
    scene->setEditorMode(EditorMode::Physics);
    scene->world().gravity = QPointF(0.0, 0.0);

    // A wall 250 px to the right of where the ray will start.
    auto *wall = new RectangleItem;
    wall->setRect(QRectF(0, 0, 40, 400));
    wall->setPos(250, -200);
    wall->setName(QStringLiteral("wall"));
    scene->addItem(wall);
    scene->notifyShapesChanged();
    scene->selectForPhysics(wall, true);
    scene->createBodyFromSelection()->props().type = physics::BodyType::Static;
    scene->clearPhysicsSelection();

    auto *action = window.findChild<QAction *>(QStringLiteral("actionAddRay"));
    EXPECT_TRUE(action != nullptr) << "the toolbar offers Add Ray";
    RayItem *ray = scene->addRay(QPointF(0, 0));
    ray->setAngleDegrees(0.0);      // straight to the right
    ray->setLength(600.0);
    settle();
    EXPECT_TRUE(scene->rays().size() == 1) << "a ray appears";
    EXPECT_TRUE(ray->name().startsWith(QStringLiteral("ray"))) << "it is named";
    EXPECT_TRUE(scene->shapes().size() == 1) << "and it is not a shape";

    sim->start();
    for (int f = 0; f < 5; ++f) sim->stepFrame();
    const QVariant distance = sim->readValue(ray->name(), QStringLiteral("distance"));
    const QVariant hit = sim->readValue(ray->name(), QStringLiteral("hit"));
    EXPECT_TRUE(hit.toBool()) << "it found the wall";
    EXPECT_TRUE(qAbs(distance.toDouble() - 250.0) < 5.0)
        << "and the distance is right -- got " << distance.toDouble();

    // Turned away it sees nothing, and reads its full length.
    ray->setAngleDegrees(180.0);
    for (int f = 0; f < 5; ++f) sim->stepFrame();
    const QVariant away = sim->readValue(ray->name(), QStringLiteral("distance"));
    EXPECT_TRUE(!sim->readValue(ray->name(), QStringLiteral("hit")).toBool())
        << "pointed away it finds nothing";
    EXPECT_TRUE(qFuzzyCompare(away.toDouble(), 600.0))
        << "and reads its full length -- got " << away.toDouble();
    sim->stop();
    settle();
    EXPECT_TRUE(!ray->hasHit()) << "the reading is cleared when the run stops";

    ray->setAngleDegrees(0.0);
    Rule stop;
    stop.subjectName = ray->name();
    stop.conditionKey = QStringLiteral("distance");
    stop.compare = Rule::Compare::Less;
    stop.conditionValue = 300.0;
    stop.targetName = QStringLiteral("wall");
    stop.propertyKey = QStringLiteral("friction");
    stop.op = Rule::Op::Set;
    stop.value = 0.25;
    scene->setRules({ stop });

    sim->start();
    for (int f = 0; f < 10; ++f) sim->stepFrame();
    const QVariant friction = sim->readValue(QStringLiteral("wall"), QStringLiteral("friction"));
    sim->stop();
    EXPECT_TRUE(friction.isValid() && qAbs(friction.toDouble() - 0.25) < 0.001)
        << "the rule fired on the ray -- friction " << friction.toDouble();

    // It names what it sees, so a rule can single out one shape.
    ray->setAngleDegrees(0.0);
    sim->start();
    for (int f = 0; f < 5; ++f) sim->stepFrame();
    EXPECT_TRUE(sim->readValue(ray->name(), QStringLiteral("hitName")).toString()
                == QStringLiteral("wall"))
        << "the ray says what it is looking at";
    sim->stop();

    // Phrased as "ray detects wall", the way a contact rule names the other
    // side -- so it fires on that shape and not on whatever is nearest.
    Rule sees;
    sees.subjectName = ray->name();
    sees.eventId = QStringLiteral("rayDetects");
    sees.conditionValue = QStringLiteral("wall");
    sees.targetName = QStringLiteral("wall");
    sees.propertyKey = QStringLiteral("restitution");
    sees.op = Rule::Op::Set;
    sees.value = 0.75;
    scene->setRules({ sees });

    sim->start();
    for (int f = 0; f < 10; ++f) sim->stepFrame();
    const QVariant bounce = sim->readValue(QStringLiteral("wall"),
                                           QStringLiteral("restitution"));
    sim->stop();
    EXPECT_TRUE(bounce.isValid() && qAbs(bounce.toDouble() - 0.75) < 0.001)
        << "a rule fires on the shape the ray names -- restitution "
        << bounce.toDouble();

    ray->setPos(QPointF(11, 22));
    ray->setAngleDegrees(45.0);
    ray->setLength(123.0);
    QString error;
    SceneSerializer::saveToFile(scene, QStringLiteral("ray.phys"), &error);
    CanvasScene back;
    SceneSerializer::loadFromFile(&back, QStringLiteral("ray.phys"), &error);
    EXPECT_TRUE(back.rays().size() == 1) << "the ray comes back";
    if (!back.rays().isEmpty()) {
        RayItem *loaded = back.rays().first();
        EXPECT_TRUE(loaded->pos() == QPointF(11, 22)) << "with its position";
        EXPECT_TRUE(qFuzzyCompare(loaded->angleDegrees(), 45.0)) << "its angle";
        EXPECT_TRUE(qFuzzyCompare(loaded->length(), 123.0)) << "and its length";
    }

    window.close();
}
