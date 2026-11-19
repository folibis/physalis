// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ruslan Muhlinin. See LICENSE.
#include "CanvasScene.h"
#include "Joint.h"
#include "PhysicsBody.h"
#include "RectangleItem.h"
#include "Rule.h"
#include "ShapeItem.h"
#include "SceneSerializer.h"
#include "PlotPanel.h"
#include "SimulationController.h"
#include "RunCopy.h"

#include <QJsonDocument>
#include <QMouseEvent>
#include <QPixmap>
#include <gtest/gtest.h>
#include "CircleItem.h"
#include "EditorMode.h"
#include <QSignalSpy>
#include "MainWindow.h"
#include "SceneTree.h"
#include "UndoStack.h"
#include <QTreeWidget>
#include <QAction>

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

    ShapeItem *faller = inRun(r.sim, r.faller);
    ASSERT_TRUE(faller);
    const qreal held = faller->pos().y();
    for (int i = 0; i < 10; ++i)
        r.sim.advance(1.0 / 60.0);
    EXPECT_NEAR(faller->pos().y(), held, 1e-9) << "time does not pass while it is paused";

    r.sim.resume();
    EXPECT_TRUE(r.sim.isRunning()) << "and it picks up again";
    r.sim.advance(0.25);
    EXPECT_GT(faller->pos().y(), held) << "it moved once it was resumed";

    r.sim.stop();
    EXPECT_FALSE(r.sim.isActive());
}

// A run moves a copy of the scene. The scene itself is never touched -- not
// while the run goes, not when it stops -- so there is nothing to put back.
TEST(Simulation, ARunNeverTouchesTheScene)
{
    Falling r;
    const QPointF was = r.faller->pos();
    const QByteArray file = QJsonDocument(SceneSerializer::save(&r.scene)).toJson();

    r.sim.start();
    for (int i = 0; i < 90; ++i)
        r.sim.stepFrame();
    ASSERT_TRUE(r.sim.runScene());
    EXPECT_NE(r.sim.runScene(), &r.scene);
    EXPECT_GT(inRun(r.sim, r.faller)->pos().y(), was.y() + 50.0) << "the run's copy did not move";
    EXPECT_EQ(r.faller->pos(), was) << "the run moved the scene";

    r.sim.stop();
    EXPECT_FALSE(r.sim.runScene()) << "the copy outlived the run";
    EXPECT_EQ(QJsonDocument(SceneSerializer::save(&r.scene)).toJson(), file)
        << "the scene is not what it was before the run";
}

// Step advances exactly one step, whatever the speed says, and leaves the run
// paused so the next Step goes on from there.
TEST(Simulation, StepAdvancesOneStep)
{
    Falling oneStep;
    oneStep.sim.start();
    oneStep.sim.stepFrame();
    const qreal afterOne = inRun(oneStep.sim, oneStep.faller)->pos().y();
    oneStep.sim.stop();

    Falling twoSteps;
    twoSteps.sim.start();
    twoSteps.sim.stepFrame();
    twoSteps.sim.stepFrame();
    const qreal afterTwo = inRun(twoSteps.sim, twoSteps.faller)->pos().y();
    EXPECT_GT(afterTwo, afterOne) << "the second step moved it further";
    EXPECT_FALSE(twoSteps.sim.isRunning()) << "stepping leaves it paused, not running";
    twoSteps.sim.stop();

    // At any speed, a step is a step.
    Falling fast;
    fast.sim.setSpeed(4.0);
    fast.sim.start();
    fast.sim.stepFrame();
    EXPECT_NEAR(inRun(fast.sim, fast.faller)->pos().y(), afterOne, 1e-9)
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
    const qreal atOne = inRun(once.sim, once.faller)->pos().y();
    once.sim.stop();

    Falling twice;
    twice.sim.setSpeed(2.0);
    twice.sim.start();
    for (int i = 0; i < 15; ++i)
        twice.sim.advance(tick);
    const qreal atTwo = inRun(twice.sim, twice.faller)->pos().y();
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
    const qreal afterAGap = inRun(r.sim, r.faller)->pos().y();
    r.sim.stop();

    Falling full;
    full.sim.start();
    for (int i = 0; i < 600; ++i)
        full.sim.stepFrame();
    const qreal afterTenSeconds = inRun(full.sim, full.faller)->pos().y();
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
    const qreal afterASecond = inRun(r.sim, r.faller)->pos().y();
    r.sim.stop();

    // A second of falling is a second of falling, whichever rate got there.
    Falling sixty;
    sixty.sim.start();
    for (int i = 0; i < 60; ++i)
        sixty.sim.stepFrame();
    const qreal atSixty = inRun(sixty.sim, sixty.faller)->pos().y();
    sixty.sim.stop();

    EXPECT_NEAR(afterASecond, atSixty, qAbs(atSixty - (-50.0)) * 0.02)
        << "a second at 120 steps landed somewhere else than a second at 60";
}

