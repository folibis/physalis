// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ruslan Muhlinin. See LICENSE.
#include "CanvasScene.h"
#include "Joint.h"
#include "PhysicsBody.h"
#include "RectangleItem.h"
#include "Rule.h"
#include "ShapeItem.h"
#include "SimulationController.h"

#include <gtest/gtest.h>

// The transport: start, pause, step, speed, stop. None of it is physics -- it
// is what the toolbar does -- and all of it decides what the user sees.

namespace {

struct Falling {
    CanvasScene scene;
    SimulationController sim { &scene, nullptr };
    ShapeItem *faller = nullptr;
    PhysicsBody *body = nullptr;

    Falling()
    {
        scene.setSimulationEngineName(QStringLiteral("Box2D"));
        scene.setPixelsPerMeter(physics::kReferencePixelsPerMeter);
        scene.world().params["gravityY"] = 9.81;
        scene.setEditorMode(EditorMode::Physics);
        sim.setEngineName(QStringLiteral("Box2D"));

        faller = new RectangleItem;
        faller->setRect(QRectF(0, 0, 40, 40));
        faller->setPos(100, -50);
        faller->setName(QStringLiteral("faller"));
        scene.addItem(faller);
        scene.notifyShapesChanged();
        scene.selectForPhysics(faller, true);
        body = scene.createBodyFromSelection();
        body->props().type = physics::BodyType::Dynamic;
        body->setName(QStringLiteral("fallerBody"));
        scene.clearPhysicsSelection();
    }
};

} // namespace

// The states the toolbar switches between, and what each one means.
TEST(Simulation, StartPauseResumeStop)
{
    Falling r;
    EXPECT_FALSE(r.sim.isActive()) << "nothing is running before Play";

    r.sim.start();
    EXPECT_TRUE(r.sim.isActive());
    EXPECT_TRUE(r.sim.isRunning()) << "Play runs it";

    r.sim.pause();
    EXPECT_TRUE(r.sim.isActive()) << "a paused run is still a run";
    EXPECT_FALSE(r.sim.isRunning()) << "but it is not running";

    const qreal held = r.faller->pos().y();
    for (int i = 0; i < 10; ++i)
        r.sim.advance(1.0 / 60.0);
    EXPECT_NEAR(r.faller->pos().y(), held, 1e-9) << "time does not pass while it is paused";

    r.sim.resume();
    EXPECT_TRUE(r.sim.isRunning()) << "and it picks up again";
    r.sim.advance(0.25);
    EXPECT_GT(r.faller->pos().y(), held) << "it moved once it was resumed";

    r.sim.stop();
    EXPECT_FALSE(r.sim.isActive());
}

// Stop is not just an end: it puts the scene back where it was before Play,
// which is what makes a run safe to try.
TEST(Simulation, StopPutsTheSceneBackWhereItWas)
{
    Falling r;
    const QPointF was = r.faller->pos();
    const qreal wasAngle = r.faller->rotation();

    r.sim.start();
    for (int i = 0; i < 90; ++i)
        r.sim.stepFrame();
    EXPECT_GT(r.faller->pos().y(), was.y() + 50.0) << "it really moved during the run";

    r.sim.stop();
    EXPECT_NEAR(r.faller->pos().x(), was.x(), 1e-9) << "Stop put it back";
    EXPECT_NEAR(r.faller->pos().y(), was.y(), 1e-9);
    EXPECT_NEAR(r.faller->rotation(), wasAngle, 1e-9) << "facing the way it did";
}

// Step advances exactly one step, whatever the speed says, and leaves the run
// paused so the next Step goes on from there.
TEST(Simulation, StepAdvancesOneStep)
{
    Falling oneStep;
    oneStep.sim.start();
    oneStep.sim.stepFrame();
    const qreal afterOne = oneStep.faller->pos().y();
    oneStep.sim.stop();

    Falling twoSteps;
    twoSteps.sim.start();
    twoSteps.sim.stepFrame();
    twoSteps.sim.stepFrame();
    const qreal afterTwo = twoSteps.faller->pos().y();
    EXPECT_GT(afterTwo, afterOne) << "the second step moved it further";
    EXPECT_FALSE(twoSteps.sim.isRunning()) << "stepping leaves it paused, not running";
    twoSteps.sim.stop();

    // At any speed, a step is a step.
    Falling fast;
    fast.sim.setSpeed(4.0);
    fast.sim.start();
    fast.sim.stepFrame();
    EXPECT_NEAR(fast.faller->pos().y(), afterOne, 1e-9)
        << "Step at x4 covered more than one step, which would be a different simulation";
    fast.sim.stop();
}

