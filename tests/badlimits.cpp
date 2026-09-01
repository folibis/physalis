#include "CanvasScene.h"
#include "PhysicsBody.h"
#include "ShapeItem.h"
#include "RectangleItem.h"
#include "Joint.h"
#include "SimulationController.h"

#include <QApplication>
#include <gtest/gtest.h>
#include <QVariantMap>
#include <cstdio>

// Every limit a user can type that Box2D asserts on. Each one used to take the
// whole application down at Simulate.
struct Case
{
    const char *type;
    const char *lowerKey;
    const char *upperKey;
    double lower;
    double upper;
    const char *what;
};

TEST(BadLimits, Behaves)
{
    const Case cases[] = {
        {"prismatic", "lowerTranslation", "upperTranslation",   5,   -5, "lower above upper"},
        {"prismatic", "lowerTranslation", "upperTranslation", 500, -500, "far apart, backwards"},
        {"wheel",     "lowerTranslation", "upperTranslation",  20,  -20, "lower above upper"},
        {"revolute",  "lowerAngle",       "upperAngle",         90,  -90, "lower above upper"},
        {"revolute",  "lowerAngle",       "upperAngle",       -400,  400, "beyond +/-180 deg"},
        {"revolute",  "lowerAngle",       "upperAngle",        400, -400, "beyond, and backwards"},
    };

    for (const Case &c : cases) {
        CanvasScene scene;
        auto *a = new RectangleItem;
        a->setRect(QRectF(0, 0, 60, 40));
        a->setPos(100, 100);
        scene.addItem(a);
        auto *b = new RectangleItem;
        b->setRect(QRectF(0, 0, 60, 40));
        b->setPos(260, 100);
        scene.addItem(b);
        scene.notifyShapesChanged();
        scene.setEditorMode(EditorMode::Physics);

        scene.selectForPhysics(a, true);
        PhysicsBody *bodyA = scene.createBodyFromSelection();
        scene.clearPhysicsSelection();
        scene.selectForPhysics(b, true);
        PhysicsBody *bodyB = scene.createBodyFromSelection();
        scene.clearPhysicsSelection();

        QVariantMap params;
        params.insert(QString::fromLatin1(c.lowerKey), c.lower);
        params.insert(QString::fromLatin1(c.upperKey), c.upper);
        params.insert(QStringLiteral("enableLimit"), true);
        Joint *joint = scene.createJoint(QString::fromLatin1(c.type), bodyA, bodyB, 1, params);
        EXPECT_TRUE(joint != nullptr) << "joint created: " << c.type;
        if (!joint)
            continue;
        joint->params() = params;

        SimulationController sim(&scene, nullptr);
        sim.setEngineName(QStringLiteral("Box2D"));
        sim.start();
        for (int i = 0; i < 60; ++i)
            sim.stepFrame();
        const bool made = sim.skippedJoints().isEmpty();
        sim.stop();

        EXPECT_TRUE(made) << "reversed limits neither abort nor drop the joint";
    }
}