// A rule changes the run's joint, never the scene's: otherwise the next run
// starts from the change and the change gets saved.
TEST(Simulation, ARuleChangingAJointLeavesTheScenesJointAlone)
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
    rule.conditions[0].subjectName = Rule::world();
    rule.conditions[0].conditionKey = QStringLiteral("frame");
    rule.conditions[0].compare = Rule::Compare::Multiple;
    rule.conditions[0].conditionValue = 5;
    rule.actions[0].targetName = joint->name();
    rule.actions[0].propertyKey = QStringLiteral("motorSpeed");
    rule.actions[0].op = Rule::Op::Set;
    rule.actions[0].value = 90.0;
    scene.setRules({ rule });

    SimulationController sim(&scene, nullptr);
    sim.setEngineName(QStringLiteral("Box2D"));
    sim.start();
    for (int i = 0; i < 30; ++i)
        sim.stepFrame();
    EXPECT_NEAR(inRun(sim, joint)->params().value(QStringLiteral("motorSpeed")).toDouble(), 90.0,
                1e-6)
        << "the rule did not change the run's joint";
    EXPECT_EQ(joint->params(), before) << "the rule changed the scene's joint";
    sim.stop();
    EXPECT_EQ(joint->params(), before);
}

namespace {

void oneFallingBody(CanvasScene *scene)
{
    scene->setSimulationEngineName(QStringLiteral("Box2D"));
    scene->setPixelsPerMeter(1000.0);
    scene->world().params[QStringLiteral("gravityY")] = 9.81;
    scene->setEditorMode(EditorMode::Physics);

    auto *ball = new CircleItem;
    ball->setRect(QRectF(0, 0, 40, 40));
    ball->setName(QStringLiteral("ball"));
    scene->addItem(ball);
    scene->notifyShapesChanged();
    scene->selectForPhysics(ball, true);
    PhysicsBody *body = scene->createBodyFromSelection();
    body->props().type = physics::BodyType::Dynamic;
    body->setName(QStringLiteral("ballBody"));
    scene->clearPhysicsSelection();
}

} // namespace

// Stepping one frame at a time has to say so, the same as a step taken by the
// clock: the property table's live rows and the log read the run when they are
// told there is something new. Stepping moved the shapes and told nobody, so
// every number beside them sat at what it had been when the run started.
TEST(Stepping, OneStepTellsWhateverIsReadingTheRun)
{
    CanvasScene scene;
    oneFallingBody(&scene);

    SimulationController sim(&scene, nullptr);
    sim.setEngineName(QStringLiteral("Box2D"));
    QSignalSpy stepped(&sim, &SimulationController::stepped);

    sim.stepFrame();
    EXPECT_EQ(stepped.count(), 1) << "the first step said nothing";

    const double afterOne =
        sim.readValue(QStringLiteral("ballBody"), QStringLiteral("velocityY")).toDouble();

    for (int i = 0; i < 4; ++i)
        sim.stepFrame();
    EXPECT_EQ(stepped.count(), 5) << "stepping four more times said nothing four more times";

    // And the numbers really did move, so what the panels are being told about
    // is a change and not a false alarm.
    const double afterFive =
        sim.readValue(QStringLiteral("ballBody"), QStringLiteral("velocityY")).toDouble();
    EXPECT_GT(qAbs(afterFive), qAbs(afterOne))
        << "five steps of falling left the velocity where one step had it";
}