// Playing faster runs more steps for the same wall-clock time -- it never
// makes the steps bigger, which would be a different simulation rather than
// the same one played faster.
TEST(Simulation, SpeedRunsMoreStepsRatherThanBiggerOnes)
{
    // Fed a tick at a time, the way the timer feeds it -- handed half a second
    // in one lump, both runs hit the catch-up ceiling instead and the test
    // compares two runs that were cut short rather than two speeds.
    const qreal tick = 1.0 / 60.0;

    Falling once;
    once.sim.start();
    for (int i = 0; i < 30; ++i)
        once.sim.advance(tick);
    const qreal atOne = once.faller->pos().y();
    once.sim.stop();

    Falling twice;
    twice.sim.setSpeed(2.0);
    twice.sim.start();
    for (int i = 0; i < 15; ++i)
        twice.sim.advance(tick);
    const qreal atTwo = twice.faller->pos().y();
    twice.sim.stop();

    EXPECT_NEAR(atTwo, atOne, 1e-9)
        << "half the wall time at twice the speed should be the same run: " << atTwo
        << " against " << atOne;
}

// A tick that arrives late does not run the whole gap: there is a ceiling, or
// a window that was dragged for ten seconds would freeze catching up.
TEST(Simulation, ALongGapIsCappedRatherThanRunInFull)
{
    Falling r;
    r.sim.start();
    r.sim.advance(10.0);
    const qreal afterAGap = r.faller->pos().y();
    r.sim.stop();

    Falling full;
    full.sim.start();
    for (int i = 0; i < 600; ++i)
        full.sim.stepFrame();
    const qreal afterTenSeconds = full.faller->pos().y();
    full.sim.stop();

    EXPECT_LT(afterAGap, afterTenSeconds)
        << "ten seconds handed over at once ran the whole ten seconds";
}

// How many steps a second the solver takes is the scene's, and a finer step
// changes the answer it arrives at rather than how fast it gets there.
TEST(Simulation, TheStepRateIsWhatTheSceneAsksFor)
{
    Falling r;
    EXPECT_EQ(r.sim.stepsPerSecond(), 60) << "sixty a second unless asked otherwise";
    r.sim.setStepsPerSecond(120);
    EXPECT_EQ(r.sim.stepsPerSecond(), 120);

    r.sim.start();
    for (int i = 0; i < 120; ++i)
        r.sim.stepFrame();
    const qreal afterASecond = r.faller->pos().y();
    r.sim.stop();

    // A second of falling is a second of falling, whichever rate got there.
    Falling sixty;
    sixty.sim.start();
    for (int i = 0; i < 60; ++i)
        sixty.sim.stepFrame();
    const qreal atSixty = sixty.faller->pos().y();
    sixty.sim.stop();

    EXPECT_NEAR(afterASecond, atSixty, qAbs(atSixty - (-50.0)) * 0.02)
        << "a second at 120 steps landed somewhere else than a second at 60";
}

// A run that a rule changed leaves nothing behind: the joint settings it wrote
// over are the document's again once it stops, or the next run starts from
// them and the change gets saved.
TEST(Simulation, ARuleChangingAJointDoesNotOutliveTheRun)
{
    CanvasScene scene;
    scene.setSimulationEngineName(QStringLiteral("Box2D"));
    scene.setPixelsPerMeter(physics::kReferencePixelsPerMeter);
    scene.setEditorMode(EditorMode::Physics);

    const auto add = [&scene](const QString &name, const QPointF &at) {
        auto *shape = new RectangleItem;
        shape->setRect(QRectF(0, 0, 40, 40));
        shape->setPos(at);
        shape->setName(name);
        scene.addItem(shape);
        return shape;
    };
    ShapeItem *top = add(QStringLiteral("top"), QPointF(0, -100));
    ShapeItem *hanging = add(QStringLiteral("hanging"), QPointF(0, 0));
    scene.notifyShapesChanged();

    scene.selectForPhysics(top, true);
    PhysicsBody *anchor = scene.createBodyFromSelection();
    anchor->props().type = physics::BodyType::Static;
    scene.clearPhysicsSelection();
    scene.selectForPhysics(hanging, true);
    PhysicsBody *weight = scene.createBodyFromSelection();
    scene.clearPhysicsSelection();

    QVariantMap params;
    params.insert(QStringLiteral("enableMotor"), true);
    params.insert(QStringLiteral("motorSpeed"), 10.0);
    Joint *joint = scene.createJoint(QStringLiteral("revolute"), anchor, weight, 1, params);
    ASSERT_TRUE(joint);
    const QVariantMap before = joint->params();

    Rule rule;
    rule.subjectName = Rule::world();
    rule.conditionKey = QStringLiteral("frame");
    rule.compare = Rule::Compare::Multiple;
    rule.conditionValue = 5;
    rule.targetName = joint->name();
    rule.propertyKey = QStringLiteral("motorSpeed");
    rule.op = Rule::Op::Set;
    rule.value = 90.0;
    scene.setRules({ rule });

    SimulationController sim(&scene, nullptr);
    sim.setEngineName(QStringLiteral("Box2D"));
    sim.start();
    for (int i = 0; i < 30; ++i)
        sim.stepFrame();
    EXPECT_NEAR(joint->params().value(QStringLiteral("motorSpeed")).toDouble(), 90.0, 1e-6)
        << "while it runs, the table follows what the rule did";
    sim.stop();

    EXPECT_EQ(joint->params(), before)
        << "once it stops, the joint is the document's again -- otherwise the rule's change"
        << " is what gets saved";
}
