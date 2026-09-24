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

#include <QJsonArray>
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

