// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ruslan Muhlinin. See LICENSE.
#include "CanvasScene.h"
#include "CircleItem.h"
#include "PhysicsBody.h"
#include "RectangleItem.h"
#include "ShapeItem.h"
#include "SimulationController.h"

#include <QString>
#include <cmath>
#include <cstdio>
#include <gtest/gtest.h>

// Does the physics do what physics does. Everything else in this suite proves
// that a number reaches the solver; this proves the solver is then right --
// against arithmetic, not against what it did last time. A run that is wrong
// in the same way every time passes a golden-value test for ever.
//
// Each case is checked on both engines, because a scene is meant to mean the
// same thing whichever one is loaded.

namespace {

const qreal kGravity = 9.81;        // m/s/s
// The scale the engines treat as 1:1, so a metre is fifty scene units and the
// arithmetic below can be done in metres and compared in pixels.
const qreal kPpm = physics::kReferencePixelsPerMeter;

struct Scene {
    CanvasScene scene;
    SimulationController sim { &scene, nullptr };

    explicit Scene(const QString &engineName)
    {
        scene.setSimulationEngineName(engineName);
        scene.setPixelsPerMeter(kPpm);
        scene.world().params["gravityY"] = kGravity;
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

    ShapeItem *ball(const QString &name, qreal radius, const QPointF &centre)
    {
        auto *shape = new CircleItem;
        shape->setRect(QRectF(0, 0, radius * 2.0, radius * 2.0));
        shape->setPos(centre - QPointF(radius, radius));
        shape->setName(name);
        scene.addItem(shape);
        return shape;
    }

    PhysicsBody *bodyOf(ShapeItem *shape, physics::BodyType type)
    {
        scene.notifyShapesChanged();
        scene.selectForPhysics(shape, true);
        PhysicsBody *body = scene.createBodyFromSelection();
        body->props().type = type;
        scene.clearPhysicsSelection();
        return body;
    }

    void run(qreal seconds)
    {
        if (sim.state() == SimulationController::State::Stopped)
            sim.start();
        const int steps = qRound(seconds * 60.0);
        for (int i = 0; i < steps; ++i)
            sim.stepFrame();
    }
};

// Where a body dropped from rest is after `steps` steps, the way a solver that
// integrates velocity first and then position arrives at it. That is one step
// of gravity more than the textbook ½gt², and at sixty steps a second the
// difference is under a percent -- but it is the number to compare against
// rather than a number that merely looks close.
qreal fallAfter(int steps, qreal gravity = kGravity, qreal dt = 1.0 / 60.0)
{
    return gravity * dt * dt * (qreal(steps) * (steps + 1) / 2.0) * kPpm;
}

// Textbook free fall, which is what the two of them straddle: Box2D adds
// gravity on every one of the sixty steps and lands a step above this,
// Chipmunk adds it on fifty-nine and lands a step below. Half a step either
// way at sixty steps a second, so the thing worth asserting is the physics,
// not one engine's convention about when in the step gravity arrives.
qreal textbookFall(qreal seconds, qreal gravity = kGravity)
{
    return 0.5 * gravity * seconds * seconds * kPpm;
}

const char *kEngines[] { "Box2D", "Chipmunk2D" };

} // namespace

// A body in free fall covers what gravity says it covers -- so gravity is
// applied once per step, at the strength the world asked for, in the units the
// scene is drawn in.
TEST(Physics, AThingFallsAtTheRateGravitySays)
{
    QVector<qreal> fellOn;
    for (const char *engineName : kEngines) {
        Scene s { QString::fromLatin1(engineName) };
        ShapeItem *shape = s.box(QStringLiteral("faller"), QRectF(0, 0, 40, 40), QPointF(0, 0));
        s.bodyOf(shape, physics::BodyType::Dynamic);

        const qreal startY = shape->pos().y();
        s.run(1.0);
        const qreal fell = shape->pos().y() - startY;
        fellOn.append(fell);

        // Within half a step of the textbook answer, which is the most either
        // integration convention can be out by.
        const qreal expected = textbookFall(1.0);
        const qreal halfAStep = qAbs(fallAfter(60) - expected) * 1.2;
        EXPECT_NEAR(fell, expected, halfAStep)
            << engineName << ": after a second of falling, ½gt² is " << expected
            << " scene units and it covered " << fell;

        // And four seconds in, where the step convention is lost in the noise,
        // it is the textbook answer to within a percent.
        s.run(3.0);
        const qreal after4s = shape->pos().y() - startY;
        EXPECT_NEAR(after4s, textbookFall(4.0), textbookFall(4.0) * 0.01)
            << engineName << ": after four seconds it should be at " << textbookFall(4.0);
        s.sim.stop();
    }

    ASSERT_EQ(fellOn.size(), 2);
    EXPECT_NEAR(fellOn[0], fellOn[1], fellOn[0] * 0.05)
        << "the two engines dropped the same body by different amounts in one second: "
        << fellOn[0] << " against " << fellOn[1];
}

// Twice the gravity, twice the fall; no gravity, no fall. Gravity Scale is a
// property of the body and has to mean the same thing.
TEST(Physics, GravityScaleMultipliesTheFall)
{
    for (const char *engineName : kEngines) {
        Scene s { QString::fromLatin1(engineName) };
        ShapeItem *normal = s.box(QStringLiteral("normal"), QRectF(0, 0, 40, 40), QPointF(0, 0));
        ShapeItem *heavy = s.box(QStringLiteral("double"), QRectF(0, 0, 40, 40), QPointF(200, 0));
        ShapeItem *floaty = s.box(QStringLiteral("none"), QRectF(0, 0, 40, 40), QPointF(400, 0));
        s.bodyOf(normal, physics::BodyType::Dynamic);
        s.bodyOf(heavy, physics::BodyType::Dynamic)->props().params["gravityScale"] = 2.0;
        s.bodyOf(floaty, physics::BodyType::Dynamic)->props().params["gravityScale"] = 0.0;

        s.run(1.0);
        const qreal one = normal->pos().y();
        const qreal two = heavy->pos().y();

        EXPECT_NEAR(two, one * 2.0, qAbs(one) * 0.03)
            << engineName << ": twice the gravity scale is twice the fall";
        EXPECT_NEAR(floaty->pos().y(), 0.0, 1.0)
            << engineName << ": no gravity scale means it stays where it was put";
        s.sim.stop();
    }
}

// Heavy and light fall together: mass cancels out of free fall, and a solver
// that let it leak in would be wrong in a way nobody would think to look for.
TEST(Physics, MassDoesNotChangeHowFastSomethingFalls)
{
    for (const char *engineName : kEngines) {
        Scene s { QString::fromLatin1(engineName) };
        ShapeItem *light = s.box(QStringLiteral("light"), QRectF(0, 0, 40, 40), QPointF(0, 0));
        ShapeItem *heavy = s.box(QStringLiteral("heavy"), QRectF(0, 0, 40, 40), QPointF(200, 0));
        light->part().params["density"] = 0.2;
        heavy->part().params["density"] = 20.0;
        s.bodyOf(light, physics::BodyType::Dynamic);
        s.bodyOf(heavy, physics::BodyType::Dynamic);

        s.run(1.0);
        EXPECT_NEAR(light->pos().y(), heavy->pos().y(), 1.0)
            << engineName << ": a hundredfold difference in density fell differently";
        s.sim.stop();
    }
}

// A body dropped on the ground stops on top of it: it does not sink into it,
// and it does not wander sideways with nothing pushing it.
TEST(Physics, AThingDroppedOnTheGroundRestsOnIt)
{
    for (const char *engineName : kEngines) {
        Scene s { QString::fromLatin1(engineName) };
        ShapeItem *ground = s.box(QStringLiteral("ground"), QRectF(0, 0, 800, 40), QPointF(-400, 300));
        ShapeItem *crate = s.box(QStringLiteral("crate"), QRectF(0, 0, 40, 40), QPointF(-20, 0));
        s.bodyOf(ground, physics::BodyType::Static);
        s.bodyOf(crate, physics::BodyType::Dynamic);

        s.run(3.0);
        // Its underside should be within a few tenths of a scene unit of the
        // ground's top: solvers leave a little overlap on purpose, but a body
        // that keeps sinking has no contact at all.
        const qreal underside = crate->pos().y() + 40.0;
        EXPECT_NEAR(underside, 300.0, 2.0)
            << engineName << ": the crate came to rest with its underside at " << underside
            << ", and the ground is at 300";
        EXPECT_NEAR(crate->pos().x(), -20.0, 1.0)
            << engineName << ": nothing pushed it sideways, so it should not have moved sideways";
        s.sim.stop();
    }
}

// A bouncy ball comes back to about the square of its bounciness times the
// height it fell from -- the definition of restitution, which is otherwise a
// number that can be set to anything with nobody the wiser.
TEST(Physics, BouncinessReturnsTheRightFractionOfTheDrop)
{
    for (const char *engineName : kEngines) {
        Scene s { QString::fromLatin1(engineName) };
        ShapeItem *ground = s.box(QStringLiteral("ground"), QRectF(0, 0, 800, 40), QPointF(-400, 300));
        ShapeItem *ball = s.ball(QStringLiteral("ball"), 20, QPointF(0, 0));
        ball->part().params["restitution"] = 0.8;
        ball->part().params["friction"] = 0.0;
        s.bodyOf(ground, physics::BodyType::Static);
        s.bodyOf(ball, physics::BodyType::Dynamic);

        const qreal startCentre = ball->pos().y() + 20.0;
        const qreal dropHeight = 300.0 - 20.0 - startCentre;   // centre to resting centre

        // Long enough to land and come back up to the top of the first bounce.
        qreal highest = 1e9;
        s.sim.start();
        bool landed = false;
        for (int i = 0; i < 240; ++i) {
            s.sim.stepFrame();
            const qreal centre = ball->pos().y() + 20.0;
            if (centre > 300.0 - 20.0 - 2.0)
                landed = true;
            if (landed)
                highest = qMin(highest, centre);
        }
        ASSERT_TRUE(landed) << engineName << ": the ball never reached the ground";

        const qreal bounced = (300.0 - 20.0) - highest;
        const qreal expected = dropHeight * 0.8 * 0.8;
        EXPECT_GT(bounced, expected * 0.6)
            << engineName << ": a bounciness of 0.8 should return about " << expected
            << " of a " << dropHeight << " drop, and it returned " << bounced;
        EXPECT_LT(bounced, expected * 1.4)
            << engineName << ": it came back higher than bounciness allows";
        s.sim.stop();
    }
}

// Friction stops a sliding box, and more friction stops it sooner. Without
// this, friction is a number that goes into the engine and is never looked at.
TEST(Physics, FrictionSlowsASlidingBox)
{
    for (const char *engineName : kEngines) {
        Scene s { QString::fromLatin1(engineName) };
        ShapeItem *ground = s.box(QStringLiteral("ground"), QRectF(0, 0, 2000, 40), QPointF(-1000, 300));
        ground->part().params["friction"] = 0.5;
        s.bodyOf(ground, physics::BodyType::Static);

        // Side by side and both already sitting on the ground: stacked one
        // above the other they land on each other instead of sliding, which
        // is a test of nothing.
        ShapeItem *slippy = s.box(QStringLiteral("slippy"), QRectF(0, 0, 40, 40), QPointF(-800, 260));
        ShapeItem *grippy = s.box(QStringLiteral("grippy"), QRectF(0, 0, 40, 40), QPointF(100, 260));
        slippy->part().params["friction"] = 0.0;
        grippy->part().params["friction"] = 1.0;
        PhysicsBody *slippyBody = s.bodyOf(slippy, physics::BodyType::Dynamic);
        PhysicsBody *grippyBody = s.bodyOf(grippy, physics::BodyType::Dynamic);
        // Both pushed along the ground at the same speed.
        slippyBody->props().params["velocityX"] = 4.0;
        grippyBody->props().params["velocityX"] = 4.0;

        const qreal slippyFrom = slippy->pos().x();
        const qreal grippyFrom = grippy->pos().x();
        s.run(2.0);
        const qreal slippyWent = slippy->pos().x() - slippyFrom;
        const qreal grippyWent = grippy->pos().x() - grippyFrom;

        EXPECT_GT(slippyWent, 0.0) << engineName << ": the frictionless box slid somewhere";
        EXPECT_GT(slippyWent, grippyWent * 1.2)
            << engineName << ": friction 0 went " << slippyWent << " and friction 1 went "
            << grippyWent << " -- friction made no difference";
        s.sim.stop();
    }
}

// Linear damping bleeds speed away, and none leaves it alone. A body with no
// gravity and no contacts is the cleanest place to see it.
TEST(Physics, DampingBleedsSpeedAway)
{
    for (const char *engineName : kEngines) {
        Scene s { QString::fromLatin1(engineName) };
        s.scene.world().params["gravityY"] = 0.0;
        ShapeItem *free_ = s.box(QStringLiteral("free"), QRectF(0, 0, 40, 40), QPointF(0, 0));
        ShapeItem *damped = s.box(QStringLiteral("damped"), QRectF(0, 0, 40, 40), QPointF(0, 200));
        PhysicsBody *freeBody = s.bodyOf(free_, physics::BodyType::Dynamic);
        PhysicsBody *dampedBody = s.bodyOf(damped, physics::BodyType::Dynamic);
        freeBody->props().params["velocityX"] = 2.0;
        dampedBody->props().params["velocityX"] = 2.0;
        dampedBody->props().params["linearDamping"] = 2.0;

        s.run(1.0);
        const qreal undamped = free_->pos().x();
        const qreal slowed = damped->pos().x();

        EXPECT_GT(undamped, 0.0) << engineName << ": the undamped body kept going";
        EXPECT_LT(slowed, undamped * 0.8)
            << engineName << ": damping 2 should visibly shorten the distance; " << slowed
            << " against " << undamped;
        s.sim.stop();
    }
}

// The same scene run twice gives the same answer. Without this nothing else
// here means anything, and an export can never be compared against the app.
TEST(Physics, TheSameSceneTwiceGivesTheSameAnswer)
{
    for (const char *engineName : kEngines) {
        QVector<QPointF> ends;
        for (int attempt = 0; attempt < 2; ++attempt) {
            Scene s { QString::fromLatin1(engineName) };
            ShapeItem *ground = s.box(QStringLiteral("ground"), QRectF(0, 0, 800, 40), QPointF(-400, 300));
            ShapeItem *ball = s.ball(QStringLiteral("ball"), 20, QPointF(-30, -100));
            ball->part().params["restitution"] = 0.6;
            s.bodyOf(ground, physics::BodyType::Static);
            s.bodyOf(ball, physics::BodyType::Dynamic);
            s.run(2.0);
            ends.append(ball->pos());
            s.sim.stop();
        }
        EXPECT_NEAR(ends[0].x(), ends[1].x(), 1e-6) << engineName << " is not repeatable";
        EXPECT_NEAR(ends[0].y(), ends[1].y(), 1e-6) << engineName << " is not repeatable";
    }
}

// The same scene at a different scale behaves the same: the scene's own units
// are what the user works in, and pixels per metre is meant to change what a
// metre means, not how the scene plays.
TEST(Physics, TheSceneScaleDoesNotChangeHowItPlays)
{
    for (const char *engineName : kEngines) {
        QVector<qreal> fell;
        for (qreal ppm : { 50.0, 500.0 }) {
            CanvasScene scene;
            scene.setSimulationEngineName(QString::fromLatin1(engineName));
            scene.setPixelsPerMeter(ppm);
            scene.world().params["gravityY"] = kGravity;
            scene.setEditorMode(EditorMode::Physics);

            auto *shape = new RectangleItem;
            shape->setRect(QRectF(0, 0, 40, 40));
            shape->setPos(0, 0);
            shape->setName(QStringLiteral("faller"));
            scene.addItem(shape);
            scene.notifyShapesChanged();
            scene.selectForPhysics(shape, true);
            scene.createBodyFromSelection()->props().type = physics::BodyType::Dynamic;
            scene.clearPhysicsSelection();

            SimulationController sim(&scene, nullptr);
            sim.setEngineName(QString::fromLatin1(engineName));
            sim.start();
            for (int i = 0; i < 60; ++i)
                sim.stepFrame();
            fell.append(shape->pos().y());
            sim.stop();
        }
        EXPECT_NEAR(fell[0], fell[1], qAbs(fell[0]) * 0.02)
            << engineName << ": the same scene fell " << fell[0] << " at 50 px per metre and "
            << fell[1] << " at 500 -- the scale changed the physics";
    }
}

// The two engines are meant to be interchangeable for a scene that uses only
// what both of them have. They will not agree exactly -- different solvers --
// but they have to agree about what happens.
TEST(Physics, BothEnginesAgreeOnTheSameScene)
{
    QVector<qreal> restingY;
    for (const char *engineName : kEngines) {
        Scene s { QString::fromLatin1(engineName) };
        ShapeItem *ground = s.box(QStringLiteral("ground"), QRectF(0, 0, 800, 40), QPointF(-400, 300));
        ShapeItem *crate = s.box(QStringLiteral("crate"), QRectF(0, 0, 40, 40), QPointF(-20, -200));
        s.bodyOf(ground, physics::BodyType::Static);
        s.bodyOf(crate, physics::BodyType::Dynamic);
        s.run(3.0);
        restingY.append(crate->pos().y());
        s.sim.stop();
    }
    ASSERT_EQ(restingY.size(), 2);
    EXPECT_NEAR(restingY[0], restingY[1], 3.0)
        << "Box2D rested the crate at " << restingY[0] << " and Chipmunk at " << restingY[1];
}
