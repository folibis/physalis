#include "CanvasScene.h"
#include "PhysicsBody.h"
#include "RectangleItem.h"
#include "SceneSerializer.h"
#include "ShapeItem.h"
#include "SimulationController.h"

#include <QApplication>
#include <gtest/gtest.h>
#include <cstdio>

// Drops a ball onto the ground and reports how high it came back.
static qreal bounceHeight(qreal restitutionThreshold, bool *ok)
{
    CanvasScene scene;
    scene.setSimulationEngineName(QStringLiteral("Box2D"));
    scene.world().restitutionThreshold = restitutionThreshold;

    auto *ground = new RectangleItem;
    ground->setRect(QRectF(0, 0, 800, 40));
    ground->setPos(-400, 300);
    ground->setName(QStringLiteral("ground"));
    auto *ball = new RectangleItem;
    ball->setRect(QRectF(0, 0, 40, 40));
    ball->setPos(-20, 0);
    ball->setName(QStringLiteral("ball"));
    ball->part().material.restitution = 0.9;
    scene.addItem(ground);
    scene.addItem(ball);
    scene.notifyShapesChanged();
    scene.setEditorMode(EditorMode::Physics);

    scene.selectForPhysics(ground, true);
    scene.createBodyFromSelection()->props().type = physics::BodyType::Static;
    scene.clearPhysicsSelection();
    scene.selectForPhysics(ball, true);
    scene.createBodyFromSelection();
    scene.clearPhysicsSelection();

    SimulationController sim(&scene, nullptr);
    sim.setEngineName(QStringLiteral("Box2D"));
    sim.start();
    // The ball is falling (top increasing). The first frame it starts rising
    // again is the bounce; how far it climbs after that is the rebound.
    qreal previous = ball->sceneBoundingRect().top();
    qreal deepest = previous, reboundTop = 1e9;
    bool landed = false;
    for (int f = 0; f < 400; ++f) {
        sim.stepFrame();
        const qreal top = ball->sceneBoundingRect().top();
        if (!landed) {
            if (f > 5 && top < previous - 0.01) {   // started coming back up
                landed = true;
                deepest = previous;
                reboundTop = top;
            }
            previous = top;
        } else {
            reboundTop = qMin(reboundTop, top);
        }
    }
    if (!landed) {          // never rose again: it landed and stayed put
        landed = previous > ball->rect().height();
        deepest = previous;
        reboundTop = previous;
    }
    sim.stop();
    *ok = landed;
    return landed ? (deepest - reboundTop) : 0.0;   // how far back up it came
}

TEST(WorldTune, Behaves)
{

    bool landedLow = false, landedHigh = false;
    // Threshold well under the impact speed: restitution applies, it bounces.
    const qreal withBounce = bounceHeight(0.01, &landedLow);
    // Threshold far above it: Box2D ignores restitution, it should not bounce.
    const qreal without = bounceHeight(50.0, &landedHigh);

    EXPECT_TRUE(landedLow && landedHigh) << "both runs actually landed";
    EXPECT_TRUE(withBounce > 20.0) << "a low restitution threshold lets it bounce";
    EXPECT_TRUE(without < withBounce / 4.0) << "a high one suppresses the bounce";

    CanvasScene scene;
    scene.world().restitutionThreshold = 0.25;
    scene.world().hitEventThreshold = 2.5;
    scene.world().contactHertz = 45.0;
    scene.world().contactDampingRatio = 4.0;
    scene.world().maxContactPushSpeed = 7.0;
    scene.world().maximumLinearSpeed = 123.0;
    scene.world().enableSleep = false;
    scene.world().enableContinuous = false;

    const QString path = QStringLiteral("worldtune.phys");
    QString error;
    SceneSerializer::saveToFile(&scene, path, &error);
    CanvasScene back;
    SceneSerializer::loadFromFile(&back, path, &error);

    EXPECT_TRUE(qFuzzyCompare(back.world().restitutionThreshold, 0.25)) << "restitution threshold";
    EXPECT_TRUE(qFuzzyCompare(back.world().hitEventThreshold, 2.5)) << "hit event threshold";
    EXPECT_TRUE(qFuzzyCompare(back.world().contactHertz, 45.0)) << "contact stiffness";
    EXPECT_TRUE(qFuzzyCompare(back.world().contactDampingRatio, 4.0)) << "contact damping";
    EXPECT_TRUE(qFuzzyCompare(back.world().maxContactPushSpeed, 7.0)) << "max push speed";
    EXPECT_TRUE(qFuzzyCompare(back.world().maximumLinearSpeed, 123.0)) << "max speed";
    EXPECT_TRUE(back.world().enableSleep == false) << "allow sleeping";
    EXPECT_TRUE(back.world().enableContinuous == false) << "continuous collision";

    CanvasScene old;
    const physics::WorldDesc factory;
    EXPECT_TRUE(qFuzzyCompare(old.world().contactHertz, factory.contactHertz)
              && qFuzzyCompare(old.world().restitutionThreshold, factory.restitutionThreshold)) << "missing keys fall back to Box2D's defaults" << " -- " << (QStringLiteral("hertz %1, threshold %2")
              .arg(old.world().contactHertz).arg(old.world().restitutionThreshold)).toStdString();
}
