// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ruslan Muhlinin. See LICENSE.
#include "CanvasScene.h"
#include "CircleItem.h"
#include "EngineRegistry.h"
#include "ExplosionItem.h"
#include "IPhysicsEngine.h"
#include "PhysicsBody.h"
#include "RayItem.h"
#include "RectangleItem.h"
#include "Rule.h"
#include "SceneSerializer.h"
#include "ShapeItem.h"
#include "SimulationController.h"

#include <QJsonObject>
#include <gtest/gtest.h>

// The three things a scene can sense or push with that are not bodies: rays,
// sensors and explosions. All three are how a scene notices something and
// answers, and all three were the parts of the app with no test at all.

namespace {

struct World {
    CanvasScene scene;
    SimulationController sim { &scene, nullptr };

    explicit World(const QString &engineName, qreal gravity = 9.81)
    {
        scene.setSimulationEngineName(engineName);
        scene.setPixelsPerMeter(physics::kReferencePixelsPerMeter);
        scene.world().params["gravityY"] = gravity;
        scene.setEditorMode(EditorMode::Physics);
        sim.setEngineName(engineName);
    }

    ShapeItem *box(const QString &name, const QRectF &rect, const QPointF &at)
    {
        auto *shape = new RectangleItem;
        shape->setRect(rect);
        shape->setPos(at);
        shape->setName(name);
        scene.addItem(shape);
        return shape;
    }

    PhysicsBody *bodyOf(ShapeItem *shape, physics::BodyType type, const QString &name)
    {
        scene.notifyShapesChanged();
        scene.selectForPhysics(shape, true);
        PhysicsBody *body = scene.createBodyFromSelection();
        body->props().type = type;
        body->setName(name);
        scene.clearPhysicsSelection();
        return body;
    }

    void run(int steps)
    {
        if (sim.state() == SimulationController::State::Stopped)
            sim.start();
        for (int i = 0; i < steps; ++i)
            sim.stepFrame();
    }
};

const char *kEngines[] { "Box2D", "Chipmunk2D" };

} // namespace

// A ray measures the distance to the first thing in its way, and says which
// shape that was. With nothing in the way it reports its own length.
TEST(Sensing, ARayMeasuresWhatItHits)
{
    for (const char *engineName : kEngines) {
        World w { QString::fromLatin1(engineName), 0.0 };
        // A wall 200 units to the right of where the ray starts.
        ShapeItem *wall = w.box(QStringLiteral("wall"), QRectF(0, 0, 40, 400), QPointF(200, -200));
        w.bodyOf(wall, physics::BodyType::Static, QStringLiteral("wallBody"));

        RayItem *ray = w.scene.addRay(QPointF(0, 0));
        ray->setAngleDegrees(0);        // straight out along +x
        ray->setLength(600);

        w.run(2);
        EXPECT_TRUE(ray->hasHit()) << engineName << ": the ray should have found the wall";
        EXPECT_EQ(ray->hitName(), QStringLiteral("wall"))
            << engineName << ": it should say which shape it hit";
        EXPECT_NEAR(ray->distance(), 200.0, 2.0)
            << engineName << ": the wall is 200 units away and the ray read " << ray->distance();
        w.sim.stop();

        // And with the wall out of the way it reaches its full length.
        World empty { QString::fromLatin1(engineName), 0.0 };
        ShapeItem *far = empty.box(QStringLiteral("far"), QRectF(0, 0, 40, 40), QPointF(5000, 0));
        empty.bodyOf(far, physics::BodyType::Static, QStringLiteral("farBody"));
        RayItem *clear = empty.scene.addRay(QPointF(0, 0));
        clear->setAngleDegrees(0);
        clear->setLength(600);
        empty.run(2);
        EXPECT_FALSE(clear->hasHit()) << engineName << ": there is nothing within reach";
        EXPECT_NEAR(clear->distance(), 600.0, 0.001)
            << engineName << ": with nothing hit it reads its own length";
        empty.sim.stop();
    }
}