// A run driven by the clock already did this; it is what the single step was
// measured against.
TEST(Stepping, AClockDrivenStepSaysSoToo)
{
    CanvasScene scene;
    oneFallingBody(&scene);

    SimulationController sim(&scene, nullptr);
    sim.setEngineName(QStringLiteral("Box2D"));
    QSignalSpy stepped(&sim, &SimulationController::stepped);

    sim.start();
    sim.advance(1.0 / 60.0);
    EXPECT_GE(stepped.count(), 1) << "a running step said nothing";
}

namespace {

// Two dynamic bodies, which is what a joint needs something to hold.
void twoBodies(CanvasScene *scene)
{
    scene->setSimulationEngineName(QStringLiteral("Box2D"));
    scene->setEditorMode(EditorMode::Physics);
    for (int i = 0; i < 2; ++i) {
        auto *shape = new RectangleItem;
        shape->setRect(QRectF(0, 0, 40, 40));
        shape->setPos(i * 120, 0);
        shape->setName(QStringLiteral("box%1").arg(i + 1));
        scene->addItem(shape);
    }
    scene->notifyShapesChanged();
    for (ShapeItem *shape : scene->shapes()) {
        scene->selectForPhysics(shape, true);
        PhysicsBody *body = scene->createBodyFromSelection();
        body->props().type = physics::BodyType::Dynamic;
        scene->clearPhysicsSelection();
    }
}

void selectEverything(CanvasScene *scene)
{
    scene->clearPhysicsSelection();
    for (ShapeItem *shape : scene->shapes())
        scene->selectForPhysics(shape, true);
}

} // namespace

// Stopping a run has to hand the toolbar back. Add Joint and Delete Joint are
// set by the joint handler and by nothing else, and stopping called only the
// physics one -- so they stayed greyed out, and selecting bodies did not bring
// them round. Switching the editor mode and back did, which is what made it
// look as though it only happened sometimes.
TEST(JointActions, StoppingARunHandsTheJointButtonsBack)
{
    MainWindow window;
    auto *scene = window.findChild<CanvasScene *>();
    ASSERT_NE(scene, nullptr);
    auto *add = window.findChild<QAction *>(QStringLiteral("actionAddJoint"));
    auto *simulate = window.findChild<QAction *>(QStringLiteral("actionSimulate"));
    auto *stop = window.findChild<QAction *>(QStringLiteral("actionStop"));
    ASSERT_NE(add, nullptr);
    ASSERT_NE(simulate, nullptr);
    ASSERT_NE(stop, nullptr);

    twoBodies(scene);
    selectEverything(scene);
    ASSERT_TRUE(add->isEnabled()) << "Add Joint is off before a run has even started";

    simulate->trigger();
    EXPECT_FALSE(add->isEnabled()) << "Add Joint is offered while the simulation runs";

    stop->trigger();
    EXPECT_TRUE(add->isEnabled())
        << "the run stopped and Add Joint is still greyed out";

    // And it stays back: picking bodies again is the first thing anyone does.
    selectEverything(scene);
    EXPECT_TRUE(add->isEnabled())
        << "selecting bodies after a run left Add Joint greyed out";
}

