#include "CanvasScene.h"
#include "MainWindow.h"
#include "PhysicsBody.h"
#include "RayItem.h"
#include "RectangleItem.h"
#include "Rule.h"
#include "SceneSerializer.h"
#include "ShapeItem.h"
#include "SimulationController.h"

#include <QApplication>
#include <gtest/gtest.h>

// One object taking its value from another: the thing the rules could not do
// while every value had to be typed.
TEST(ValueSource, Behaves)
{
    MainWindow window;
    window.show();
    for (int i = 0; i < 25; ++i) QCoreApplication::processEvents();
    auto *scene = window.findChild<CanvasScene *>();
    auto *sim = window.findChild<SimulationController *>();
    scene->setEditorMode(EditorMode::Physics);
    scene->world().gravity = QPointF(0.0, 0.0);

    // A wall for the ray to find, and a platform to be moved under the hit.
    auto *wall = new RectangleItem;
    wall->setRect(QRectF(0, 0, 40, 400));
    wall->setPos(300, -200);
    wall->setName(QStringLiteral("wall"));
    scene->addItem(wall);

    auto *platform = new RectangleItem;
    platform->setRect(QRectF(0, 0, 120, 20));
    platform->setPos(-400, -400);
    platform->setName(QStringLiteral("platform"));
    scene->addItem(platform);
    scene->notifyShapesChanged();

    scene->selectForPhysics(wall, true);
    scene->createBodyFromSelection()->props().type = physics::BodyType::Static;
    scene->clearPhysicsSelection();
    scene->selectForPhysics(platform, true);
    PhysicsBody *platformBody = scene->createBodyFromSelection();
    platformBody->props().type = physics::BodyType::Kinematic;
    scene->clearPhysicsSelection();

    RayItem *ray = scene->addRay(QPointF(-200, 60));
    ray->setAngleDegrees(0.0);
    ray->setLength(900.0);

    // "Put the platform 40 below whatever the ray is looking at."
    Rule follow;
    follow.subjectName = ray->name();
    follow.eventId = QStringLiteral("rayDetects");
    follow.conditionValue = QStringLiteral("wall");
    follow.targetName = platformBody->name();
    follow.propertyKey = QStringLiteral("velocityY");
    follow.op = Rule::Op::Set;
    follow.sourceObject = ray->name();
    follow.sourceProperty = QStringLiteral("hitY");
    follow.sourceOffset = 40.0;
    scene->setRules({ follow });

    sim->start();
    for (int f = 0; f < 20; ++f) sim->stepFrame();
    const QVariant placed = sim->readValue(platformBody->name(),
                                           QStringLiteral("velocityY"));
    const QVariant hitY = sim->readValue(ray->name(), QStringLiteral("hitY"));
    sim->stop();

    EXPECT_TRUE(hitY.isValid() && placed.isValid()) << "both values are readable";
    EXPECT_TRUE(qAbs(placed.toDouble() - (hitY.toDouble() + 40.0)) < 2.0)
        << "the platform took the ray reading plus 40 -- hit " << hitY.toDouble()
        << ", platform " << placed.toDouble();

    // Placing a body outright, which is what "move it there" means: the
    // property is settable, and a rule can drive it from another object.
    Rule place;
    place.subjectName = ray->name();
    place.eventId = QStringLiteral("rayDetects");
    place.conditionValue = QStringLiteral("wall");
    place.targetName = platformBody->name();
    place.propertyKey = QStringLiteral("positionX");
    place.op = Rule::Op::Set;
    place.sourceObject = ray->name();
    place.sourceProperty = QStringLiteral("hitX");
    scene->setRules({ place });

    sim->start();
    for (int f = 0; f < 20; ++f) sim->stepFrame();
    const QVariant movedTo = sim->readValue(platformBody->name(),
                                            QStringLiteral("positionX"));
    const QVariant hitX = sim->readValue(ray->name(), QStringLiteral("hitX"));
    sim->stop();
    EXPECT_TRUE(qAbs(movedTo.toDouble() - hitX.toDouble()) < 2.0)
        << "the body was placed where the ray struck -- hit " << hitX.toDouble()
        << ", body " << movedTo.toDouble();

    // A rule with no source still uses its typed number.
    Rule literal = follow;
    literal.sourceObject.clear();
    literal.sourceProperty.clear();
    literal.value = -123.0;
    scene->setRules({ literal });
    sim->start();
    for (int f = 0; f < 20; ++f) sim->stepFrame();
    const QVariant typed = sim->readValue(platformBody->name(),
                                          QStringLiteral("velocityY"));
    sim->stop();
    EXPECT_TRUE(qAbs(typed.toDouble() + 123.0) < 2.0)
        << "a typed value still wins when no source is set -- got " << typed.toDouble();

    // And the source survives a save.
    scene->setRules({ follow });
    QString error;
    EXPECT_TRUE(SceneSerializer::saveToFile(scene, QStringLiteral("valuesource.phys"), &error))
        << "saved -- " << error.toStdString();
    CanvasScene back;
    SceneSerializer::loadFromFile(&back, QStringLiteral("valuesource.phys"), &error);
    ASSERT_TRUE(!back.rules().isEmpty()) << "the rule comes back";
    const Rule &loaded = back.rules().first();
    EXPECT_TRUE(loaded.usesSource()) << "with its source";
    EXPECT_TRUE(loaded.sourceProperty == QStringLiteral("hitY")) << "and the property";
    EXPECT_TRUE(qFuzzyCompare(loaded.sourceOffset, 40.0)) << "and the offset";

    window.close();
}
