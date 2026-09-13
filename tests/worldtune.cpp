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
    scene.world().params["restitutionThreshold"] = restitutionThreshold;

    auto *ground = new RectangleItem;
    ground->setRect(QRectF(0, 0, 800, 40));
    ground->setPos(-400, 300);
    ground->setName(QStringLiteral("ground"));
    auto *ball = new RectangleItem;
    ball->setRect(QRectF(0, 0, 40, 40));
    ball->setPos(-20, 0);
    ball->setName(QStringLiteral("ball"));
    ball->part().params["restitution"] = 0.9;
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
    scene.world().params["restitutionThreshold"] = 0.25;
    scene.world().params["hitEventThreshold"] = 2.5;
    scene.world().params["contactHertz"] = 45.0;
    scene.world().params["contactDampingRatio"] = 4.0;
    scene.world().params["maxContactPushSpeed"] = 7.0;
    scene.world().params["maximumLinearSpeed"] = 123.0;
    scene.world().params["enableSleep"] = false;
    scene.world().params["enableContinuous"] = false;

    const QString path = QStringLiteral("worldtune.phys");
    QString error;
    SceneSerializer::saveToFile(&scene, path, &error);
    CanvasScene back;
    SceneSerializer::loadFromFile(&back, path, &error);

    EXPECT_TRUE(qFuzzyCompare(back.world().params["restitutionThreshold"].toDouble(), 0.25)) << "restitution threshold";
    EXPECT_TRUE(qFuzzyCompare(back.world().params["hitEventThreshold"].toDouble(), 2.5)) << "hit event threshold";
    EXPECT_TRUE(qFuzzyCompare(back.world().params["contactHertz"].toDouble(), 45.0)) << "contact stiffness";
    EXPECT_TRUE(qFuzzyCompare(back.world().params["contactDampingRatio"].toDouble(), 4.0)) << "contact damping";
    EXPECT_TRUE(qFuzzyCompare(back.world().params["maxContactPushSpeed"].toDouble(), 7.0)) << "max push speed";
    EXPECT_TRUE(qFuzzyCompare(back.world().params["maximumLinearSpeed"].toDouble(), 123.0)) << "max speed";
    EXPECT_TRUE(back.world().params["enableSleep"].toBool() == false) << "allow sleeping";
    EXPECT_TRUE(back.world().params["enableContinuous"].toBool() == false) << "continuous collision";

    // What an untouched scene carries: nothing at all, so every one of these
    // falls through to what the engine says it starts as.
    CanvasScene old;
    EXPECT_TRUE(old.world().params.isEmpty())
        << "an untouched scene holds no settings of its own -- " << old.world().params.size()
        << " kept";
}