// A watch marked for plotting is recorded every step, and one that is not is
// not recorded at all. The samples belong to the run: a second Play starts a
// fresh trace rather than continuing the last one.
TEST(Simulation, APlottedWatchIsRecordedEveryStep)
{
    CanvasScene scene;
    scene.setSimulationEngineName(QStringLiteral("Box2D"));
    scene.setEditorMode(EditorMode::Physics);

    auto *shape = new RectangleItem;
    shape->setRect(QRectF(0, 0, 40, 40));
    shape->setName(QStringLiteral("faller"));
    scene.addItem(shape);
    scene.notifyShapesChanged();
    scene.selectForPhysics(shape, true);
    PhysicsBody *body = scene.createBodyFromSelection();
    body->props().type = physics::BodyType::Dynamic;
    scene.clearPhysicsSelection();

    scene.addWatch({body->name(), QStringLiteral("positionY"), QStringLiteral("Y"), true});
    scene.addWatch({body->name(), QStringLiteral("mass"), QStringLiteral("Mass"), false});

    SimulationController sim(&scene, nullptr);
    sim.setEngineName(QStringLiteral("Box2D"));
    sim.start();
    for (int i = 0; i < 90; ++i)
        sim.stepFrame();

    const QString plotted = SimulationController::plotKey(body->name(),
                                                          QStringLiteral("positionY"));
    const QString ignored = SimulationController::plotKey(body->name(), QStringLiteral("mass"));
    ASSERT_TRUE(sim.plots().contains(plotted)) << "the plotted watch recorded nothing";
    EXPECT_FALSE(sim.plots().contains(ignored))
        << "a watch that is only logged was recorded as well";

    const auto &points = sim.plots().value(plotted).points;
    EXPECT_EQ(points.size(), 90) << "one sample a step, not one a frame";
    EXPECT_GT(points.constLast().y(), points.constFirst().y())
        << "the trace did not follow the body down";
    // The clock is the run's, and it starts one step in rather than at zero.
    EXPECT_GT(points.constFirst().x(), 0.0);
    EXPECT_LT(points.constFirst().x(), points.constLast().x());

    sim.stop();
    sim.start();
    EXPECT_TRUE(sim.plots().isEmpty()) << "a new run kept the last one's trace";
    sim.stop();
}

// A trace says what a value did, and until the crosshair it could say what it
// was at no moment but the last. The pointer picks a moment; every band reads
// it, since the clock is the one axis they all share.
TEST(Simulation, ThePlotReadsAValueOffATraceUnderThePointer)
{
    CanvasScene scene;
    scene.setSimulationEngineName(QStringLiteral("Box2D"));
    scene.setEditorMode(EditorMode::Physics);

    auto *shape = new RectangleItem;
    shape->setRect(QRectF(0, 0, 40, 40));
    shape->setName(QStringLiteral("faller"));
    scene.addItem(shape);
    scene.notifyShapesChanged();
    scene.selectForPhysics(shape, true);
    PhysicsBody *body = scene.createBodyFromSelection();
    body->props().type = physics::BodyType::Dynamic;
    scene.clearPhysicsSelection();

    scene.addWatch({ body->name(), QStringLiteral("positionY"), QStringLiteral("Y"), true });

    SimulationController sim(&scene, nullptr);
    sim.setEngineName(QStringLiteral("Box2D"));
    sim.start();
    for (int i = 0; i < 180; ++i)
        sim.stepFrame();
    ASSERT_FALSE(sim.plots().isEmpty()) << "the run recorded no trace";

    PlotPanel panel;
    panel.attach(&scene, &sim);
    panel.resize(620, 340);

    const auto shot = [&panel] {
        QPixmap pixmap(panel.size());
        panel.render(&pixmap);
        return pixmap.toImage();
    };
    // The column the two pictures differ down the most, and by how much: a
    // line drawn through every band changes a whole column of pixels, which
    // no caption or dot does.
    const auto busiestChange = [](const QImage &before, const QImage &after) {
        int where = -1;
        int most = 0;
        for (int x = 0; x < before.width(); ++x) {
            int changed = 0;
            for (int y = 0; y < before.height(); ++y) {
                if (before.pixelColor(x, y) != after.pixelColor(x, y))
                    ++changed;
            }
            if (changed > most) {
                most = changed;
                where = x;
            }
        }
        return QPair<int, int>(where, most);
    };

    const QImage plain = shot();

    const QPointF at(330, 120);
    QMouseEvent move(QEvent::MouseMove, at, at, Qt::NoButton, Qt::NoButton, Qt::NoModifier);
    QCoreApplication::sendEvent(&panel, &move);
    const QImage crossed = shot();

    EXPECT_NE(plain, crossed) << "the pointer over the plot changed nothing";
    const QPair<int, int> line = busiestChange(plain, crossed);
    // Within a sample of the pointer: the crosshair sits on the reading it is
    // quoting, not on the pixel the mouse happens to be over.
    EXPECT_NEAR(line.first, int(at.x()), 6)
        << "the line went down column " << line.first << ", not under the pointer";
    EXPECT_GT(line.second, 40) << "whatever was drawn there was not a line through the bands";

    // Leaving puts it back exactly: the crosshair is the pointer's, not the
    // run's, so nothing of it may be left behind in the picture.
    QEvent gone(QEvent::Leave);
    QCoreApplication::sendEvent(&panel, &gone);
    EXPECT_EQ(shot(), plain) << "the crosshair outlived the pointer";

    sim.stop();
}