// A ray only sees what its mask lets it see.
TEST(Sensing, ARayIgnoresWhatItsMaskExcludes)
{
    for (const char *engineName : kEngines) {
        World w { QString::fromLatin1(engineName), 0.0 };
        ShapeItem *wall = w.box(QStringLiteral("wall"), QRectF(0, 0, 40, 400), QPointF(200, -200));
        // On category two, so a ray masked to category one looks through it.
        wall->part().params["categoryBits"] = 2.0;
        w.bodyOf(wall, physics::BodyType::Static, QStringLiteral("wallBody"));

        RayItem *ray = w.scene.addRay(QPointF(0, 0));
        ray->setAngleDegrees(0);
        ray->setLength(600);
        ray->setMaskBits(1);

        w.run(2);
        EXPECT_FALSE(ray->hasHit())
            << engineName << ": the wall is not in the ray's mask, and it saw it anyway";
        w.sim.stop();
    }
}

// What a ray sees is a rule's to act on, through the application's own
// "detects" event -- and what it names is the body it saw.
TEST(Sensing, ARuleCanActOnWhatARaySees)
{
    for (const char *engineName : kEngines) {
        World w { QString::fromLatin1(engineName) };
        ShapeItem *faller = w.box(QStringLiteral("faller"), QRectF(0, 0, 40, 40), QPointF(-20, -200));
        w.bodyOf(faller, physics::BodyType::Dynamic, QStringLiteral("fallerBody"));

        // Across the path of the falling box.
        RayItem *ray = w.scene.addRay(QPointF(-200, 100));
        ray->setAngleDegrees(0);
        ray->setLength(400);

        Rule rule;
        rule.conditions[0].subjectName = ray->name();
        rule.conditions[0].eventId = QStringLiteral("rayDetects");
        rule.actions[0].targetName = QStringLiteral("@otherBody");
        rule.actions[0].actionId = QStringLiteral("removeBody");
        w.scene.setRules({ rule });

        w.run(120);
        EXPECT_FALSE(faller->isVisible())
            << engineName << ": the ray saw the box crossing it and the rule should have"
            << " taken it away";
        w.sim.stop();
    }
}

// A sensor notices what enters it and does not stop it: the difference between
// a sensor and a wall, and the thing that makes a pocket a pocket.
TEST(Sensing, ASensorNoticesWhatEntersAndLetsItThrough)
{
    for (const char *engineName : kEngines) {
        auto engine = physics::EngineRegistry::create(QString::fromLatin1(engineName));
        ASSERT_TRUE(engine);
        const QString sensorKey =
            physics::keyForRole(engine->shapeProperties(), physics::PropertyRole::Sensor);
        ASSERT_FALSE(sensorKey.isEmpty()) << engineName << " publishes no sensor property";

        World w { QString::fromLatin1(engineName) };
        ShapeItem *pocket = w.box(QStringLiteral("pocket"), QRectF(0, 0, 200, 40), QPointF(-100, 200));
        pocket->part().params[sensorKey] = true;
        w.bodyOf(pocket, physics::BodyType::Static, QStringLiteral("pocketBody"));

        ShapeItem *ball = w.box(QStringLiteral("ball"), QRectF(0, 0, 40, 40), QPointF(-20, -100));
        PhysicsBody *ballBody = w.bodyOf(ball, physics::BodyType::Dynamic, QStringLiteral("ballBody"));

        // A rule on the sensor being entered, so the engine switches sensor
        // events on for everything -- which is what the application relies on.
        Rule rule;
        rule.conditions[0].subjectName = pocket->name();
        rule.conditions[0].eventId = QStringLiteral("sensorBegin");
        rule.actions[0].targetName = ballBody->name();
        rule.actions[0].actionId = Rule::initStateAction();
        w.scene.setRules({ rule });

        // It falls, enters the sensor, and the rule sends it back to the top.
        w.run(240);
        EXPECT_LT(ball->pos().y(), 200.0)
            << engineName << ": entering the sensor should have fired the rule; the ball is at "
            << ball->pos().y();
        w.sim.stop();
    }
}

// The same shape as a wall stops the ball, which is what proves the sensor
// above let it through rather than the ball never arriving.
TEST(Sensing, TheSameShapeAsAWallStopsIt)
{
    for (const char *engineName : kEngines) {
        World w { QString::fromLatin1(engineName) };
        ShapeItem *wall = w.box(QStringLiteral("wall"), QRectF(0, 0, 200, 40), QPointF(-100, 200));
        w.bodyOf(wall, physics::BodyType::Static, QStringLiteral("wallBody"));
        ShapeItem *ball = w.box(QStringLiteral("ball"), QRectF(0, 0, 40, 40), QPointF(-20, -100));
        w.bodyOf(ball, physics::BodyType::Dynamic, QStringLiteral("ballBody"));

        w.run(240);
        EXPECT_NEAR(ball->pos().y() + 40.0, 200.0, 2.0)
            << engineName << ": a solid shape should have stopped the ball on top of it";
        w.sim.stop();
    }
}

