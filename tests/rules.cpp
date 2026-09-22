// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ruslan Muhlinin. See LICENSE.
#include "CanvasScene.h"
#include "CircleItem.h"
#include "EngineRegistry.h"
#include "IPhysicsEngine.h"
#include "PhysicsBody.h"
#include "RectangleItem.h"
#include "Rule.h"
#include "SceneSerializer.h"
#include "ShapeItem.h"
#include "SimulationController.h"

#include <QJsonObject>
#include <QSet>
#include <cmath>
#include <cstdio>
#include <gtest/gtest.h>

// Rules: the part of the app with no physics in it and the most ways to be
// silently wrong. Two things are checked here, for both engines -- that every
// action either engine publishes actually does something when a rule performs
// it, and that the conditions a rule can be written with fire when they should
// and not when they should not.

namespace {

struct Bench {
    CanvasScene scene;
    SimulationController sim { &scene, nullptr };
    ShapeItem *groundShape = nullptr;
    ShapeItem *boxShape = nullptr;
    PhysicsBody *ground = nullptr;
    PhysicsBody *box = nullptr;

    explicit Bench(const QString &engineName, qreal gravity = 9.81)
    {
        scene.setSimulationEngineName(engineName);
        scene.setPixelsPerMeter(physics::kReferencePixelsPerMeter);
        scene.world().params["gravityY"] = gravity;
        scene.setEditorMode(EditorMode::Physics);
        sim.setEngineName(engineName);

        groundShape = add(QStringLiteral("ground"), QRectF(0, 0, 800, 40), QPointF(-400, 300));
        boxShape = add(QStringLiteral("box"), QRectF(0, 0, 40, 40), QPointF(-20, 0));
        scene.notifyShapesChanged();
        ground = bodyOf(groundShape, physics::BodyType::Static, QStringLiteral("groundBody"));
        box = bodyOf(boxShape, physics::BodyType::Dynamic, QStringLiteral("boxBody"));
    }