// A body marked to draw leaves its centre of mass behind it as a line: one
// point a step, there for the whole run and gone with it. Built as a double
// pendulum, which is what the line is for -- nothing else shows where the
// lower arm has been.
TEST(Simulation, ABodyMarkedToDrawLeavesATrailForTheRun)
{
    CanvasScene scene;
    scene.setSimulationEngineName(QStringLiteral("Box2D"));
    scene.setPixelsPerMeter(physics::kReferencePixelsPerMeter);
    scene.setEditorMode(EditorMode::Physics);

    const auto bar = [&scene](const QString &name, const QRectF &rect) {
        auto *shape = new RectangleItem;
        shape->setRect(QRectF(0, 0, rect.width(), rect.height()));
        shape->setPos(rect.topLeft());
        shape->setName(name);
        scene.addItem(shape);
        scene.notifyShapesChanged();
        scene.clearPhysicsSelection();
        scene.selectForPhysics(shape, true);
        PhysicsBody *body = scene.createBodyFromSelection();
        scene.clearPhysicsSelection();
        return body;
    };
    // Laid out sideways from the pivot, so it has somewhere to fall.
    const QPointF pivot(400, 200);
    PhysicsBody *post = bar(QStringLiteral("post"), QRectF(390, 190, 20, 20));
    post->props().type = physics::BodyType::Static;
    PhysicsBody *upper = bar(QStringLiteral("upper"), QRectF(400, 195, 120, 10));
    PhysicsBody *lower = bar(QStringLiteral("lower"), QRectF(520, 195, 120, 10));

    Joint *shoulder = scene.createJoint(QStringLiteral("revolute"), post, upper, 1, {});
    Joint *elbow = scene.createJoint(QStringLiteral("revolute"), upper, lower, 1, {});
    ASSERT_TRUE(shoulder && elbow);
    shoulder->setAnchorScenePos(Joint::End::A, pivot);
    elbow->setAnchorScenePos(Joint::End::A, QPointF(520, 200));

    lower->setDrawsTrail(true);
    const QPointF startedAt = lower->centerOfMassScenePos();

    // The mark is the scene's, so it comes back with the file.
    QString error;
    CanvasScene reopened;
    reopened.setSimulationEngineName(QStringLiteral("Box2D"));
    ASSERT_TRUE(SceneSerializer::load(&reopened, SceneSerializer::save(&scene), &error))
        << error.toStdString();
    int marked = 0;
    for (PhysicsBody *body : reopened.bodies())
        marked += body->drawsTrail() ? 1 : 0;
    EXPECT_EQ(marked, 1) << "the file lost which body draws, or gave it to another";

    SimulationController sim(&scene, nullptr);
    sim.setEngineName(QStringLiteral("Box2D"));
    sim.start();
    CanvasScene *run = sim.runScene();
    PhysicsBody *runLower = inRun(sim, lower);
    ASSERT_TRUE(run && runLower);
    EXPECT_TRUE(run->trail(runLower).isEmpty()) << "a trail before the first step";
    const int steps = 240;
    for (int i = 0; i < steps; ++i)
        sim.stepFrame();

    const QVector<QPointF> drawn = run->trail(runLower);
    EXPECT_GT(drawn.size(), steps / 2) << "fewer points than a point a step would leave";
    EXPECT_TRUE(run->trail(inRun(sim, upper)).isEmpty()) << "a body nobody marked drew a line";
    EXPECT_TRUE(scene.trail(lower).isEmpty()) << "the trail was drawn into the scene";
    ASSERT_FALSE(drawn.isEmpty());

    // It starts where the centre of mass stood and ends where it stands now:
    // the line is carried through the body's own transform, so both ends are
    // a check that the transform is the right one.
    EXPECT_LT(QLineF(drawn.first(), startedAt).length(), 3.0)
        << "the line starts somewhere the body never was";
    EXPECT_LT(QLineF(drawn.last(), runLower->centerOfMassScenePos()).length(), 0.5)
        << "the line's end is not where the body is drawn";

    // And it is a pendulum's: no point further from the pivot than the two
    // arms reach, and it has actually gone somewhere.
    qreal furthest = 0.0;
    qreal lowest = startedAt.y();
    for (const QPointF &point : drawn) {
        furthest = qMax(furthest, QLineF(pivot, point).length());
        lowest = qMax(lowest, point.y());
    }
    EXPECT_LT(furthest, 120.0 + 60.0 + 2.0) << "the line left the reach of the arms";
    EXPECT_GT(lowest, startedAt.y() + 100.0) << "the arm fell and the line did not follow";

    sim.stop();

    // Rules work the pen like any flag: toggled off at frame 60 and set on at
    // 120, the line is two strokes with a gap between, not one joined across
    // it; wiped at 150, only what was drawn since is left.
    const auto atFrame = [&](int frame) {
        Rule rule;
        rule.conditions[0].subjectName = Rule::world();
        rule.conditions[0].conditionKey = QStringLiteral("frame");
        rule.conditions[0].compare = Rule::Compare::Equal;
        rule.conditions[0].conditionValue = frame;
        rule.actions[0].targetName = lower->name();
        return rule;
    };
    Rule lift = atFrame(60);
    lift.actions[0].propertyKey = Rule::drawsTrailKey();
    lift.actions[0].op = Rule::Op::Toggle;
    Rule lower_ = atFrame(120);
    lower_.actions[0].propertyKey = Rule::drawsTrailKey();
    lower_.actions[0].op = Rule::Op::Set;
    lower_.actions[0].value = true;
    Rule wipe = atFrame(150);
    wipe.actions[0].actionId = Rule::clearTrailAction();
    scene.setRules({ lift, lower_, wipe });
    SimulationController pen(&scene, nullptr);
    pen.setEngineName(QStringLiteral("Box2D"));
    pen.start();
    for (int i = 0; i < 140; ++i)
        pen.stepFrame();
    const QVector<QVector<QPointF>> strokes = pen.runScene()->trailStrokes(inRun(pen, lower));
    ASSERT_EQ(strokes.size(), 2) << "lifting and lowering the pen did not leave a gap";
    EXPECT_GT(QLineF(strokes[0].last(), strokes[1].first()).length(), 5.0)
        << "the gap is where the body went while the pen was up";
    EXPECT_TRUE(lower->drawsTrail()) << "a rule lifted the scene's own pen";
    for (int i = 0; i < 40; ++i)
        pen.stepFrame();
    const QVector<QVector<QPointF>> wiped = pen.runScene()->trailStrokes(inRun(pen, lower));
    ASSERT_EQ(wiped.size(), 1) << "clearing left the old line behind";
    EXPECT_LE(wiped[0].size(), 31) << "more is left than was drawn since the wipe";
    pen.stop();

    // With a fade set on the body, only the last half second of its line is
    // kept -- and it is the body's, so it reaches the run's copy through the
    // file the copy is made from.
    scene.setRules({});
    lower->setTrailFadeMs(500);
    SimulationController fading(&scene, nullptr);
    fading.setEngineName(QStringLiteral("Box2D"));
    fading.start();
    for (int i = 0; i < 240; ++i)
        fading.stepFrame();
    const QVector<QPointF> tail = fading.runScene()->trail(inRun(fading, lower));
    EXPECT_GT(tail.size(), 10) << "the tail faded away while the body was still moving";
    EXPECT_LE(tail.size(), 31) << "points older than the fade were kept";
    fading.stop();
}