// An explosion pushes what is near it, and pushes what is nearer harder.
TEST(Sensing, AnExplosionPushesWhatIsNearestHardest)
{
    for (const char *engineName : kEngines) {
        World w { QString::fromLatin1(engineName), 0.0 };
        ShapeItem *near_ = w.box(QStringLiteral("near"), QRectF(0, 0, 40, 40), QPointF(80, -20));
        ShapeItem *far = w.box(QStringLiteral("far"), QRectF(0, 0, 40, 40), QPointF(240, -20));
        w.bodyOf(near_, physics::BodyType::Dynamic, QStringLiteral("nearBody"));
        w.bodyOf(far, physics::BodyType::Dynamic, QStringLiteral("farBody"));

        ExplosionItem *blast = w.scene.addExplosion(QPointF(0, 0));
        blast->setParam(QStringLiteral("radius"), 400.0);
        blast->setParam(QStringLiteral("impulsePerLength"), 50.0);

        Rule rule;
        rule.conditions[0].subjectName = Rule::world();
        rule.conditions[0].eventId = Rule::runStartedEvent();
        rule.actions[0].targetName = blast->name();
        rule.actions[0].actionId = QStringLiteral("explode");
        w.scene.setRules({ rule });

        const qreal nearFrom = near_->pos().x();
        const qreal farFrom = far->pos().x();
        w.run(60);
        const qreal nearWent = near_->pos().x() - nearFrom;
        const qreal farWent = far->pos().x() - farFrom;
        w.sim.stop();

        EXPECT_GT(nearWent, 0.0)
            << engineName << ": the explosion should have pushed the near box away from it";
        EXPECT_GT(nearWent, farWent)
            << engineName << ": the near box went " << nearWent << " and the far one "
            << farWent << " -- an explosion falls off with distance";
    }
}

// Rays and explosions are part of the document, with everything they carry.
TEST(Sensing, RaysAndExplosionsSurviveTheFile)
{
    CanvasScene scene;
    scene.setSimulationEngineName(QStringLiteral("Box2D"));

    RayItem *ray = scene.addRay(QPointF(-150, 75));
    ray->setName(QStringLiteral("tripwire"));
    ray->setAngleDegrees(35);
    ray->setLength(480);
    ray->setMaskBits(0xF0F0);

    ExplosionItem *blast = scene.addExplosion(QPointF(60, -40));
    blast->setName(QStringLiteral("charge"));
    blast->setParam(QStringLiteral("radius"), 275.0);

    const QJsonObject document = SceneSerializer::save(&scene);
    CanvasScene reopened;
    QString error;
    ASSERT_TRUE(SceneSerializer::load(&reopened, document, &error)) << error.toStdString();

    ASSERT_EQ(reopened.rays().size(), 1);
    RayItem *loaded = reopened.rays().first();
    EXPECT_EQ(loaded->name(), QStringLiteral("tripwire"));
    EXPECT_NEAR(loaded->angleDegrees(), 35.0, 1e-6);
    EXPECT_NEAR(loaded->length(), 480.0, 1e-6);
    EXPECT_EQ(loaded->maskBits(), quint64(0xF0F0));
    EXPECT_NEAR(loaded->pos().x(), -150.0, 1e-6);
    EXPECT_NEAR(loaded->pos().y(), 75.0, 1e-6);

    ASSERT_EQ(reopened.explosions().size(), 1);
    ExplosionItem *loadedBlast = reopened.explosions().first();
    EXPECT_EQ(loadedBlast->name(), QStringLiteral("charge"));
    EXPECT_NEAR(loadedBlast->params().value(QStringLiteral("radius")).toDouble(), 275.0, 1e-6);
    EXPECT_NEAR(loadedBlast->pos().x(), 60.0, 1e-6);
    EXPECT_NEAR(loadedBlast->pos().y(), -40.0, 1e-6);
}
