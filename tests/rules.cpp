// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ruslan Muhlinin. See LICENSE.
#include "CanvasScene.h"
#include "CircleItem.h"
#include "EngineRegistry.h"
#include "IPhysicsEngine.h"
#include "PhysicsBody.h"
#include "RectangleItem.h"
#include "ExplosionItem.h"
#include "RayItem.h"
#include "Rule.h"
#include "SceneControl.h"
#include "SceneVariable.h"
#include "SceneSerializer.h"
#include "ShapeItem.h"
#include "SimulationController.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QSet>
#include <cmath>
#include <cstdio>
#include "PropertyPanel.h"
#include <QTabWidget>
#include "PropertyPane/ExplosionPropertyPane.h"
#include "Joint.h"
#include <QKeyEvent>
#include <QCoreApplication>
#include "FullScreenView.h"
#include <QSignalSpy>
#include <gtest/gtest.h>
#include "EditorMode.h"

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
        rule.conditions[0].subjectName = Rule::world();
        rule.conditions[0].conditionKey = QStringLiteral("frame");
        rule.conditions[0].compare = Rule::Compare::Multiple;
        rule.conditions[0].conditionValue = 1;
        rule.actions[0].targetName = bench.box->name();
        rule.actions[0].actionId = action.id;
        for (const physics::JointParam &param : action.params) {
            const QVariant value = somethingElse(param);
            if (value.isValid())
                rule.actions[0].actionParams.insert(param.key, value);
        }
        // A force is quoted in scene units and divided by the scene's scale on
        // the way in, and it acts for the one step it is applied in -- so the
        // number that shifts a two-gram box is nothing like the number that
        // would in newtons. This is the trap the manual warns about, not a
        // fault in the action, so the test uses a figure that does something
        // at this scale rather than one that reads well.
        if (action.id == QLatin1String("pushForceAt")) {
            rule.actions[0].actionParams.insert(QStringLiteral("impulseX"), 100000.0);
            rule.actions[0].actionParams.insert(QStringLiteral("impulseY"), 0.0);
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
        rule.conditions[0].subjectName = Rule::world();
        rule.conditions[0].conditionKey = QStringLiteral("frame");
        rule.conditions[0].compare = Rule::Compare::Multiple;
        rule.conditions[0].conditionValue = 60;
        rule.actions[0].targetName = bench.box->name();
        rule.actions[0].actionId = Rule::initStateAction();
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
            clone.conditions[0].subjectName = Rule::world();
            clone.conditions[0].eventId = Rule::runStartedEvent();
            clone.actions[0].targetName = bench.box->name();
            clone.actions[0].actionId = Rule::cloneAction();
            clone.actions[0].actionParams.insert(Rule::cloneXParam(), 200.0);
            clone.actions[0].actionParams.insert(Rule::cloneYParam(), -100.0);
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
            stop.conditions[0].subjectName = Rule::world();
            stop.conditions[0].conditionKey = QStringLiteral("frame");
            stop.conditions[0].compare = Rule::Compare::Multiple;
            stop.conditions[0].conditionValue = 30;
            stop.actions[0].targetName = Rule::world();
            stop.actions[0].actionId = Rule::stopRunAction();
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
        rule.conditions[0].subjectName = bench.box->name();
        rule.conditions[0].conditionKey = QStringLiteral("positionY");
        rule.conditions[0].compare = Rule::Compare::Greater;
        rule.conditions[0].conditionValue = 100.0;
        rule.actions[0].targetName = bench.box->name();
        rule.actions[0].actionId = Rule::initStateAction();
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
    rule.conditions[0].subjectName = Rule::world();
    rule.conditions[0].conditionKey = QStringLiteral("frame");
    rule.conditions[0].compare = Rule::Compare::Multiple;
    rule.conditions[0].conditionValue = 10;
    rule.actions[0].targetName = bench.box->name();
    rule.actions[0].actionId = Rule::cloneAction();
    rule.actions[0].actionParams.insert(Rule::cloneXParam(), 300.0);
    rule.actions[0].actionParams.insert(Rule::cloneYParam(), 0.0);
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
        rule.conditions[0].subjectName = Rule::world();
        rule.conditions[0].conditionKey = QStringLiteral("frame");
        rule.conditions[0].compare = Rule::Compare::Multiple;
        rule.conditions[0].conditionValue = 5;
        rule.actions[0].targetName = bench.box->name();
        rule.actions[0].actionId = Rule::cloneAction();
        rule.actions[0].actionParams.insert(Rule::cloneXParam(), 300.0);
        rule.actions[0].actionParams.insert(Rule::cloneYParam(), 0.0);
        rule.enabled = false;
        bench.scene.setRules({ rule });

        bench.run(30);
        EXPECT_EQ(bench.scene.bodies().size(), 2) << "a disabled rule did something";
        bench.sim.stop();
    }
    {
        Bench bench(QStringLiteral("Box2D"), 0.0);
        Rule rule;
        rule.conditions[0].subjectName = Rule::world();
        rule.conditions[0].conditionKey = QStringLiteral("frame");
        rule.conditions[0].compare = Rule::Compare::Multiple;
        rule.conditions[0].conditionValue = 5;
        rule.actions[0].targetName = bench.box->name();
        rule.actions[0].actionId = Rule::cloneAction();
        rule.actions[0].actionParams.insert(Rule::cloneXParam(), 300.0);
        rule.actions[0].actionParams.insert(Rule::cloneYParam(), 0.0);
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
        rule.conditions[0].subjectName = bench.boxShape->name();
        rule.conditions[0].eventId = contactBegin;
        rule.actions[0].targetName = bench.box->name();
        rule.actions[0].actionId = Rule::initStateAction();
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

// A card can watch more than one thing, and the join says how they are read
// together. Frames are what these count on: true from a given step onward, and
// settled by nothing else in the scene.
TEST(Rules, ConditionsJoinWithAllOfOrAnyOf)
{
    const auto pastFrame = [](int n) {
        RuleCondition condition;
        condition.subjectName = Rule::world();
        condition.conditionKey = QStringLiteral("frame");
        condition.compare = Rule::Compare::Greater;
        condition.conditionValue = n;
        return condition;
    };
    const auto cloneRule = [&](Rule::Join join, const RuleCondition &a, const RuleCondition &b,
                               const QString &target) {
        Rule rule;
        rule.join = join;
        rule.conditions = { a, b };
        rule.actions[0].targetName = target;
        rule.actions[0].actionId = Rule::cloneAction();
        rule.actions[0].actionParams.insert(Rule::cloneXParam(), 300.0);
        rule.actions[0].actionParams.insert(Rule::cloneYParam(), 0.0);
        return rule;
    };

    {
        Bench bench(QStringLiteral("Box2D"), 0.0);
        bench.scene.setRules({ cloneRule(Rule::Join::All, pastFrame(5), pastFrame(10),
                                         bench.box->name()) });
        bench.run(8);
        EXPECT_EQ(bench.scene.bodies().size(), 2)
            << "all-of fired on the first condition alone";
        bench.run(4);
        EXPECT_EQ(bench.scene.bodies().size(), 3) << "all-of did not fire once both were true";
        // The rule fires as the card becomes true, not for as long as it stays
        // true -- the same edge every other condition is read on.
        bench.run(30);
        EXPECT_EQ(bench.scene.bodies().size(), 3) << "all-of fired again while it stayed true";
        bench.sim.stop();
    }
    {
        Bench bench(QStringLiteral("Box2D"), 0.0);
        bench.scene.setRules({ cloneRule(Rule::Join::Any, pastFrame(5), pastFrame(10),
                                         bench.box->name()) });
        bench.run(8);
        EXPECT_EQ(bench.scene.bodies().size(), 3) << "any-of did not fire on the first one";
        bench.run(30);
        EXPECT_EQ(bench.scene.bodies().size(), 3)
            << "the second condition coming true fired any-of a second time";
        bench.sim.stop();
    }
}

// Every action on the card, in the order listed, each with its own settings.
TEST(Rules, EveryActionOnACardIsCarriedOut)
{
    Bench bench(QStringLiteral("Box2D"), 0.0);

    const auto cloneAt = [&](qreal x) {
        RuleAction action;
        action.targetName = bench.box->name();
        action.actionId = Rule::cloneAction();
        action.actionParams.insert(Rule::cloneXParam(), x);
        action.actionParams.insert(Rule::cloneYParam(), 0.0);
        return action;
    };

    Rule rule;
    rule.conditions[0].subjectName = Rule::world();
    rule.conditions[0].conditionKey = QStringLiteral("frame");
    rule.conditions[0].compare = Rule::Compare::Greater;
    rule.conditions[0].conditionValue = 5;
    rule.actions = { cloneAt(300.0), cloneAt(500.0), cloneAt(700.0) };
    rule.once = true;
    bench.scene.setRules({ rule });

    bench.run(10);
    ASSERT_EQ(bench.scene.bodies().size(), 5) << "not every action on the card happened";

    QVector<qreal> xs;
    for (PhysicsBody *body : bench.scene.bodies()) {
        if (body != bench.box && body != bench.ground)
            xs.append(body->shapes().isEmpty() ? 0.0 : body->shapes().first()->pos().x());
    }
    std::sort(xs.begin(), xs.end());
    ASSERT_EQ(xs.size(), 3);
    EXPECT_NEAR(xs[0], 300.0, 1.0);
    EXPECT_NEAR(xs[1], 500.0, 1.0);
    EXPECT_NEAR(xs[2], 700.0, 1.0);
    bench.sim.stop();
}

// An unfinished rule is kept rather than thrown away -- it used to vanish on
// save, taking however much of it had been written -- and is passed over by
// the run instead.
TEST(Rules, AnUnfinishedRuleIsKeptAndPassedOver)
{
    Bench bench(QStringLiteral("Box2D"), 0.0);

    Rule half;
    half.name = QStringLiteral("half written");
    half.conditions[0].subjectName = Rule::world();
    half.conditions[0].conditionKey = QStringLiteral("frame");
    half.conditions[0].compare = Rule::Compare::Greater;
    half.conditions[0].conditionValue = 1;
    half.actions[0].targetName = bench.box->name();
    // No property and no action: there is nothing for it to do.
    ASSERT_EQ(half.problem(), Rule::Problem::NoProperty);
    bench.scene.setRules({ half });

    bench.run(20);
    EXPECT_EQ(bench.scene.bodies().size(), 2) << "an unfinished rule did something";
    EXPECT_TRUE(bench.sim.problems().isEmpty())
        << bench.sim.problems().join(QStringLiteral("; ")).toStdString();
    bench.sim.stop();

    const QJsonObject document = SceneSerializer::save(&bench.scene);
    CanvasScene reopened;
    QString error;
    ASSERT_TRUE(SceneSerializer::load(&reopened, document, &error)) << error.toStdString();
    ASSERT_EQ(reopened.rules().size(), 1) << "an unfinished rule was dropped by the file";
    EXPECT_EQ(reopened.rules().first().name, half.name);
    EXPECT_EQ(reopened.rules().first().problem(), Rule::Problem::NoProperty)
        << "the mark is worked out again from the fields, not read from the file";
}

// "Changed to" and "changed from" look at the step before as well: true only
// where the reading moved, and moved onto or off the value. A rule counting
// with them is the pairing they exist for -- something happens, a tally goes
// up by one.
TEST(Rules, ChangedToAndChangedFromWatchTheStepBefore)
{
    Bench bench(QStringLiteral("Box2D"), 0.0);

    // A flag on the body that a rule can move about, and two rules watching it
    // change in either direction. Counting is done by cloning, so how many
    // bodies exist says how many times each fired.
    const auto onChange = [&](Rule::Compare compare, bool value, qreal cloneX) {
        Rule rule;
        rule.conditions[0].subjectName = bench.box->name();
        rule.conditions[0].conditionKey = QStringLiteral("isBullet");
        rule.conditions[0].compare = compare;
        rule.conditions[0].conditionValue = value;
        rule.actions[0].targetName = bench.box->name();
        rule.actions[0].actionId = Rule::cloneAction();
        rule.actions[0].actionParams.insert(Rule::cloneXParam(), cloneX);
        rule.actions[0].actionParams.insert(Rule::cloneYParam(), 0.0);
        return rule;
    };

    // Something to move the flag: on at frame 5, off again at frame 15.
    const auto setFlagAt = [&](int frame, bool value) {
        Rule rule;
        rule.conditions[0].subjectName = Rule::world();
        rule.conditions[0].conditionKey = QStringLiteral("frame");
        rule.conditions[0].compare = Rule::Compare::Greater;
        rule.conditions[0].conditionValue = frame;
        rule.actions[0].targetName = bench.box->name();
        rule.actions[0].propertyKey = QStringLiteral("isBullet");
        rule.actions[0].op = Rule::Op::Set;
        rule.actions[0].value = value;
        rule.once = true;
        return rule;
    };

    bench.scene.setRules({ setFlagAt(5, true), setFlagAt(15, false),
                           onChange(Rule::Compare::ChangedTo, true, 300.0),
                           onChange(Rule::Compare::ChangedFrom, true, 500.0) });

    // The flag starts false and stays false: nothing has changed, and a run
    // does not begin by firing every rule that watches a change.
    bench.run(4);
    EXPECT_EQ(bench.scene.bodies().size(), 2) << "a change rule fired before anything changed";

    bench.run(6); // past frame 5, so the flag goes true
    EXPECT_EQ(bench.scene.bodies().size(), 3) << "changed-to-true did not fire when it went true";

    // It stays true for the next ten frames, and staying is not changing.
    bench.run(5);
    EXPECT_EQ(bench.scene.bodies().size(), 3)
        << "changed-to fired again while the value merely stayed there";

    bench.run(6); // past frame 15, so the flag goes false again
    EXPECT_EQ(bench.scene.bodies().size(), 4)
        << "changed-from-true did not fire when it stopped being true";

    bench.run(20);
    EXPECT_EQ(bench.scene.bodies().size(), 4) << "a change rule fired with nothing changing";
    bench.sim.stop();
}

// Increment and decrement move the value the property already has, rather than
// replacing it.
TEST(Rules, IncrementAndDecrementMoveTheValueFromWhereItStands)
{
    const auto countBy = [](Bench &bench, Rule::Op op, qreal by) {
        Rule rule;
        rule.conditions[0].subjectName = Rule::world();
        rule.conditions[0].conditionKey = QStringLiteral("frame");
        rule.conditions[0].compare = Rule::Compare::Multiple;
        rule.conditions[0].conditionValue = 5;
        rule.actions[0].targetName = bench.box->name();
        rule.actions[0].propertyKey = QStringLiteral("gravityScale");
        rule.actions[0].op = op;
        rule.actions[0].value = by;
        bench.scene.setRules({ rule });
    };

    {
        Bench bench(QStringLiteral("Box2D"), 0.0);
        countBy(bench, Rule::Op::Add, 0.5);
        bench.run(16); // frames 5, 10 and 15
        EXPECT_NEAR(bench.sim.readValue(bench.box->name(),
                                        QStringLiteral("gravityScale")).toDouble(),
                    2.5, 1e-4)
            << "three increments of 0.5 did not count on from 1";
        bench.sim.stop();
    }
    {
        Bench bench(QStringLiteral("Box2D"), 0.0);
        countBy(bench, Rule::Op::Subtract, 0.25);
        bench.run(16);
        EXPECT_NEAR(bench.sim.readValue(bench.box->name(),
                                        QStringLiteral("gravityScale")).toDouble(),
                    0.25, 1e-4)
            << "three decrements of 0.25 did not count down from 1";
        bench.sim.stop();
    }
}

// The scene's own variables: rules count with them, they start each run where
// the scene says, and they are gone again when it stops.
TEST(Rules, VariablesAreCountedWithAndResetByEachRun)
{
    Bench bench(QStringLiteral("Box2D"), 0.0);

    SceneVariable score;
    score.name = QStringLiteral("score");
    score.type = SceneVariable::Type::Integer;
    score.initial = 10;

    SceneVariable running;
    running.name = QStringLiteral("running");
    running.type = SceneVariable::Type::Bool;
    running.initial = false;
    bench.scene.setVariables({ score, running });

    // Every fifth frame, count the score on by three and set the flag.
    Rule count;
    count.conditions[0].subjectName = Rule::world();
    count.conditions[0].conditionKey = QStringLiteral("frame");
    count.conditions[0].compare = Rule::Compare::Multiple;
    count.conditions[0].conditionValue = 5;
    count.actions.resize(2);
    count.actions[0].targetName = Rule::variables();
    count.actions[0].propertyKey = QStringLiteral("score");
    count.actions[0].op = Rule::Op::Add;
    count.actions[0].value = 3;
    count.actions[1].targetName = Rule::variables();
    count.actions[1].propertyKey = QStringLiteral("running");
    count.actions[1].op = Rule::Op::Set;
    count.actions[1].value = true;

    // And a rule reading one back: once the score passes 15, clone the box.
    Rule reward;
    reward.conditions[0].subjectName = Rule::variables();
    reward.conditions[0].conditionKey = QStringLiteral("score");
    reward.conditions[0].compare = Rule::Compare::Greater;
    reward.conditions[0].conditionValue = 15;
    reward.actions[0].targetName = bench.box->name();
    reward.actions[0].actionId = Rule::cloneAction();
    reward.actions[0].actionParams.insert(Rule::cloneXParam(), 300.0);
    reward.actions[0].actionParams.insert(Rule::cloneYParam(), 0.0);

    bench.scene.setRules({ count, reward });

    // Before a run, a variable reads as what it starts at.
    EXPECT_EQ(bench.sim.initialValue(Rule::variables(), QStringLiteral("score")).toInt(), 10);

    bench.run(6); // frame 5 counted once
    EXPECT_EQ(bench.sim.readValue(Rule::variables(), QStringLiteral("score")).toInt(), 13);
    EXPECT_TRUE(bench.sim.readValue(Rule::variables(), QStringLiteral("running")).toBool());
    EXPECT_EQ(bench.scene.bodies().size(), 2) << "the reward fired before the score passed 15";

    bench.run(5); // frame 10
    EXPECT_EQ(bench.sim.readValue(Rule::variables(), QStringLiteral("score")).toInt(), 16);
    EXPECT_EQ(bench.scene.bodies().size(), 3) << "a rule reading a variable did not fire";

    // Renaming one carries every rule and log row that named it along, the way
    // renaming a body does -- otherwise each of them would quietly go
    // unfinished the moment the name changed.
    bench.scene.addWatch({ Rule::variables(), QStringLiteral("score"),
                           QStringLiteral("score") });
    QVector<SceneVariable> renamed = bench.scene.variables();
    renamed[0].name = QStringLiteral("points");
    bench.scene.replaceVariables(renamed);
    bench.scene.renameVariableInRules(QStringLiteral("score"), QStringLiteral("points"));

    EXPECT_EQ(bench.scene.rules().at(0).actions[0].propertyKey, QStringLiteral("points"))
        << "a rule writing the variable kept the old name";
    EXPECT_EQ(bench.scene.rules().at(1).conditions[0].conditionKey, QStringLiteral("points"))
        << "a rule reading the variable kept the old name";
    ASSERT_EQ(bench.scene.watches().size(), 1);
    EXPECT_EQ(bench.scene.watches().first().propertyKey, QStringLiteral("points"))
        << "the log row kept the old name";

    // Stopping puts them back, the way it puts every shape back.
    bench.sim.stop();
    EXPECT_EQ(bench.sim.initialValue(Rule::variables(), QStringLiteral("points")).toInt(), 10);

    bench.run(6);
    EXPECT_EQ(bench.sim.readValue(Rule::variables(), QStringLiteral("points")).toInt(), 13)
        << "a second run carried on from where the first left off";
    bench.sim.stop();
}

// Rules address objects by name, so two objects sharing one makes a rule
// ambiguous -- it acts on whichever comes first. Everything a rule can name is
// therefore in one namespace, rays and explosions included.
TEST(Rules, EverythingARuleCanNameGetsANameOfItsOwn)
{
    CanvasScene scene;

    QSet<QString> seen;
    const auto fresh = [&seen](const QString &name, const char *what) {
        EXPECT_FALSE(name.isEmpty()) << what << " was given no name at all";
        EXPECT_FALSE(seen.contains(name))
            << what << " was called " << name.toStdString() << ", which was taken";
        seen.insert(name);
    };

    for (int i = 0; i < 3; ++i) {
        fresh(scene.addRay(QPointF(i * 50, 0))->name(), "a ray");
        fresh(scene.addExplosion(QPointF(i * 50, 100))->name(), "an explosion");
        fresh(scene.addRectangle(QPointF(i * 50, 200))->name(), "a rectangle");
        fresh(scene.addCircle(QPointF(i * 50, 300))->name(), "a circle");
    }

    // And the scene agrees: every one of them is in the namespace, which is
    // what stops the next one being given a name already in use.
    const QSet<QString> taken = scene.takenNames();
    for (const QString &name : seen)
        EXPECT_TRUE(taken.contains(name)) << name.toStdString() << " is not in the namespace";
}

// A rule survives the file exactly as written -- every field of it.
TEST(Rules, RulesSurviveTheFile)
{
    Bench bench(QStringLiteral("Box2D"));

    Rule written;
    written.name = QStringLiteral("put it back");
    written.conditions[0].subjectName = bench.boxShape->name();
    written.conditions[0].eventId = QStringLiteral("beginContact");
    written.actions[0].targetName = bench.box->name();
    written.actions[0].actionId = Rule::cloneAction();
    written.actions[0].actionParams.insert(Rule::cloneXParam(), 120.0);
    written.actions[0].actionParams.insert(Rule::cloneYParam(), -80.0);
    written.once = true;

    Rule valued;
    valued.conditions[0].subjectName = Rule::world();
    valued.conditions[0].conditionKey = QStringLiteral("frame");
    valued.conditions[0].compare = Rule::Compare::Multiple;
    valued.conditions[0].conditionValue = 25;
    valued.actions[0].targetName = bench.box->name();
    valued.actions[0].propertyKey = QStringLiteral("gravityScale");
    valued.actions[0].op = Rule::Op::Add;
    valued.actions[0].value = 0.5;
    valued.actions[0].sourceObject = bench.box->name();
    valued.actions[0].sourceProperty = QStringLiteral("positionY");
    valued.actions[0].sourceOffset = 12.5;
    valued.enabled = false;

    // A third with more than one of each, so the arrays go through the file
    // alongside the flat form the first two use.
    Rule compound;
    compound.name = QStringLiteral("compound");
    compound.join = Rule::Join::Any;
    compound.conditions = { valued.conditions[0], valued.conditions[0] };
    compound.conditions[1].conditionKey = QStringLiteral("time");
    compound.conditions[1].compare = Rule::Compare::LessEqual;
    compound.actions = { written.actions[0], written.actions[0] };
    compound.actions[1].actionParams.insert(Rule::cloneXParam(), 999.0);

    SceneVariable tally;
    tally.name = QStringLiteral("tally");
    tally.type = SceneVariable::Type::Integer;
    tally.initial = 7;
    bench.scene.setVariables({ tally });

    bench.scene.setRules({ written, valued, compound });

    const QJsonObject document = SceneSerializer::save(&bench.scene);
    CanvasScene reopened;
    QString error;
    ASSERT_TRUE(SceneSerializer::load(&reopened, document, &error)) << error.toStdString();
    ASSERT_EQ(reopened.rules().size(), 3);

    const Rule &a = reopened.rules().at(0);
    EXPECT_EQ(a.name, written.name);
    EXPECT_EQ(a.conditions[0].subjectName, written.conditions[0].subjectName);
    EXPECT_EQ(a.conditions[0].eventId, written.conditions[0].eventId);
    EXPECT_EQ(a.actions[0].targetName, written.actions[0].targetName);
    EXPECT_EQ(a.actions[0].actionId, written.actions[0].actionId);
    EXPECT_EQ(a.actions[0].actionParams, written.actions[0].actionParams);
    EXPECT_EQ(a.once, written.once);

    const Rule &b = reopened.rules().at(1);
    EXPECT_EQ(b.conditions[0].conditionKey, valued.conditions[0].conditionKey);
    EXPECT_EQ(int(b.conditions[0].compare), int(valued.conditions[0].compare));
    EXPECT_EQ(b.conditions[0].conditionValue.toInt(), valued.conditions[0].conditionValue.toInt());
    EXPECT_EQ(b.actions[0].propertyKey, valued.actions[0].propertyKey);
    EXPECT_EQ(int(b.actions[0].op), int(valued.actions[0].op));
    EXPECT_EQ(b.actions[0].value.toDouble(), valued.actions[0].value.toDouble());
    EXPECT_EQ(b.actions[0].sourceObject, valued.actions[0].sourceObject);
    EXPECT_EQ(b.actions[0].sourceProperty, valued.actions[0].sourceProperty);
    EXPECT_DOUBLE_EQ(b.actions[0].sourceOffset, valued.actions[0].sourceOffset);
    EXPECT_EQ(b.enabled, valued.enabled);

    ASSERT_EQ(reopened.variables().size(), 1) << "a variable was dropped by the file";
    EXPECT_EQ(reopened.variables().first().name, QStringLiteral("tally"));
    EXPECT_EQ(int(reopened.variables().first().type), int(SceneVariable::Type::Integer));
    EXPECT_EQ(reopened.variables().first().value().toInt(), 7);

    const Rule &c = reopened.rules().at(2);
    EXPECT_EQ(int(c.join), int(Rule::Join::Any));
    ASSERT_EQ(c.conditions.size(), 2);
    ASSERT_EQ(c.actions.size(), 2);
    EXPECT_EQ(c.conditions[1].conditionKey, QStringLiteral("time"));
    EXPECT_EQ(int(c.conditions[1].compare), int(Rule::Compare::LessEqual));
    EXPECT_NEAR(c.actions[1].actionParams.value(Rule::cloneXParam()).toDouble(), 999.0, 1e-9);

    // A file written before a rule could hold more than one of either carries
    // the single condition and action on the rule itself, with no arrays. It
    // has to load as a card with one of each.
    QJsonObject older = document;
    QJsonArray rules = older.value(QStringLiteral("rules")).toArray();
    QJsonObject flat = rules.at(2).toObject();
    ASSERT_TRUE(flat.contains(QStringLiteral("conditions")))
        << "a compound rule was not written with its arrays";
    flat.remove(QStringLiteral("conditions"));
    flat.remove(QStringLiteral("actions"));
    flat.remove(QStringLiteral("join"));
    rules.replace(2, flat);
    older.insert(QStringLiteral("rules"), rules);

    CanvasScene old;
    ASSERT_TRUE(SceneSerializer::load(&old, older, &error)) << error.toStdString();
    ASSERT_EQ(old.rules().size(), 3);
    EXPECT_EQ(old.rules().at(2).conditions.size(), 1);
    EXPECT_EQ(old.rules().at(2).actions.size(), 1);
    EXPECT_EQ(int(old.rules().at(2).join), int(Rule::Join::All)); // nothing said, the default
}


// A timer variable: it stands at what the scene declares until a rule starts
// it, counts in milliseconds from there, and answers the four verbs only it
// has. The counting is checked at a pace whose step is not a whole number of
// milliseconds, because that is where a timer that dropped the fraction each
// step would drift -- 60 steps of 16.67 ms is a second, not 960 ms.
TEST(Rules, TimerVariableCountsOnlyWhileStarted)
{
    Bench bench(QStringLiteral("Box2D"), 0.0);

    SceneVariable clock;
    clock.name = QStringLiteral("clock");
    clock.type = SceneVariable::Type::Timer;
    clock.initial = 0;
    bench.scene.setVariables({ clock });

    const auto reading = [&bench] {
        return bench.sim.readValue(Rule::variables(), QStringLiteral("clock")).toInt();
    };
    const auto verb = [&bench](Rule::Op op) {
        Rule rule;
        rule.conditions[0].subjectName = Rule::world();
        rule.conditions[0].eventId = Rule::runStartedEvent();
        rule.actions[0].targetName = Rule::variables();
        rule.actions[0].propertyKey = QStringLiteral("clock");
        rule.actions[0].op = op;
        return rule;
    };

    // Before a run it reads what it starts at, like any other variable.
    EXPECT_EQ(bench.sim.initialValue(Rule::variables(), QStringLiteral("clock")).toInt(), 0);

    // Left alone it never moves: a timer waits to be started.
    bench.scene.setRules({});
    bench.run(60);
    EXPECT_EQ(reading(), 0) << "a timer counted without being started";
    bench.sim.stop();

    // A rule reading it clones the box once it passes 1500 ms, so the count is
    // checked both as a number and by something acting on it.
    Rule far;
    far.conditions[0].subjectName = Rule::variables();
    far.conditions[0].conditionKey = QStringLiteral("clock");
    far.conditions[0].compare = Rule::Compare::Greater;
    far.conditions[0].conditionValue = 1500;
    far.actions[0].targetName = bench.box->name();
    far.actions[0].actionId = Rule::cloneAction();
    far.actions[0].actionParams.insert(Rule::cloneXParam(), 300.0);
    far.actions[0].actionParams.insert(Rule::cloneYParam(), 0.0);

    // Started at the top of the run, a second of steps is a second on the clock.
    bench.scene.setRules({ verb(Rule::Op::TimerStart), far });
    bench.run(60);
    EXPECT_EQ(reading(), 1000) << "a timer lost the fraction of a millisecond in a step";
    EXPECT_EQ(bench.scene.bodies().size(), 2) << "the rule fired before the timer passed 1500";

    bench.run(31);
    EXPECT_GT(reading(), 1500);
    EXPECT_EQ(bench.scene.bodies().size(), 3) << "a rule reading a timer did not fire";
    bench.sim.stop();

    // And it goes back to what the scene declares when the run stops, like
    // every other variable.
    EXPECT_EQ(bench.sim.initialValue(Rule::variables(), QStringLiteral("clock")).toInt(), 0);

    // The type and the verb both survive the file: a timer read back as a
    // double would stop counting, and a verb read back as Set would write a
    // null over the count instead of starting it.
    const QJsonObject document = SceneSerializer::save(&bench.scene);
    CanvasScene reopened;
    QString error;
    ASSERT_TRUE(SceneSerializer::load(&reopened, document, &error)) << error.toStdString();
    ASSERT_EQ(reopened.variables().size(), 1);
    EXPECT_EQ(reopened.variables().first().type, SceneVariable::Type::Timer)
        << "a timer came back from the file as some other type";
    ASSERT_FALSE(reopened.rules().isEmpty());
    EXPECT_EQ(reopened.rules().first().actions[0].op, Rule::Op::TimerStart)
        << "a timer verb came back from the file as something else";
    EXPECT_EQ(reopened.rules().first().problem(), Rule::Problem::None)
        << "a rule starting a timer was read back as unfinished";
}

// The three verbs that are not Start, each checked against what it leaves
// behind: Pause keeps the count, Stop puts it back, Reset puts it back and goes
// on counting.
TEST(Rules, TimerVerbsPauseStopAndReset)
{
    Bench bench(QStringLiteral("Box2D"), 0.0);

    SceneVariable clock;
    clock.name = QStringLiteral("clock");
    clock.type = SceneVariable::Type::Timer;
    clock.initial = 200; // not zero, so "back to the start" is visibly not "to 0"
    bench.scene.setVariables({ clock });

    const auto reading = [&bench] {
        return bench.sim.readValue(Rule::variables(), QStringLiteral("clock")).toInt();
    };
    // Start on the first frame, then the verb under test once the frame count
    // reaches `at` -- which is how a timer is driven by anything else.
    const auto scenario = [&bench](Rule::Op op, int at) {
        Rule start;
        start.conditions[0].subjectName = Rule::world();
        start.conditions[0].eventId = Rule::runStartedEvent();
        start.actions[0].targetName = Rule::variables();
        start.actions[0].propertyKey = QStringLiteral("clock");
        start.actions[0].op = Rule::Op::TimerStart;

        Rule then;
        then.conditions[0].subjectName = Rule::world();
        then.conditions[0].conditionKey = QStringLiteral("frame");
        then.conditions[0].compare = Rule::Compare::GreaterEqual;
        then.conditions[0].conditionValue = at;
        then.actions[0].targetName = Rule::variables();
        then.actions[0].propertyKey = QStringLiteral("clock");
        then.actions[0].op = op;

        bench.scene.setRules({ start, then });
    };

    // Paused at frame 30, it holds what it had reached and no more.
    scenario(Rule::Op::TimerPause, 30);
    bench.run(30);
    const int held = reading();
    EXPECT_GT(held, 200) << "the timer never started";
    bench.run(30);
    EXPECT_EQ(reading(), held) << "a paused timer went on counting";
    bench.sim.stop();

    // Stopped, it is back at what the scene declares and stays there.
    scenario(Rule::Op::TimerStop, 30);
    bench.run(30);
    EXPECT_EQ(reading(), 200) << "a stopped timer did not go back to its starting value";
    bench.run(30);
    EXPECT_EQ(reading(), 200) << "a stopped timer went on counting";
    bench.sim.stop();

    // Reset winds it back without stopping it, so it is climbing again the very
    // next step. The rule fires once, as the frame count reaches 30 -- a rule
    // goes off as its conditions become true, not on every step they stay true
    // -- so after sixty steps the clock carries the thirty since the reset and
    // not the thirty before it.
    scenario(Rule::Op::TimerReset, 30);
    bench.run(60);
    EXPECT_GT(reading(), 600) << "a reset timer stopped counting";
    EXPECT_LT(reading(), 800) << "a reset timer kept the count it had before";
    bench.sim.stop();

    // Set to jumps it, and the count goes on from where it was put rather than
    // from where the step before had left it.
    Rule start;
    start.conditions[0].subjectName = Rule::world();
    start.conditions[0].eventId = Rule::runStartedEvent();
    start.actions.resize(2);
    start.actions[0].targetName = Rule::variables();
    start.actions[0].propertyKey = QStringLiteral("clock");
    start.actions[0].op = Rule::Op::TimerStart;
    start.actions[1].targetName = Rule::variables();
    start.actions[1].propertyKey = QStringLiteral("clock");
    start.actions[1].op = Rule::Op::Set;
    start.actions[1].value = 5000;
    bench.scene.setRules({ start });
    bench.run(30);
    EXPECT_GT(reading(), 5000) << "a timer set to a value did not count on from it";
    EXPECT_LT(reading(), 5600);
    bench.sim.stop();
}

namespace {

// A scene with one falling body and a counter, so a rule has something to write.
void build(CanvasScene *scene, quint32 seed)
{
    scene->setSimulationEngineName(QStringLiteral("Box2D"));
    scene->setPixelsPerMeter(1000.0);
    scene->setEditorMode(EditorMode::Physics);
    scene->setRandomSeed(seed);

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

    SceneVariable rolled;
    rolled.name = QStringLiteral("rolled");
    rolled.type = SceneVariable::Type::Double;
    rolled.initial = 0.0;
    scene->setVariables({ rolled });
}

double runAndRead(CanvasScene *scene, int steps = 20)
{
    SimulationController sim(scene, nullptr);
    sim.setEngineName(QStringLiteral("Box2D"));
    sim.start();
    for (int s = 0; s < steps; ++s)
        sim.advance(1.0 / 60.0);
    return sim.readValue(Rule::variables(), QStringLiteral("rolled")).toDouble();
}

// Every step, write a random number between the two bounds into `rolled`.
Rule rollEveryStep(double from, double to)
{
    Rule rule;
    rule.conditions[0].subjectName = Rule::world();
    rule.conditions[0].conditionKey = QStringLiteral("frame");
    rule.conditions[0].compare = Rule::Compare::Greater;
    rule.conditions[0].conditionValue = 0;
    rule.actions[0].targetName = Rule::variables();
    rule.actions[0].propertyKey = QStringLiteral("rolled");
    rule.actions[0].op = Rule::Op::Set;
    rule.actions[0].value = RuleNumber::range(from, to, 0.0);
    return rule;
}

} // namespace

// A value written as a range lands between its two bounds, and is not the same
// number every time.
TEST(RandomValues, ARangeLandsBetweenItsBounds)
{
    QVector<double> seen;
    for (quint32 seed = 1; seed <= 12; ++seed) {
        CanvasScene scene;
        build(&scene, seed);
        scene.setRules({ rollEveryStep(-200.0, 200.0) });
        const double got = runAndRead(&scene);
        EXPECT_GE(got, -200.0) << "a rolled value fell below the range";
        EXPECT_LE(got, 200.0) << "a rolled value went above the range";
        seen << got;
    }
    int same = 0;
    for (double v : seen)
        same += qFuzzyCompare(v + 1.0, seen.first() + 1.0) ? 1 : 0;
    EXPECT_LT(same, seen.size())
        << "every seed produced the same number, so nothing is actually random";
}

// The same seed gives the same run. Without this a scene with chance in it
// could never be compared against an export of itself.
TEST(RandomValues, TheSameSeedGivesTheSameRun)
{
    CanvasScene first;
    build(&first, 20260102u);
    first.setRules({ rollEveryStep(0.0, 1000.0) });
    const double a = runAndRead(&first);

    CanvasScene second;
    build(&second, 20260102u);
    second.setRules({ rollEveryStep(0.0, 1000.0) });
    const double b = runAndRead(&second);

    EXPECT_DOUBLE_EQ(a, b) << "the same seed produced a different run";

    CanvasScene other;
    build(&other, 99u);
    other.setRules({ rollEveryStep(0.0, 1000.0) });
    EXPECT_NE(runAndRead(&other), a) << "a different seed produced the same run";
}

// A seed of zero is the scene asking for a different run every time.
TEST(RandomValues, ZeroMeansADifferentRunEveryTime)
{
    QVector<double> seen;
    for (int attempt = 0; attempt < 6; ++attempt) {
        CanvasScene scene;
        build(&scene, 0);
        scene.setRules({ rollEveryStep(0.0, 1e6) });
        seen << runAndRead(&scene);
    }
    int same = 0;
    for (double v : seen)
        same += qFuzzyCompare(v + 1.0, seen.first() + 1.0) ? 1 : 0;
    EXPECT_LT(same, seen.size()) << "a seed of zero repeated the same run";
}

// Chance is a reading of the world that is never the same twice, and a rule
// comparing it fires about that often. Seeded, so the count is fixed.
TEST(RandomValues, ChanceFiresAboutAsOftenAsItSays)
{
    CanvasScene scene;
    build(&scene, 7u);

    Rule sometimes;
    sometimes.conditions[0].subjectName = Rule::world();
    sometimes.conditions[0].conditionKey = QStringLiteral("chance");
    sometimes.conditions[0].compare = Rule::Compare::Less;
    sometimes.conditions[0].conditionValue = 25.0;
    sometimes.actions[0].targetName = Rule::variables();
    sometimes.actions[0].propertyKey = QStringLiteral("rolled");
    sometimes.actions[0].op = Rule::Op::Add;
    sometimes.actions[0].value = 1.0;
    scene.setRules({ sometimes });

    // A rule fires as its condition becomes true, so it counts the times chance
    // crossed under 25 rather than every step below it -- about half as often.
    const double fired = runAndRead(&scene, 2000);
    EXPECT_GT(fired, 100.0) << "a one-in-four chance almost never fired in 2000 steps";
    EXPECT_LT(fired, 700.0) << "a one-in-four chance fired far too often";
}

// A range survives the file: written when it is there, absent when it is not,
// so a scene from before ranges existed reads back as a plain value.
TEST(RandomValues, ARangeIsWrittenAndReadBack)
{
    CanvasScene scene;
    build(&scene, 42u);
    scene.setRules({ rollEveryStep(-50.0, 150.0) });

    const QJsonObject document = SceneSerializer::save(&scene);
    const QJsonObject written = document.value(QStringLiteral("rules")).toArray()
                                    .at(0).toObject();
    EXPECT_TRUE(written.value(QStringLiteral("value")).isObject())
        << "a range was not written as one";
    EXPECT_EQ(document.value(QStringLiteral("world")).toObject()
                  .value(QStringLiteral("randomSeed")).toDouble(), 42.0)
        << "the seed was not written to the file";

    CanvasScene back;
    QString error;
    ASSERT_TRUE(SceneSerializer::load(&back, document, &error)) << error.toStdString();
    ASSERT_EQ(back.rules().size(), 1);
    const RuleAction &read = back.rules().at(0).actions.at(0);
    EXPECT_TRUE(read.usesRange()) << "the range did not come back";
    EXPECT_DOUBLE_EQ(RuleNumber::from(read.value), -50.0);
    EXPECT_DOUBLE_EQ(RuleNumber::to(read.value), 150.0);
    EXPECT_EQ(back.randomSeed(), 42u) << "the seed did not come back";

    // And a plain value stays a plain number rather than becoming an object.
    CanvasScene plain;
    build(&plain, 0);
    Rule one = rollEveryStep(0.0, 0.0);
    one.actions[0].value = 7.0;
    plain.setRules({ one });
    EXPECT_FALSE(SceneSerializer::save(&plain).value(QStringLiteral("rules")).toArray()
                     .at(0).toObject().value(QStringLiteral("value")).isObject())
        << "a plain value was written as a range";
}

// A rule names objects, and the panel offers a way back to the one it names:
// whatever kind it is, the scene selects it.
TEST(Rules, ARuleCanBeFollowedBackToWhatItNames)
{
    CanvasScene scene;
    scene.setSimulationEngineName(QStringLiteral("Box2D"));
    scene.setEditorMode(EditorMode::Physics);

    auto *shape = new RectangleItem;
    shape->setRect(QRectF(0, 0, 40, 40));
    shape->setName(QStringLiteral("crate"));
    scene.addItem(shape);
    scene.notifyShapesChanged();
    scene.selectForPhysics(shape, true);
    PhysicsBody *body = scene.createBodyFromSelection();
    scene.clearPhysicsSelection();

    ExplosionItem *boom = scene.addExplosion(QPointF(100, 100));
    ASSERT_TRUE(boom);
    RayItem *ray = scene.addRay(QPointF(-100, 0));

    EXPECT_TRUE(scene.revealObjectNamed(boom->name()));
    EXPECT_EQ(scene.selectedExplosion(), boom) << "naming an explosion did not select it";

    if (ray) {
        EXPECT_TRUE(scene.revealObjectNamed(ray->name()));
        EXPECT_EQ(scene.selectedExplosion(), nullptr)
            << "the explosion stayed selected beside the ray";
    }

    EXPECT_TRUE(scene.revealObjectNamed(shape->name()));
    EXPECT_TRUE(scene.physicsSelection().contains(shape)) << "naming a shape did not select it";

    EXPECT_TRUE(scene.revealObjectNamed(body->name())) << "a body is nameable too";
    EXPECT_FALSE(scene.revealObjectNamed(QStringLiteral("nothing_called_this")));
}

// A ray and an explosion are drawn in both modes, so clicking one shows it in
// both: before this the Edit-mode panel answered with the world's settings,
// which reads as the click having done nothing.
TEST(Rules, ClickingARayOrExplosionShowsItInEitherMode)
{
    CanvasScene scene;
    scene.setSimulationEngineName(QStringLiteral("Box2D"));
    ExplosionItem *boom = scene.addExplosion(QPointF(50, 50));
    ASSERT_TRUE(boom);

    PropertyPanel panel;
    panel.setScene(&scene);

    for (EditorMode mode : { EditorMode::Physics, EditorMode::Edit }) {
        scene.setEditorMode(mode);
        panel.setEditorMode(mode);
        scene.selectExplosion(nullptr);
        scene.selectExplosion(boom);

        QStringList sections;
        if (auto *tabs = panel.findChild<QTabWidget *>())
            for (int i = 0; i < tabs->count(); ++i)
                sections << tabs->tabText(i);
        EXPECT_TRUE(sections.contains(QStringLiteral("Explosion")))
            << (mode == EditorMode::Edit ? "Edit" : "Physics")
            << " mode showed " << sections.join(QLatin1Char(',')).toStdString()
            << " instead of the explosion";
    }
}

// Anything a rule can name can be watched. Explosions and rays were in the
// object lists but nothing they carry could be put in the log: their rows went
// out with no engine key, and the scene could not read one back by name.
TEST(Rules, AnExplosionAndARayCanBeLogged)
{
    CanvasScene scene;
    scene.setSimulationEngineName(QStringLiteral("Box2D"));
    scene.setEditorMode(EditorMode::Physics);

    ExplosionItem *boom = scene.addExplosion(QPointF(40, 40));
    ASSERT_TRUE(boom);
    RayItem *ray = scene.addRay(QPointF(-60, 0));
    ASSERT_TRUE(ray);

    // Every row the explosion offers that carries an engine key reads back.
    scene.selectExplosion(boom);
    ExplosionPropertyPane pane;
    pane.attach(&scene);
    int readable = 0;
    for (const PropertyRow &row : pane.rows(EditorMode::Physics)) {
        if (row.key.isEmpty())
            continue;
        ++readable;
        EXPECT_TRUE(scene.readSceneValue(boom->name(), row.key).isValid())
            << "explosion " << row.key.toStdString() << " cannot be read for the log";
    }
    EXPECT_GT(readable, 0) << "no explosion row carries a key, so none can be logged";

    scene.selectExplosion(nullptr);
    scene.selectRay(ray);
    ray->setAngleDegrees(30.0);
    ray->setLength(250.0);
    EXPECT_TRUE(scene.readSceneValue(ray->name(), QStringLiteral("angle")).isValid());
    EXPECT_NEAR(scene.readSceneValue(ray->name(), QStringLiteral("length")).toDouble(), 250.0, 1e-6);
}

// A watch outlives nothing: when its object goes, or when the joint that bears
// its name no longer has that reading, it goes too. A log row reading "--"
// for the rest of the scene's life is worse than no row.
TEST(Rules, TheLogForgetsWhatIsNoLongerThere)
{
    CanvasScene scene;
    scene.setSimulationEngineName(QStringLiteral("Box2D"));
    scene.setEditorMode(EditorMode::Physics);

    auto *topShape = new RectangleItem;
    topShape->setRect(QRectF(0, 0, 40, 40));
    topShape->setName(QStringLiteral("top"));
    scene.addItem(topShape);
    auto *lowShape = new RectangleItem;
    lowShape->setRect(QRectF(0, 0, 40, 40));
    lowShape->setPos(0, 120);
    lowShape->setName(QStringLiteral("low"));
    scene.addItem(lowShape);
    scene.notifyShapesChanged();

    scene.selectForPhysics(topShape, true);
    PhysicsBody *top = scene.createBodyFromSelection();
    scene.clearPhysicsSelection();
    scene.selectForPhysics(lowShape, true);
    PhysicsBody *low = scene.createBodyFromSelection();
    scene.clearPhysicsSelection();

    Joint *hinge = scene.createJoint(QStringLiteral("revolute"), top, low, 1, {});
    ASSERT_TRUE(hinge);
    ExplosionItem *boom = scene.addExplosion(QPointF(80, 80));
    ASSERT_TRUE(boom);

    scene.addWatch({hinge->name(), QStringLiteral("angle"), QStringLiteral("Angle"), false});
    // A hinge has no translation; a sliding joint does.
    scene.addWatch({hinge->name(), QStringLiteral("translation"), QStringLiteral("Travel"), false});
    scene.addWatch({boom->name(), QStringLiteral("radius"), QStringLiteral("Radius"), false});
    scene.addWatch({Rule::world(), QStringLiteral("time"), QStringLiteral("Elapsed"), false});

    scene.pruneWatches();
    EXPECT_TRUE(scene.isWatched(hinge->name(), QStringLiteral("angle")))
        << "a hinge's angle was thrown away";
    EXPECT_FALSE(scene.isWatched(hinge->name(), QStringLiteral("translation")))
        << "a hinge has no translation, and the log kept watching one";
    EXPECT_TRUE(scene.isWatched(Rule::world(), QStringLiteral("time")))
        << "the world is not an object and outlives every one of them";

    // And when the object itself goes.
    const QString gone = hinge->name();
    scene.destroyJoint(hinge);
    EXPECT_FALSE(scene.isWatched(gone, QStringLiteral("angle")))
        << "the joint was deleted and its log row stayed";

    scene.removeExplosion(boom);
    EXPECT_FALSE(scene.isWatched(QStringLiteral("explosion_1"), QStringLiteral("radius")))
        << "the explosion was deleted and its log row stayed";
    EXPECT_TRUE(scene.isWatched(Rule::world(), QStringLiteral("time")));
}

// A key is the application's own event: no engine knows a keyboard exists. It
// goes through the scene, which has the focus while a run is going, and the
// rule names which key the way a contact rule names the other shape.
TEST(RuleSource, AKeyPressFiresARule)
{
    CanvasScene scene;
    scene.setSimulationEngineName(QStringLiteral("Box2D"));
    scene.setPixelsPerMeter(physics::kReferencePixelsPerMeter);
    scene.setEditorMode(EditorMode::Physics);

    auto *box = new RectangleItem;
    box->setRect(QRectF(0, 0, 40, 40));
    box->setPos(0, 0);
    box->setName(QStringLiteral("box"));
    scene.addItem(box);
    scene.notifyShapesChanged();
    scene.selectForPhysics(box, true);
    PhysicsBody *body = scene.createBodyFromSelection();
    ASSERT_TRUE(body);

    scene.setVariables({ SceneVariable { QStringLiteral("hits"), SceneVariable::Type::Integer, 0 },
                         SceneVariable { QStringLiteral("lets"), SceneVariable::Type::Integer, 0 },
                         SceneVariable { QStringLiteral("while"), SceneVariable::Type::Integer, 0 } });

    const auto counting = [](const QString &event, const QString &key, const QString &variable) {
        Rule rule;
        rule.conditions[0].subjectName = Rule::keyboard();
        rule.conditions[0].eventId = event;
        rule.conditions[0].conditionValue = key;
        rule.actions[0].targetName = Rule::variables();
        rule.actions[0].propertyKey = variable;
        rule.actions[0].op = Rule::Op::Add;
        rule.actions[0].value = 1;
        return rule;
    };
    scene.setRules({ counting(Rule::keyPressedEvent(), QStringLiteral("A"),
                              QStringLiteral("hits")),
                     counting(Rule::keyReleasedEvent(), QStringLiteral("A"),
                              QStringLiteral("lets")),
                     counting(Rule::keyHeldEvent(), QStringLiteral("A"),
                              QStringLiteral("while")) });

    SimulationController sim(&scene, nullptr);
    sim.setEngineName(QStringLiteral("Box2D"));
    sim.start();

    const auto step = [&] { sim.advance(1.0 / 60.0); };
    const auto count = [&](const char *name) {
        return sim.readValue(Rule::variables(), QString::fromLatin1(name)).toInt();
    };
    step();
    EXPECT_EQ(count("hits"), 0) << "a rule fired before any key was touched";

    // Through the scene, which is where a key arrives while a run is going.
    QKeyEvent down(QEvent::KeyPress, Qt::Key_A, Qt::NoModifier);
    QCoreApplication::sendEvent(&scene, &down);
    step();
    EXPECT_EQ(count("hits"), 1) << "the key press raised nothing";
    EXPECT_EQ(count("lets"), 0) << "a press counted as a release";

    // Held down: the keyboard repeats, the press does not.
    QCoreApplication::sendEvent(&scene, &down);
    step();
    EXPECT_EQ(count("hits"), 1) << "holding the key fired the press again";

    // "Key held" is the other way round: it happens on every step the key is
    // down, which is what a thruster is -- press and there is a force, let go
    // and there is none.
    const int whileHeld = count("while");
    EXPECT_GT(whileHeld, 0) << "a rule watching the held key never fired";
    step();
    step();
    EXPECT_EQ(count("while"), whileHeld + 2) << "holding it did not fire on every step";

    QKeyEvent up(QEvent::KeyRelease, Qt::Key_A, Qt::NoModifier);
    QCoreApplication::sendEvent(&scene, &up);
    step();
    EXPECT_EQ(count("lets"), 1) << "the key release raised nothing";
    const int afterLettingGo = count("while");
    step();
    step();
    EXPECT_EQ(count("while"), afterLettingGo) << "it went on firing after the key came up";

    // A different key is a different event.
    QKeyEvent other(QEvent::KeyPress, Qt::Key_B, Qt::NoModifier);
    QCoreApplication::sendEvent(&scene, &other);
    step();
    EXPECT_EQ(count("hits"), 1) << "a rule watching A fired on B";

    // And so is the same key held with a modifier: a rule asking for A is not
    // what Ctrl+A means.
    QKeyEvent withCtrl(QEvent::KeyPress, Qt::Key_A, Qt::ControlModifier);
    QCoreApplication::sendEvent(&scene, &withCtrl);
    step();
    EXPECT_EQ(count("hits"), 1) << "a rule watching A fired on Ctrl+A";

    sim.stop();
}

// Every key is the scene's. The run used to keep Space for holding itself and
// Right for stepping, which took them from every scene that wanted them -- and
// a scene that does want them can say so, because holding and ending a run are
// rule actions like any other. Escape is the one exception: without it a
// full-screen run cannot be left from the keyboard at all.
TEST(RuleSource, EveryKeyIsOfferedToARule)
{
    QSet<int> offered;
    for (const auto &key : Rule::offerableKeys())
        offered.insert(key.second);

    for (const int key : { int(Qt::Key_Space), int(Qt::Key_Right), int(Qt::Key_0),
                           int(Qt::Key_Delete), int(Qt::Key_A), int(Qt::Key_Left) }) {
        EXPECT_TRUE(offered.contains(key))
            << "key " << key << " is not offered, and nothing reserves it any more";
    }

    // Names survive the file, so a scene written today reads in a build with a
    // different Qt.
    EXPECT_EQ(Rule::keyFromName(QStringLiteral("A")), int(Qt::Key_A));
    EXPECT_EQ(Rule::nameOfKey(Qt::Key_Left), QStringLiteral("Left"));
    EXPECT_EQ(Rule::keyFromName(QStringLiteral("Space")), int(Qt::Key_Space));
    EXPECT_EQ(Rule::nameOfKey(Qt::Key_Right), QStringLiteral("Right"));
}

// A key may be held with Ctrl, Shift or Alt, and the combination is what the
// rule names -- written the way a menu writes one, and compared as that text.
TEST(RuleSource, AKeyChordFiresOnlyItsOwnCombination)
{
    EXPECT_EQ(Rule::chordName(Qt::Key_A, Qt::NoModifier), QStringLiteral("A"));
    EXPECT_EQ(Rule::chordName(Qt::Key_A, Qt::ControlModifier), QStringLiteral("Ctrl+A"));
    EXPECT_EQ(Rule::chordName(Qt::Key_Left, Qt::ControlModifier | Qt::ShiftModifier | Qt::AltModifier),
              QStringLiteral("Ctrl+Shift+Alt+Left"));
    EXPECT_EQ(Rule::keyOfChord(QStringLiteral("Ctrl+Shift+A")), QStringLiteral("A"));
    EXPECT_TRUE(Rule::modifiersOfChord(QStringLiteral("Ctrl+Shift+A"))
                    .testFlag(Qt::ShiftModifier));
    EXPECT_FALSE(Rule::modifiersOfChord(QStringLiteral("Ctrl+A")).testFlag(Qt::AltModifier));

    CanvasScene scene;
    scene.setSimulationEngineName(QStringLiteral("Box2D"));
    scene.setPixelsPerMeter(physics::kReferencePixelsPerMeter);
    scene.setEditorMode(EditorMode::Physics);
    auto *box = new RectangleItem;
    box->setRect(QRectF(0, 0, 40, 40));
    box->setName(QStringLiteral("box"));
    scene.addItem(box);
    scene.notifyShapesChanged();
    scene.selectForPhysics(box, true);
    ASSERT_TRUE(scene.createBodyFromSelection());

    scene.setVariables({ SceneVariable { QStringLiteral("hits"), SceneVariable::Type::Integer, 0 } });
    Rule rule;
    rule.conditions[0].subjectName = Rule::keyboard();
    rule.conditions[0].eventId = Rule::keyPressedEvent();
    rule.conditions[0].conditionValue = QStringLiteral("Ctrl+A");
    rule.actions[0].targetName = Rule::variables();
    rule.actions[0].propertyKey = QStringLiteral("hits");
    rule.actions[0].op = Rule::Op::Add;
    rule.actions[0].value = 1;
    scene.setRules({ rule });

    SimulationController sim(&scene, nullptr);
    sim.setEngineName(QStringLiteral("Box2D"));
    sim.start();
    const auto hits = [&] {
        return sim.readValue(Rule::variables(), QStringLiteral("hits")).toInt();
    };

    QKeyEvent bare(QEvent::KeyPress, Qt::Key_A, Qt::NoModifier);
    QCoreApplication::sendEvent(&scene, &bare);
    sim.advance(1.0 / 60.0);
    EXPECT_EQ(hits(), 0) << "a rule asking for Ctrl+A fired on a bare A";

    QKeyEvent release(QEvent::KeyRelease, Qt::Key_A, Qt::NoModifier);
    QCoreApplication::sendEvent(&scene, &release);
    QKeyEvent chord(QEvent::KeyPress, Qt::Key_A, Qt::ControlModifier);
    QCoreApplication::sendEvent(&scene, &chord);
    sim.advance(1.0 / 60.0);
    EXPECT_EQ(hits(), 1) << "Ctrl+A did not fire the rule that asked for it";
    sim.stop();
}

// The keypad's Enter is an Enter: nobody pressing it means anything different
// by it, and a rule asking for Enter was ignoring it.
TEST(RuleSource, TheKeypadsEnterIsAnEnter)
{
    EXPECT_EQ(Rule::nameOfKey(Qt::Key_Return), QStringLiteral("Enter"));
    EXPECT_EQ(Rule::nameOfKey(Qt::Key_Enter), QStringLiteral("Enter"));
    EXPECT_EQ(Rule::chordName(Qt::Key_Enter, Qt::ControlModifier), QStringLiteral("Ctrl+Enter"));
}

// Words on the canvas, put there by a rule. No engine has anything to say
// about text, so this is the application's own action -- and like everything
// else a rule does to a scene, the caption belongs to the run and goes with it.
TEST(RuleSource, ARuleShowsAndHidesACaption)
{
    CanvasScene scene;
    scene.setSimulationEngineName(QStringLiteral("Box2D"));
    scene.setPixelsPerMeter(physics::kReferencePixelsPerMeter);
    scene.setEditorMode(EditorMode::Physics);

    auto *box = new RectangleItem;
    box->setRect(QRectF(0, 0, 40, 40));
    box->setName(QStringLiteral("box"));
    scene.addItem(box);
    scene.notifyShapesChanged();
    scene.selectForPhysics(box, true);
    ASSERT_TRUE(scene.createBodyFromSelection());

    const auto saying = [](const QString &key, const QString &text, bool shown) {
        Rule rule;
        rule.conditions[0].subjectName = Rule::keyboard();
        rule.conditions[0].eventId = Rule::keyPressedEvent();
        rule.conditions[0].conditionValue = key;
        rule.actions[0].targetName = Rule::world();
        rule.actions[0].actionId = Rule::showCaptionAction();
        rule.actions[0].actionParams.insert(Rule::captionTextParam(), text);
        rule.actions[0].actionParams.insert(Rule::captionShownParam(), shown);
        return rule;
    };
    scene.setRules({ saying(QStringLiteral("A"), QStringLiteral("<b>Go</b>"), true),
                     saying(QStringLiteral("B"), QStringLiteral("<b>Go</b>"), false) });

    SimulationController sim(&scene, nullptr);
    sim.setEngineName(QStringLiteral("Box2D"));
    sim.start();
    sim.advance(1.0 / 60.0);
    EXPECT_TRUE(scene.caption().isEmpty()) << "a run started with a caption up";

    QKeyEvent show(QEvent::KeyPress, Qt::Key_A, Qt::NoModifier);
    QCoreApplication::sendEvent(&scene, &show);
    sim.advance(1.0 / 60.0);
    EXPECT_EQ(scene.caption(), QStringLiteral("<b>Go</b>")) << "the caption was not put up";
    EXPECT_TRUE(scene.captionShown());

    QKeyEvent hide(QEvent::KeyPress, Qt::Key_B, Qt::NoModifier);
    QCoreApplication::sendEvent(&scene, &hide);
    sim.advance(1.0 / 60.0);
    EXPECT_FALSE(scene.captionShown()) << "turning Shown off left the caption up";

    // It belongs to the run. A has to come up before it can go down again --
    // a key held is not a key pressed twice.
    QKeyEvent lift(QEvent::KeyRelease, Qt::Key_A, Qt::NoModifier);
    QCoreApplication::sendEvent(&scene, &lift);
    QKeyEvent again(QEvent::KeyPress, Qt::Key_A, Qt::NoModifier);
    QCoreApplication::sendEvent(&scene, &again);
    sim.advance(1.0 / 60.0);
    ASSERT_TRUE(scene.captionShown());
    sim.stop();
    EXPECT_TRUE(scene.caption().isEmpty()) << "the caption outlived the run";
    EXPECT_FALSE(scene.captionShown());
}

// Controls are the scene's own, like its variables: no engine knows a slider
// exists. A rule reads one, writes one, and hears when a hand works it.
TEST(RuleSource, RulesReadWriteAndHearControls)
{
    CanvasScene scene;
    scene.setSimulationEngineName(QStringLiteral("Box2D"));
    scene.setPixelsPerMeter(physics::kReferencePixelsPerMeter);
    scene.setEditorMode(EditorMode::Physics);

    auto *box = new RectangleItem;
    box->setRect(QRectF(0, 0, 40, 40));
    box->setName(QStringLiteral("box"));
    scene.addItem(box);
    scene.notifyShapesChanged();
    scene.selectForPhysics(box, true);
    ASSERT_TRUE(scene.createBodyFromSelection());

    SceneControl run;
    run.id = QStringLiteral("go");
    run.kind = SceneControl::Kind::Checkbox;
    run.initial = false;
    SceneControl speed;
    speed.id = QStringLiteral("speed");
    speed.kind = SceneControl::Kind::Slider;
    speed.minimum = 0.0;
    speed.maximum = 50.0;
    speed.initial = 10.0;
    SceneControl fire;
    fire.id = QStringLiteral("fire");
    fire.kind = SceneControl::Kind::Button;
    scene.setControls({ run, speed, fire });

    EXPECT_EQ(scene.controlValue(QStringLiteral("go")).toBool(), false);
    EXPECT_DOUBLE_EQ(scene.controlValue(QStringLiteral("speed")).toDouble(), 10.0);
    EXPECT_TRUE(scene.controlEnabled(QStringLiteral("speed")));

    scene.setVariables({ SceneVariable { QStringLiteral("shots"), SceneVariable::Type::Integer, 0 },
                         SceneVariable { QStringLiteral("seen"), SceneVariable::Type::Double, 0.0 } });

    // Pressing the button counts a shot.
    Rule counting;
    counting.conditions[0].subjectName = Rule::controls();
    counting.conditions[0].eventId = Rule::controlPressedEvent();
    counting.conditions[0].conditionValue = QStringLiteral("fire");
    counting.actions[0].targetName = Rule::variables();
    counting.actions[0].propertyKey = QStringLiteral("shots");
    counting.actions[0].op = Rule::Op::Add;
    counting.actions[0].value = 1;

    // While the box is ticked, the slider's value is copied into a variable.
    Rule copying;
    copying.conditions[0].subjectName = Rule::controls();
    copying.conditions[0].conditionKey = QStringLiteral("go:value");
    copying.conditions[0].compare = Rule::Compare::Equal;
    copying.conditions[0].conditionValue = true;
    copying.actions[0].targetName = Rule::variables();
    copying.actions[0].propertyKey = QStringLiteral("seen");
    copying.actions[0].op = Rule::Op::Set;
    copying.actions[0].sourceObject = Rule::controls();
    copying.actions[0].sourceProperty = QStringLiteral("speed:value");

    // And a rule can work a control itself.
    Rule greying;
    greying.conditions[0].subjectName = Rule::controls();
    greying.conditions[0].eventId = Rule::controlChangedEvent();
    greying.conditions[0].conditionValue = QStringLiteral("go");
    greying.actions[0].targetName = Rule::controls();
    greying.actions[0].propertyKey = QStringLiteral("speed:enabled");
    greying.actions[0].op = Rule::Op::Set;
    greying.actions[0].value = false;

    scene.setRules({ counting, copying, greying });

    SimulationController sim(&scene, nullptr);
    sim.setEngineName(QStringLiteral("Box2D"));
    sim.start();
    const auto step = [&] { sim.advance(1.0 / 60.0); };
    const auto shots = [&] {
        return sim.readValue(Rule::variables(), QStringLiteral("shots")).toInt();
    };

    step();
    EXPECT_EQ(shots(), 0) << "a rule fired before the button was touched";

    // A hand on the button: down, then up.
    scene.setControlValue(QStringLiteral("fire"), true);
    step();
    EXPECT_EQ(shots(), 1) << "pressing the button raised nothing";
    scene.setControlValue(QStringLiteral("fire"), false);
    step();
    EXPECT_EQ(shots(), 1) << "letting the button go counted as another press";

    // Ticking the box lets the copying rule through, and greys the slider.
    scene.setControlValue(QStringLiteral("speed"), 42.0);
    scene.setControlValue(QStringLiteral("go"), true);
    step();
    EXPECT_DOUBLE_EQ(sim.readValue(Rule::variables(), QStringLiteral("seen")).toDouble(), 42.0)
        << "the rule did not read the slider";
    EXPECT_FALSE(scene.controlEnabled(QStringLiteral("speed")))
        << "the rule did not grey the slider out";

    // A rule writing a slider is held to the bounds it was declared with.
    RuleAction push;
    push.targetName = Rule::controls();
    push.propertyKey = QStringLiteral("speed:value");
    push.op = Rule::Op::Set;
    push.value = 999.0;
    sim.applyActionForTest(push);
    EXPECT_DOUBLE_EQ(scene.controlValue(QStringLiteral("speed")).toDouble(), 50.0)
        << "a rule pushed the slider past its maximum";

    sim.stop();
    EXPECT_DOUBLE_EQ(scene.controlValue(QStringLiteral("speed")).toDouble(), 10.0)
        << "the slider did not go back to what the scene declares";
    EXPECT_FALSE(scene.controlValue(QStringLiteral("go")).toBool());
    EXPECT_TRUE(scene.controlEnabled(QStringLiteral("speed")));
}

// An event is already an edge: it is true on the step it is raised and false
// on every other. Asking it to *rise* as well swallowed the second of two that
// landed on consecutive steps -- a slider pushed up ten times in ten steps
// moved what it drove once, and the two numbers disagreed on screen.
TEST(RuleSource, AnEventOnEveryStepFiresOnEveryStep)
{
    CanvasScene scene;
    scene.setSimulationEngineName(QStringLiteral("Box2D"));
    scene.setPixelsPerMeter(physics::kReferencePixelsPerMeter);
    scene.setEditorMode(EditorMode::Physics);

    auto *box = new RectangleItem;
    box->setRect(QRectF(0, 0, 40, 40));
    box->setName(QStringLiteral("box"));
    scene.addItem(box);
    scene.notifyShapesChanged();
    scene.selectForPhysics(box, true);
    ASSERT_TRUE(scene.createBodyFromSelection());

    SceneControl pull;
    pull.id = QStringLiteral("speed");
    pull.kind = SceneControl::Kind::Slider;
    pull.minimum = 0.0;
    pull.maximum = 100.0;
    pull.step = 10.0;
    pull.initial = 0.0;
    scene.setControls({ pull });
    scene.setVariables({ SceneVariable { QStringLiteral("seen"), SceneVariable::Type::Double,
                                         0.0 } });

    Rule follow;
    follow.conditions[0].subjectName = Rule::controls();
    follow.conditions[0].eventId = Rule::controlChangedEvent();
    follow.conditions[0].conditionValue = QStringLiteral("speed");
    follow.actions[0].targetName = Rule::variables();
    follow.actions[0].propertyKey = QStringLiteral("seen");
    follow.actions[0].op = Rule::Op::Set;
    follow.actions[0].sourceObject = Rule::controls();
    follow.actions[0].sourceProperty = QStringLiteral("speed:value");
    scene.setRules({ follow });

    SimulationController sim(&scene, nullptr);
    sim.setEngineName(QStringLiteral("Box2D"));
    sim.start();
    const auto seen = [&] {
        return sim.readValue(Rule::variables(), QStringLiteral("seen")).toDouble();
    };

    // Changed on every step, with no quiet step in between to re-arm it.
    for (int at = 10; at <= 50; at += 10) {
        scene.setControlValue(QStringLiteral("speed"), double(at));
        sim.advance(1.0 / 60.0);
        EXPECT_DOUBLE_EQ(seen(), double(at))
            << "the change on this step was swallowed: the two disagree at " << at;
    }

    // A reading still needs its edge, or it would fire on every step it stays
    // true for.
    scene.setVariables({ SceneVariable { QStringLiteral("seen"), SceneVariable::Type::Double, 0.0 },
                         SceneVariable { QStringLiteral("count"), SceneVariable::Type::Integer,
                                         0 } });
    Rule counting;
    counting.conditions[0].subjectName = Rule::controls();
    counting.conditions[0].conditionKey = QStringLiteral("speed:value");
    counting.conditions[0].compare = Rule::Compare::Greater;
    counting.conditions[0].conditionValue = 5.0;
    counting.actions[0].targetName = Rule::variables();
    counting.actions[0].propertyKey = QStringLiteral("count");
    counting.actions[0].op = Rule::Op::Add;
    counting.actions[0].value = 1;
    scene.setRules({ counting });
    sim.stop();
    sim.start();

    scene.setControlValue(QStringLiteral("speed"), 50.0);
    for (int step = 0; step < 5; ++step)
        sim.advance(1.0 / 60.0);
    EXPECT_EQ(sim.readValue(Rule::variables(), QStringLiteral("count")).toInt(), 1)
        << "a reading fired on every step it stayed true for";
    sim.stop();
}

// A full-screen run is a different view onto the same scene, and keys reach
// the scene through whichever view has them. This drives the view directly:
// whether the window is given the focus when it opens is the one part a test
// on a machine with no window manager cannot answer.
TEST(RuleSource, AKeyInTheFullScreenViewReachesTheRules)
{
    CanvasScene scene;
    scene.setSimulationEngineName(QStringLiteral("Box2D"));
    scene.setPixelsPerMeter(physics::kReferencePixelsPerMeter);
    scene.setEditorMode(EditorMode::Physics);

    auto *box = new RectangleItem;
    box->setRect(QRectF(0, 0, 40, 40));
    box->setName(QStringLiteral("box"));
    scene.addItem(box);
    scene.notifyShapesChanged();
    scene.selectForPhysics(box, true);
    ASSERT_TRUE(scene.createBodyFromSelection());

    scene.setVariables({ SceneVariable { QStringLiteral("hits"), SceneVariable::Type::Integer,
                                         0 } });
    Rule rule;
    rule.conditions[0].subjectName = Rule::keyboard();
    rule.conditions[0].eventId = Rule::keyPressedEvent();
    rule.conditions[0].conditionValue = QStringLiteral("A");
    rule.actions[0].targetName = Rule::variables();
    rule.actions[0].propertyKey = QStringLiteral("hits");
    rule.actions[0].op = Rule::Op::Add;
    rule.actions[0].value = 1;
    scene.setRules({ rule });

    SimulationController sim(&scene, nullptr);
    sim.setEngineName(QStringLiteral("Box2D"));
    sim.start();
    sim.advance(1.0 / 60.0);

    FullScreenView view(&scene, nullptr);
    view.resize(640, 480);
    const auto hits = [&] {
        return sim.readValue(Rule::variables(), QStringLiteral("hits")).toInt();
    };
    ASSERT_EQ(hits(), 0);

    // Delivered to the full-screen view, not to the scene.
    QKeyEvent down(QEvent::KeyPress, Qt::Key_A, Qt::NoModifier);
    QCoreApplication::sendEvent(&view, &down);
    sim.advance(1.0 / 60.0);
    EXPECT_EQ(hits(), 1) << "a key given to the full-screen view never reached the rules";

    QKeyEvent up(QEvent::KeyRelease, Qt::Key_A, Qt::NoModifier);
    QCoreApplication::sendEvent(&view, &up);
    sim.advance(1.0 / 60.0);

    // And Escape still belongs to the view: it is how a full-screen run is
    // left, so the scene must not swallow it.
    QSignalSpy leaving(&view, &FullScreenView::closeRequested);
    QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QCoreApplication::sendEvent(&view, &escape);
    EXPECT_EQ(leaving.count(), 1) << "Escape no longer leaves a full-screen run";
    EXPECT_EQ(hits(), 1);

    sim.stop();
}

// In a full-screen run Escape is the only key the view keeps. Space must not
// hold the run, the arrows must not step it or nudge anything, and all of them
// must reach the rules -- a scene is free to bind every one of them.
TEST(RuleSource, OnlyEscapeIsTheViewsInAFullScreenRun)
{
    CanvasScene scene;
    scene.setSimulationEngineName(QStringLiteral("Box2D"));
    scene.setPixelsPerMeter(physics::kReferencePixelsPerMeter);
    scene.setEditorMode(EditorMode::Physics);

    auto *box = new RectangleItem;
    box->setRect(QRectF(0, 0, 40, 40));
    box->setPos(0, 0);
    box->setName(QStringLiteral("box"));
    scene.addItem(box);
    scene.notifyShapesChanged();
    scene.selectForPhysics(box, true);
    PhysicsBody *body = scene.createBodyFromSelection();
    ASSERT_TRUE(body);
    // Static, so anything that moves it moved it on purpose: a dynamic one
    // falls, and falling is not nudging.
    body->props().type = physics::BodyType::Static;

    scene.setVariables({ SceneVariable { QStringLiteral("hits"), SceneVariable::Type::Integer,
                                         0 } });
    QVector<Rule> rules;
    for (const char *key : { "Space", "Left", "Right", "Up", "Down" }) {
        Rule rule;
        rule.conditions[0].subjectName = Rule::keyboard();
        rule.conditions[0].eventId = Rule::keyPressedEvent();
        rule.conditions[0].conditionValue = QString::fromLatin1(key);
        rule.actions[0].targetName = Rule::variables();
        rule.actions[0].propertyKey = QStringLiteral("hits");
        rule.actions[0].op = Rule::Op::Add;
        rule.actions[0].value = 1;
        rules.append(rule);
    }
    scene.setRules(rules);

    SimulationController sim(&scene, nullptr);
    sim.setEngineName(QStringLiteral("Box2D"));
    sim.start();
    sim.advance(1.0 / 60.0);

    FullScreenView view(&scene, nullptr);
    view.resize(640, 480);
    QSignalSpy leaving(&view, &FullScreenView::closeRequested);

    const QPointF stood = box->pos();
    int expected = 0;
    for (const int key : { int(Qt::Key_Space), int(Qt::Key_Left), int(Qt::Key_Right),
                           int(Qt::Key_Up), int(Qt::Key_Down) }) {
        QKeyEvent down(QEvent::KeyPress, key, Qt::NoModifier);
        QCoreApplication::sendEvent(&view, &down);
        sim.advance(1.0 / 60.0);
        QKeyEvent up(QEvent::KeyRelease, key, Qt::NoModifier);
        QCoreApplication::sendEvent(&view, &up);
        sim.advance(1.0 / 60.0);
        ++expected;
        EXPECT_EQ(sim.readValue(Rule::variables(), QStringLiteral("hits")).toInt(), expected)
            << "key " << key << " never reached the rules";
        EXPECT_TRUE(sim.isRunning()) << "key " << key << " stopped or held the run";
        EXPECT_EQ(leaving.count(), 0) << "key " << key << " asked to leave the run";
    }
    EXPECT_EQ(box->pos(), stood) << "an arrow nudged a shape during the run";

    // And Escape is still the way out.
    QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QCoreApplication::sendEvent(&view, &escape);
    EXPECT_EQ(leaving.count(), 1) << "Escape no longer leaves a full-screen run";

    sim.stop();
}

// What a control says is its own property: a rule changes the words on a
// button without changing what every other rule calls it. Run state, like its
// value -- the name comes back when the run stops.
TEST(RuleSource, ARuleChangesWhatAControlSays)
{
    CanvasScene scene;
    scene.setSimulationEngineName(QStringLiteral("Box2D"));
    scene.setPixelsPerMeter(physics::kReferencePixelsPerMeter);
    scene.setEditorMode(EditorMode::Physics);

    auto *box = new RectangleItem;
    box->setRect(QRectF(0, 0, 40, 40));
    box->setName(QStringLiteral("box"));
    scene.addItem(box);
    scene.notifyShapesChanged();
    scene.selectForPhysics(box, true);
    ASSERT_TRUE(scene.createBodyFromSelection());

    SceneControl press;
    press.id = QStringLiteral("fire");
    press.kind = SceneControl::Kind::Button;
    scene.setControls({ press });
    EXPECT_EQ(scene.controlCaption(QStringLiteral("fire")), QStringLiteral("fire"))
        << "a control says its name until something says otherwise";

    Rule rename;
    rename.conditions[0].subjectName = Rule::keyboard();
    rename.conditions[0].eventId = Rule::keyPressedEvent();
    rename.conditions[0].conditionValue = QStringLiteral("A");
    rename.actions[0].targetName = Rule::controls();
    rename.actions[0].propertyKey = QStringLiteral("fire:caption");
    rename.actions[0].op = Rule::Op::Set;
    rename.actions[0].value = QStringLiteral("Reload");
    scene.setRules({ rename });

    SimulationController sim(&scene, nullptr);
    sim.setEngineName(QStringLiteral("Box2D"));
    sim.start();
    sim.advance(1.0 / 60.0);

    QKeyEvent down(QEvent::KeyPress, Qt::Key_A, Qt::NoModifier);
    QCoreApplication::sendEvent(&scene, &down);
    sim.advance(1.0 / 60.0);
    EXPECT_EQ(scene.controlCaption(QStringLiteral("fire")), QStringLiteral("Reload"))
        << "the rule did not change what the button says";
    EXPECT_EQ(sim.readValue(Rule::controls(), QStringLiteral("fire:caption")).toString(),
              QStringLiteral("Reload"))
        << "a rule cannot read back what it wrote";
    // The name is untouched: every other rule still calls it "fire".
    ASSERT_EQ(scene.controls().size(), 1);
    EXPECT_EQ(scene.controls().first().id, QStringLiteral("fire"));

    sim.stop();
    EXPECT_EQ(scene.controlCaption(QStringLiteral("fire")), QStringLiteral("fire"))
        << "the caption outlived the run";
}