// What is plotted is part of the scene, not of the session.
TEST(Simulation, WhatIsPlottedSurvivesTheFile)
{
    CanvasScene scene;
    scene.setSimulationEngineName(QStringLiteral("Box2D"));
    scene.addWatch({QStringLiteral("@world"), QStringLiteral("time"),
                    QStringLiteral("Elapsed"), true});
    scene.addWatch({QStringLiteral("@world"), QStringLiteral("frame"),
                    QStringLiteral("Frame"), false});

    CanvasScene reopened;
    QString error;
    ASSERT_TRUE(SceneSerializer::load(&reopened, SceneSerializer::save(&scene), &error))
        << error.toStdString();

    ASSERT_EQ(reopened.watches().size(), 2);
    EXPECT_TRUE(reopened.isPlotted(QStringLiteral("@world"), QStringLiteral("time")));
    EXPECT_FALSE(reopened.isPlotted(QStringLiteral("@world"), QStringLiteral("frame")));
}

namespace {

ShapeItem *shapeNamed(const CanvasScene &scene, const QString &name)
{
    for (ShapeItem *shape : scene.shapes()) {
        if (shape->name() == name)
            return shape;
    }
    return nullptr;
}

} // namespace

// A run is read, not edited, and the two halves of that are easy to get wrong
// in opposite directions.
//
// The undo stack records nothing while one is going. Every body is somewhere
// it will not be once Stop has put it back, so a state taken mid-run holds a
// frozen frame of the run -- undoing after Stop used to teleport the whole
// scene into it. The edit is still real, though: adding a row to the log is
// meant to happen mid-run, and the scene has to come out of one dirty.
//
// The Objects tree, on the other hand, stays live: picking a row is how an
// object's live values are read while the scene moves, and the canvas cannot
// do it because a press there pans the field.
TEST(Simulation, ARunIsReadableAndRecordsNoUndoState)
{
    Falling r;
    // A second body, so there is something to switch the panels to.
    ShapeItem *other = new RectangleItem;
    other->setRect(QRectF(0, 0, 40, 40));
    other->setPos(300, -50);
    other->setName(QStringLiteral("second"));
    r.scene.addItem(other);
    r.scene.notifyShapesChanged();
    r.scene.selectForPhysics(other, true);
    PhysicsBody *secondBody = r.scene.createBodyFromSelection();
    secondBody->props().type = physics::BodyType::Dynamic;
    secondBody->setName(QStringLiteral("secondBody"));
    r.scene.clearPhysicsSelection();

    SceneTree tree(&r.scene);
    UndoStack undo(&r.scene);

    // One recorded edit to undo back to later.
    r.faller->setPos(100, -200);
    undo.push(QStringLiteral("Move the faller"));
    ASSERT_TRUE(undo.canUndo());
    // Saved here, so that "the scene is dirty again" below says something.
    undo.markClean();
    ASSERT_TRUE(undo.isClean());

    r.scene.selectForPhysics(r.faller, true);
    r.sim.start();
    for (int i = 0; i < 90; ++i)
        r.sim.stepFrame();
    const qreal midRun = inRun(r.sim, r.faller)->pos().y();
    ASSERT_GT(midRun, -150.0) << "it really moved during the run";

    // Picking a row mid-run points the panels at that object instead.
    auto *rows = tree.findChild<QTreeWidget *>();
    ASSERT_TRUE(rows);
    ASSERT_TRUE(rows->isEnabled()) << "the tree was taken away for the length of the run";
    QTreeWidgetItem *wanted = nullptr;
    for (QTreeWidgetItemIterator it(rows); *it; ++it) {
        if ((*it)->text(0) == QStringLiteral("secondBody"))
            wanted = *it;
    }
    ASSERT_TRUE(wanted) << "the second body has no row to pick";
    rows->setCurrentItem(wanted);
    ASSERT_FALSE(r.scene.physicsSelection().isEmpty());
    EXPECT_EQ(r.scene.physicsSelection().first()->body(), secondBody)
        << "picking a row mid-run did not move the selection";

    // The kind of edit a run is for: a value put on the log.
    r.scene.addWatch({secondBody->name(), QStringLiteral("positionY"),
                      QStringLiteral("Y"), false});
    undo.push(QStringLiteral("Add Y to the log"));
    EXPECT_FALSE(undo.isClean()) << "a mid-run edit left the scene looking saved";

    r.sim.stop();
    undo.undo();

    ShapeItem *back = shapeNamed(r.scene, QStringLiteral("faller"));
    ASSERT_TRUE(back);
    EXPECT_NEAR(back->pos().y(), -50.0, 1e-9)
        << "undo landed on a state recorded in the middle of the run";
}