    ShapeItem *add(const QString &name, const QRectF &rect, const QPointF &at)
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

QVariant somethingElse(const physics::JointParam &param)
{
    if (param.type == physics::ParamType::Bool)
        return !param.defaultValue.toBool();
    if (param.type == physics::ParamType::Choice)
        return param.defaultValue.toInt() == 0 ? 1 : 0;
    const qreal was = param.defaultValue.toDouble();
    qreal wanted = was != 0.0 ? was * 2.0 : 1.0;
    wanted = qBound(param.minValue, wanted, param.maxValue);
    if (param.decimals == 0)
        wanted = std::round(wanted);
    return qFuzzyCompare(wanted, was) ? QVariant() : QVariant(wanted);
}

const char *kEngines[] { "Box2D", "Chipmunk2D" };

// An action that legitimately does nothing to a scene that has not changed
// under it. Each one is named with why, so an action that does nothing by
// accident cannot hide among them.
const QHash<QString, const char *> kInertOnTheirOwn {
    // Recomputes a body's mass from its shapes. Nothing has altered a shape,
    // so the mass it arrives at is the mass it already had.
    { QStringLiteral("resetMass"), "there is nothing to recompute in a scene nobody changed" },
};

// The id of the first event of a kind the engine publishes for shapes, so a
// test watches what the engine really raises rather than a name somebody
// remembered. Watching "beginContact" when the engine says "contactBegin"
// gives a rule that never fires and a test that proves nothing.
QString shapeEventNamed(const QString &engineName, const QString &wanted)
{
    auto engine = physics::EngineRegistry::create(engineName);
    if (!engine)
        return {};
    for (const physics::EventType &event : engine->shapeEvents()) {
        if (event.id == wanted)
            return event.id;
    }
    return {};
}

} // namespace

// Every action a rule can perform does something. Each one is run twice --
// once with the rule, once without -- and the two runs have to differ. An
// action published but never implemented, or implemented and never reached,
// leaves the two runs identical, which is what a user sees as "the rule does
// nothing".
void everyActionDoesSomething(const QString &engineName)
{
    auto engine = physics::EngineRegistry::create(engineName);
    ASSERT_TRUE(engine) << engineName.toStdString() << " is not installed";

    // In free space with no gravity and nothing to land on: on the ground, a
    // small push is rubbed out by friction before the run ends, and every
    // action looks inert whether it is or not.
    QPointF withoutRule;
    bool boxSurvivedWithoutRule = false;
    {
        Bench bench(engineName, 0.0);
        bench.run(120);
        withoutRule = bench.boxShape->pos();
        boxSurvivedWithoutRule = bench.boxShape->isVisible();
        bench.sim.stop();
    }
    ASSERT_TRUE(boxSurvivedWithoutRule);

    int checked = 0;
    for (const physics::ActionType &action : engine->bodyActions()) {
        Bench bench(engineName, 0.0);

        // Every frame, not once at the start: a force lasts for the step it is
        // applied in, so a single one is a nudge of a fraction of a pixel and
        // the body is asleep again before it shows. Everything else -- an
        // impulse, a teleport, a clone -- shows up under the same rule.
        Rule rule;
        rule.subjectName = Rule::world();
        rule.conditionKey = QStringLiteral("frame");
        rule.compare = Rule::Compare::Multiple;
        rule.conditionValue = 1;
        rule.targetName = bench.box->name();
        rule.actionId = action.id;
        for (const physics::JointParam &param : action.params) {
            const QVariant value = somethingElse(param);
            if (value.isValid())
                rule.actionParams.insert(param.key, value);
        }
        // A force is quoted in scene units and divided by the scene's scale on
        // the way in, and it acts for the one step it is applied in -- so the
        // number that shifts a two-gram box is nothing like the number that
        // would in newtons. This is the trap the manual warns about, not a
        // fault in the action, so the test uses a figure that does something
        // at this scale rather than one that reads well.
        if (action.id == QLatin1String("pushForceAt")) {
            rule.actionParams.insert(QStringLiteral("impulseX"), 100000.0);
            rule.actionParams.insert(QStringLiteral("impulseY"), 0.0);
        }
        bench.scene.setRules({ rule });

        bench.run(120);
        const QPointF withRule = bench.boxShape->pos();
        const bool gone = !bench.boxShape->isVisible();
        const int bodies = bench.scene.bodies().size();
        bench.sim.stop();
        ++checked;

        const std::string what = (engineName + QLatin1String(": ") + action.id).toStdString();
        if (action.removesBody) {
            EXPECT_TRUE(gone) << what << " says it removes the body, and the body is still there";
            continue;
        }
        // A clone leaves the original alone and adds another body, so it is
        // the body count that moved rather than the box.
        const bool movedDifferently = (withRule - withoutRule).manhattanLength() > 0.5;
        const bool madeSomething = bodies > 2;
        if (kInertOnTheirOwn.contains(action.id)) {
            EXPECT_FALSE(movedDifferently || madeSomething || gone)
                << what << " is listed as doing nothing on its own, and it did something --"
                << " so the list is out of date rather than the action";
            continue;
        }
        EXPECT_TRUE(movedDifferently || madeSomething || gone)
            << what << " is offered as something a rule can do, and doing it changed nothing:"
            << " the box ended at the same place as it does with no rule at all";
    }
    EXPECT_GT(checked, 0) << "the engine published no body actions";
}

TEST(Rules, Box2DActionsAllDoSomething)
{
    everyActionDoesSomething(QStringLiteral("Box2D"));
}

TEST(Rules, ChipmunkActionsAllDoSomething)
{
    everyActionDoesSomething(QStringLiteral("Chipmunk2D"));
}

// The application's own actions on a body, which no engine knows about.
TEST(Rules, InitStatePutsABodyBackWhereItStarted)
{
    for (const char *engineName : kEngines) {
        Bench bench(QString::fromLatin1(engineName));
        const QPointF started = bench.boxShape->pos();

        // Fall for a while, then a rule puts it back on the sixtieth frame.
        Rule rule;
        rule.subjectName = Rule::world();
        rule.conditionKey = QStringLiteral("frame");
        rule.compare = Rule::Compare::Multiple;
        rule.conditionValue = 60;
        rule.targetName = bench.box->name();
        rule.actionId = Rule::initStateAction();
        bench.scene.setRules({ rule });

        bench.run(59);
        EXPECT_GT(bench.boxShape->pos().y(), started.y() + 10.0)
            << engineName << ": it should have fallen before the rule fires";
        bench.run(1);
        EXPECT_NEAR(bench.boxShape->pos().y(), started.y(), 1.0)
            << engineName << ": Init State should have put it back where it started";
        bench.sim.stop();
    }
}

TEST(Rules, CloneMakesAnotherBodyAndStopEndsTheRun)
{
    for (const char *engineName : kEngines) {
        {
            Bench bench(QString::fromLatin1(engineName));
            Rule clone;
            clone.subjectName = Rule::world();
            clone.eventId = Rule::runStartedEvent();
            clone.targetName = bench.box->name();
            clone.actionId = Rule::cloneAction();
            clone.actionParams.insert(Rule::cloneXParam(), 200.0);
            clone.actionParams.insert(Rule::cloneYParam(), -100.0);
            bench.scene.setRules({ clone });

            bench.run(5);
            EXPECT_EQ(bench.scene.bodies().size(), 3)
                << engineName << ": cloning should have added a body";
            bench.sim.stop();
            EXPECT_EQ(bench.scene.bodies().size(), 2)
                << engineName << ": and the clone should be gone when the run ends";
        }
        {
            Bench bench(QString::fromLatin1(engineName));
            Rule stop;
            stop.subjectName = Rule::world();
            stop.conditionKey = QStringLiteral("frame");
            stop.compare = Rule::Compare::Multiple;
            stop.conditionValue = 30;
            stop.targetName = Rule::world();
            stop.actionId = Rule::stopRunAction();
            bench.scene.setRules({ stop });

            // Stepped one at a time and stopped at: stepFrame() on a run that
            // has ended starts a new one, which would hide the very thing
            // being tested.
            bench.sim.start();
            int stepsTaken = 0;
            for (int i = 0; i < 45 && bench.sim.isActive(); ++i) {
                bench.sim.stepFrame();
                ++stepsTaken;
            }
            EXPECT_FALSE(bench.sim.isActive())
                << engineName << ": a rule asked the run to stop and it is still going";
            EXPECT_LE(stepsTaken, 31)
                << engineName << ": it should have stopped on the thirtieth frame, and ran "
                << stepsTaken;
        }
    }
}

// Conditions fire when they should. A rule that never fires and a rule that
// fires every step look the same from the outside until something moves.
TEST(Rules, ConditionsFireWhenTheyShould)
{
    for (const char *engineName : kEngines) {
        // "Greater than" on a property the run answers for: the box falls, and
        // the rule fires once it is past the line -- not before.
        Bench bench(QString::fromLatin1(engineName));
        Rule rule;
        rule.subjectName = bench.box->name();
        rule.conditionKey = QStringLiteral("positionY");
        rule.compare = Rule::Compare::Greater;
        rule.conditionValue = 100.0;
        rule.targetName = bench.box->name();
        rule.actionId = Rule::initStateAction();
        bench.scene.setRules({ rule });

        bench.run(20);
        EXPECT_LT(bench.boxShape->pos().y(), 100.0)
            << engineName << ": twenty steps in it has not passed the line yet";
        // Falling from 0, it passes y = 100 after about 0.64 s; by two seconds
        // the rule must have caught it and put it back at least once.
        bench.run(100);
        EXPECT_LT(bench.boxShape->pos().y(), 140.0)
            << engineName << ": the rule should keep resetting it near the line, and it is at "
            << bench.boxShape->pos().y();
        bench.sim.stop();
    }
}

// "Every nth frame" is the one condition with arithmetic in it.
TEST(Rules, EveryNthFrameFiresEveryNthFrame)
{
    Bench bench(QStringLiteral("Box2D"), 0.0);
    Rule rule;
    rule.subjectName = Rule::world();
    rule.conditionKey = QStringLiteral("frame");
    rule.compare = Rule::Compare::Multiple;
    rule.conditionValue = 10;
    rule.targetName = bench.box->name();
    rule.actionId = Rule::cloneAction();
    rule.actionParams.insert(Rule::cloneXParam(), 300.0);
    rule.actionParams.insert(Rule::cloneYParam(), 0.0);
    bench.scene.setRules({ rule });

    bench.run(9);
    EXPECT_EQ(bench.scene.bodies().size(), 2) << "nothing yet by the ninth frame";
    bench.run(1);
    EXPECT_EQ(bench.scene.bodies().size(), 3) << "the tenth frame is a multiple of ten";
    bench.run(10);
    EXPECT_EQ(bench.scene.bodies().size(), 4) << "and so is the twentieth";
    bench.sim.stop();
}

// A disabled rule does nothing, and a "once" rule does its thing once.
TEST(Rules, DisabledRulesAreSkippedAndOnceMeansOnce)
{
    {
        Bench bench(QStringLiteral("Box2D"), 0.0);
        Rule rule;
        rule.subjectName = Rule::world();
        rule.conditionKey = QStringLiteral("frame");
        rule.compare = Rule::Compare::Multiple;
        rule.conditionValue = 5;
        rule.targetName = bench.box->name();
        rule.actionId = Rule::cloneAction();
        rule.actionParams.insert(Rule::cloneXParam(), 300.0);
        rule.actionParams.insert(Rule::cloneYParam(), 0.0);
        rule.enabled = false;
        bench.scene.setRules({ rule });

        bench.run(30);
        EXPECT_EQ(bench.scene.bodies().size(), 2) << "a disabled rule did something";
        bench.sim.stop();
    }
    {
        Bench bench(QStringLiteral("Box2D"), 0.0);
        Rule rule;
        rule.subjectName = Rule::world();
        rule.conditionKey = QStringLiteral("frame");
        rule.compare = Rule::Compare::Multiple;
        rule.conditionValue = 5;
        rule.targetName = bench.box->name();
        rule.actionId = Rule::cloneAction();
        rule.actionParams.insert(Rule::cloneXParam(), 300.0);
        rule.actionParams.insert(Rule::cloneYParam(), 0.0);
        rule.once = true;
        bench.scene.setRules({ rule });

        bench.run(30);
        EXPECT_EQ(bench.scene.bodies().size(), 3)
            << "a rule marked once fired more than once in thirty frames";
        bench.sim.stop();
    }
}

// Touching: the event both engines raise and the most-used one there is.
TEST(Rules, ContactFiresWhenTwoShapesTouch)
{
    for (const char *engineName : kEngines) {
        const QString contactBegin =
            shapeEventNamed(QString::fromLatin1(engineName), QStringLiteral("contactBegin"));
        ASSERT_FALSE(contactBegin.isEmpty())
            << engineName << " publishes no contactBegin event for shapes";

        Bench bench(QString::fromLatin1(engineName));
        Rule rule;
        rule.subjectName = bench.boxShape->name();
        rule.eventId = contactBegin;
        rule.targetName = bench.box->name();
        rule.actionId = Rule::initStateAction();
        bench.scene.setRules({ rule });

        // It falls to the ground, touches, and the rule puts it back up.
        bench.run(20);
        const qreal beforeLanding = bench.boxShape->pos().y();
        EXPECT_GT(beforeLanding, 0.0) << engineName << ": it is on its way down";
        bench.run(200);
        EXPECT_LT(bench.boxShape->pos().y(), 260.0)
            << engineName << ": touching the ground should have sent it back to the start,"
            << " and it is resting at " << bench.boxShape->pos().y();
        bench.sim.stop();
    }
}

// A rule survives the file exactly as written -- every field of it.
TEST(Rules, RulesSurviveTheFile)
{
    Bench bench(QStringLiteral("Box2D"));

    Rule written;
    written.name = QStringLiteral("put it back");
    written.subjectName = bench.boxShape->name();
    written.eventId = QStringLiteral("beginContact");
    written.targetName = bench.box->name();
    written.actionId = Rule::cloneAction();
    written.actionParams.insert(Rule::cloneXParam(), 120.0);
    written.actionParams.insert(Rule::cloneYParam(), -80.0);
    written.once = true;

    Rule valued;
    valued.subjectName = Rule::world();
    valued.conditionKey = QStringLiteral("frame");
    valued.compare = Rule::Compare::Multiple;
    valued.conditionValue = 25;
    valued.targetName = bench.box->name();
    valued.propertyKey = QStringLiteral("gravityScale");
    valued.op = Rule::Op::Add;
    valued.value = 0.5;
    valued.sourceObject = bench.box->name();
    valued.sourceProperty = QStringLiteral("positionY");
    valued.sourceOffset = 12.5;
    valued.enabled = false;

    bench.scene.setRules({ written, valued });

    const QJsonObject document = SceneSerializer::save(&bench.scene);
    CanvasScene reopened;
    QString error;
    ASSERT_TRUE(SceneSerializer::load(&reopened, document, &error)) << error.toStdString();
    ASSERT_EQ(reopened.rules().size(), 2);

    const Rule &a = reopened.rules().at(0);
    EXPECT_EQ(a.name, written.name);
    EXPECT_EQ(a.subjectName, written.subjectName);
    EXPECT_EQ(a.eventId, written.eventId);
    EXPECT_EQ(a.targetName, written.targetName);
    EXPECT_EQ(a.actionId, written.actionId);
    EXPECT_EQ(a.actionParams, written.actionParams);
    EXPECT_EQ(a.once, written.once);

    const Rule &b = reopened.rules().at(1);
    EXPECT_EQ(b.conditionKey, valued.conditionKey);
    EXPECT_EQ(int(b.compare), int(valued.compare));
    EXPECT_EQ(b.conditionValue.toInt(), valued.conditionValue.toInt());
    EXPECT_EQ(b.propertyKey, valued.propertyKey);
    EXPECT_EQ(int(b.op), int(valued.op));
    EXPECT_EQ(b.value.toDouble(), valued.value.toDouble());
    EXPECT_EQ(b.sourceObject, valued.sourceObject);
    EXPECT_EQ(b.sourceProperty, valued.sourceProperty);
    EXPECT_DOUBLE_EQ(b.sourceOffset, valued.sourceOffset);
    EXPECT_EQ(b.enabled, valued.enabled);
}

