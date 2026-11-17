// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ruslan Muhlinin. See LICENSE.
#include "CanvasScene.h"
#include "EngineRegistry.h"
#include "IPhysicsEngine.h"
#include "Joint.h"
#include "PhysicsBody.h"
#include "PropertyPane/JointPropertyPane.h"
#include "RectangleItem.h"
#include "SceneSerializer.h"
#include "ShapeItem.h"
#include "SimulationController.h"
#include "RunCopy.h"

#include <QHash>
#include <QJsonObject>
#include <QSet>
#include <cmath>
#include <gtest/gtest.h>

// Every joint type both engines publish, every parameter each type declares,
// every reading it offers -- rather than the two or three types somebody had
// trouble with. A type appears in the toolbar because its engine published it,
// so publishing it is a promise that it can be made, that its settings reach
// the solver, and that what it says it measures can be read.

namespace {

QVariant somethingElse(const physics::JointParam &property)
{
    if (property.type == physics::ParamType::Bool)
        return !property.defaultValue.toBool();
    if (property.type == physics::ParamType::Choice) {
        if (property.choices.size() < 2)
            return {};
        return property.defaultValue.toInt() == 0 ? 1 : 0;
    }
    const qreal was = property.defaultValue.toDouble();
    qreal wanted = was != 0.0 ? was / 2.0 : 1.0;
    wanted = qBound(property.minValue, wanted, property.maxValue);
    if (property.decimals == 0)
        wanted = std::round(wanted);
    if (qFuzzyCompare(wanted, was))
        wanted = qBound(property.minValue, was + qMax(property.step, 1.0), property.maxValue);
    if (qFuzzyCompare(wanted, was))
        return {};
    return wanted;
}

// Two boxes, one static, one hanging below it -- enough for any joint either
// engine offers, at the scale the engines treat as 1:1.
struct TwoBodies {
    CanvasScene scene;
    PhysicsBody *anchor = nullptr;
    PhysicsBody *hanging = nullptr;

    explicit TwoBodies(const QString &engineName)
    {
        scene.setSimulationEngineName(engineName);
        scene.setPixelsPerMeter(physics::kReferencePixelsPerMeter);
        scene.setEditorMode(EditorMode::Physics);

        ShapeItem *top = scene.addRectangle(QPointF(0, -100));
        top->setRect(QRectF(0, 0, 40, 40));
        top->setName(QStringLiteral("top"));
        ShapeItem *bottom = scene.addRectangle(QPointF(0, 0));
        bottom->setRect(QRectF(0, 0, 40, 40));
        bottom->setName(QStringLiteral("bottom"));
        scene.notifyShapesChanged();

        scene.selectForPhysics(top, true);
        anchor = scene.createBodyFromSelection();
        anchor->props().type = physics::BodyType::Static;
        anchor->setName(QStringLiteral("anchorBody"));
        scene.clearPhysicsSelection();
        scene.selectForPhysics(bottom, true);
        hanging = scene.createBodyFromSelection();
        hanging->setName(QStringLiteral("hangingBody"));
        scene.clearPhysicsSelection();
    }

    Joint *join(const physics::JointType &type, const QVariantMap &params = {})
    {
        return scene.createJoint(type.id, anchor, type.bodyCount == 1 ? nullptr : hanging,
                                 type.anchorCount, params);
    }
};

} // namespace

// Every published type can be made: the engine takes it, and the run does not
// list it among the ones it had to skip.
void everyTypeCanBeMade(const QString &engineName)
{
    auto engine = physics::EngineRegistry::create(engineName);
    ASSERT_TRUE(engine) << engineName.toStdString() << " is not installed";
    const QVector<physics::JointType> types = engine->jointTypes();
    ASSERT_FALSE(types.isEmpty());

    for (const physics::JointType &type : types) {
        TwoBodies bench(engineName);
        Joint *joint = bench.join(type);
        const std::string what = (engineName + QLatin1String(": ") + type.id).toStdString();
        ASSERT_TRUE(joint) << what << " could not be made in the scene at all";

        SimulationController sim(&bench.scene, nullptr);
        sim.setEngineName(engineName);
        sim.start();
        EXPECT_TRUE(sim.skippedJoints().isEmpty())
            << what << " is published but the engine would not build it: "
            << sim.skippedJoints().join(QLatin1Char(',')).toStdString();
        for (int i = 0; i < 5; ++i)
            sim.stepFrame();
        EXPECT_TRUE(sim.problems().isEmpty())
            << what << " ran into: " << sim.problems().join(QLatin1Char(',')).toStdString();
        sim.stop();
    }
}

TEST(JointCatalogue, Box2DMakesEveryTypeItPublishes)
{
    everyTypeCanBeMade(QStringLiteral("Box2D"));
}

TEST(JointCatalogue, ChipmunkMakesEveryTypeItPublishes)
{
    everyTypeCanBeMade(QStringLiteral("Chipmunk2D"));
}

// Every parameter a type declares reaches the solver: set it to something
// other than its default, build the world, and ask the engine what it holds.
// A parameter in the panel that the joint is built without is a row that does
// nothing, which is the whole of what this suite is for.
void everyParameterArrives(const QString &engineName)
{
    auto engine = physics::EngineRegistry::create(engineName);
    ASSERT_TRUE(engine) << engineName.toStdString() << " is not installed";

    int checked = 0;
    for (const physics::JointType &type : engine->jointTypes()) {
        for (const physics::JointParam &param : type.params) {
            const QVariant wanted = somethingElse(param);
            if (!wanted.isValid())
                continue;

            TwoBodies bench(engineName);
            QVariantMap params;
            params.insert(param.key, wanted);
            Joint *joint = bench.join(type, params);
            ASSERT_TRUE(joint);

            SimulationController sim(&bench.scene, nullptr);
            sim.setEngineName(engineName);
            sim.start();
            const QVariant got = sim.readValue(joint->name(), param.key);
            sim.stop();

            const std::string what =
                (engineName + QLatin1String(": ") + type.id + QLatin1Char('.') + param.key).toStdString();
            if (!got.isValid())
                continue;   // the engine offers no reading for it; nothing to compare against
            ++checked;
            if (param.type == physics::ParamType::Bool) {
                EXPECT_EQ(got.toBool(), wanted.toBool())
                    << what << ": the panel sets it, the joint is built without it";
            } else if (param.type == physics::ParamType::Choice) {
                EXPECT_EQ(got.toInt(), wanted.toInt())
                    << what << ": the panel sets it, the joint is built without it";
            } else {
                EXPECT_NEAR(got.toDouble(), wanted.toDouble(), 1e-3)
                    << what << ": the panel sets it, the joint is built without it";
            }
        }
    }
    EXPECT_GT(checked, 0) << "no joint parameter could be read back at all";
}

TEST(JointCatalogue, Box2DBuildsJointsFromWhatThePanelSet)
{
    everyParameterArrives(QStringLiteral("Box2D"));
}

TEST(JointCatalogue, ChipmunkBuildsJointsFromWhatThePanelSet)
{
    everyParameterArrives(QStringLiteral("Chipmunk2D"));
}

// Everything a type says it measures answers during a run. A reading offered
// in the rule menus that the engine never answers is a condition that can
// never be true.
void everyReadingAnswers(const QString &engineName)
{
    auto engine = physics::EngineRegistry::create(engineName);
    ASSERT_TRUE(engine) << engineName.toStdString() << " is not installed";

    for (const physics::JointType &type : engine->jointTypes()) {
        TwoBodies bench(engineName);
        Joint *joint = bench.join(type);
        ASSERT_TRUE(joint);

        const physics::PropertyList readings = engine->jointReadables(type.id);

        SimulationController sim(&bench.scene, nullptr);
        sim.setEngineName(engineName);
        sim.start();
        // Read on every step, the way the property table does while a run is
        // going, rather than once at the end: a reading that upsets the solver
        // upsets it once per frame, and reading it once would not show that.
        for (int i = 0; i < 5; ++i) {
            sim.stepFrame();
            for (const physics::JointParam &reading : readings)
                sim.readValue(joint->name(), reading.key);
        }

        for (const physics::JointParam &reading : readings) {
            if (!reading.liveReadable)
                continue;
            const std::string what =
                (engineName + QLatin1String(": ") + type.id + QLatin1Char('.') + reading.key).toStdString();
            EXPECT_TRUE(sim.readValue(joint->name(), reading.key).isValid())
                << what << " is offered as something to measure and the engine answers nothing";
        }
        // Asking what a joint measures is not meant to cost the run anything.
        EXPECT_TRUE(sim.problems().isEmpty())
            << (engineName + QLatin1String(": ") + type.id).toStdString()
            << " reported, while its readings were being polled: "
            << sim.problems().join(QLatin1Char(',')).toStdString();
        sim.stop();
    }
}

// The property table shows each of a joint's keys once. A motor's speed is
// published twice on purpose -- as the setting and as a reading, so a rule can
// ask what it is before negating it -- and the table has to pick one, or the
// joint grows two "Motor Speed" rows that set each other.
void noRowIsShownTwice(const QString &engineName)
{
    auto engine = physics::EngineRegistry::create(engineName);
    ASSERT_TRUE(engine) << engineName.toStdString() << " is not installed";

    for (const physics::JointType &type : engine->jointTypes()) {
        TwoBodies bench(engineName);
        Joint *joint = bench.join(type);
        ASSERT_TRUE(joint);
        bench.scene.selectJoint(joint);

        JointPropertyPane pane;
        pane.attach(&bench.scene);

        QHash<QString, QStringList> shown;
        for (const PropertyRow &row : pane.rows(EditorMode::Physics)) {
            if (row.key.isEmpty())
                continue; // a row the editor owns carries no engine key
            shown[row.key] << (row.group + QLatin1Char('/') + row.label);
        }

        for (auto it = shown.constBegin(); it != shown.constEnd(); ++it) {
            EXPECT_EQ(it.value().size(), 1)
                << engineName.toStdString() << ": " << type.id.toStdString() << '.'
                << it.key().toStdString() << " is in the table as "
                << it.value().join(QLatin1String(" and ")).toStdString();
        }
    }
}

TEST(JointCatalogue, Box2DShowsEachJointRowOnce)
{
    noRowIsShownTwice(QStringLiteral("Box2D"));
}

TEST(JointCatalogue, ChipmunkShowsEachJointRowOnce)
{
    noRowIsShownTwice(QStringLiteral("Chipmunk2D"));
}

TEST(JointCatalogue, Box2DAnswersForEverythingItMeasures)
{
    everyReadingAnswers(QStringLiteral("Box2D"));
}

TEST(JointCatalogue, ChipmunkAnswersForEverythingItMeasures)
{
    everyReadingAnswers(QStringLiteral("Chipmunk2D"));
}

// A joint the engine says is a spring is drawn as one. The editor finds that
// out by role, never by key, so what this really checks is that the roles are
// on the parameters -- an engine that forgets them draws rods for springs and
// nothing anywhere says so.
void springsAreRecognised(const QString &engineName)
{
    auto engine = physics::EngineRegistry::create(engineName);
    ASSERT_TRUE(engine) << engineName.toStdString() << " is not installed";

    int springTypes = 0;
    for (const physics::JointType &type : engine->jointTypes()) {
        QString switchKey;
        QString stiffnessKey;
        for (const physics::JointParam &param : type.params) {
            if (param.role == physics::PropertyRole::SpringEnabled)
                switchKey = param.key;
            else if (param.role == physics::PropertyRole::SpringStiffness
                     && stiffnessKey.isEmpty())
                stiffnessKey = param.key;
        }
        if (switchKey.isEmpty() && stiffnessKey.isEmpty())
            continue;
        ++springTypes;

        const std::string what = (engineName + QLatin1String(": ") + type.id).toStdString();
        TwoBodies bench(engineName);

        QVariantMap on;
        if (!switchKey.isEmpty())
            on.insert(switchKey, true);
        else
            on.insert(stiffnessKey, 5.0);
        Joint *springing = bench.join(type, on);
        ASSERT_TRUE(springing) << what;
        EXPECT_TRUE(bench.scene.isSpringJoint(springing))
            << what << " marks a spring parameter, and the canvas does not see a spring";

        // And off again, where the engine has a switch to turn off.
        if (!switchKey.isEmpty()) {
            TwoBodies plain(engineName);
            QVariantMap off;
            off.insert(switchKey, false);
            Joint *rod = plain.join(type, off);
            ASSERT_TRUE(rod) << what;
            EXPECT_FALSE(plain.scene.isSpringJoint(rod))
                << what << " has its spring switched off and is still drawn as one";
        }
    }

    EXPECT_GT(springTypes, 0)
        << engineName.toStdString() << " publishes no spring at all, which cannot be right";
}

TEST(JointCatalogue, Box2DSpringsAreDrawnAsSprings)
{
    springsAreRecognised(QStringLiteral("Box2D"));
}

TEST(JointCatalogue, ChipmunkSpringsAreDrawnAsSprings)
{
    springsAreRecognised(QStringLiteral("Chipmunk2D"));
}

// A joint of every type survives the file with its settings.
void everyTypeSurvivesTheFile(const QString &engineName)
{
    auto engine = physics::EngineRegistry::create(engineName);
    ASSERT_TRUE(engine) << engineName.toStdString() << " is not installed";

    for (const physics::JointType &type : engine->jointTypes()) {
        TwoBodies bench(engineName);
        QVariantMap params;
        for (const physics::JointParam &param : type.params) {
            const QVariant wanted = somethingElse(param);
            if (wanted.isValid())
                params.insert(param.key, wanted);
        }
        Joint *joint = bench.join(type, params);
        ASSERT_TRUE(joint);
        const QString name = joint->name();

        const QJsonObject document = SceneSerializer::save(&bench.scene);
        CanvasScene reopened;
        QString error;
        ASSERT_TRUE(SceneSerializer::load(&reopened, document, &error)) << error.toStdString();
        ASSERT_EQ(reopened.joints().size(), 1) << type.id.toStdString() << " did not survive the file";

        Joint *loaded = reopened.joints().first();
        const std::string what = (engineName + QLatin1String(": ") + type.id).toStdString();
        EXPECT_EQ(loaded->typeId(), type.id) << what << " came back as a different type";
        EXPECT_EQ(loaded->name(), name);
        for (auto it = params.cbegin(); it != params.cend(); ++it) {
            EXPECT_TRUE(loaded->params().contains(it.key()))
                << what << ": " << it.key().toStdString() << " did not survive the file";
        }
    }
}

TEST(JointCatalogue, Box2DJointsSurviveTheFile)
{
    everyTypeSurvivesTheFile(QStringLiteral("Box2D"));
}

TEST(JointCatalogue, ChipmunkJointsSurviveTheFile)
{
    everyTypeSurvivesTheFile(QStringLiteral("Chipmunk2D"));
}

// The editor can only draw what it can ask for by role. It draws a spring
// because three parameters are tagged with the spring roles; the travel limits
// were in every catalogue under the engine's own names and tagged with
// nothing, so the canvas could not see them and drew none.
void everyLimitIsTagged(const QString &engineName)
{
    auto engine = physics::EngineRegistry::create(engineName);
    ASSERT_TRUE(engine) << engineName.toStdString() << " is not installed";

    int withLimits = 0;
    for (const physics::JointType &type : engine->jointTypes()) {
        QStringList lower;
        QStringList upper;
        for (const physics::JointParam &param : type.params) {
            if (param.role == physics::PropertyRole::LowerLimit)
                lower << param.key;
            else if (param.role == physics::PropertyRole::UpperLimit)
                upper << param.key;
        }
        // Either it has a pair or it has neither: one half of a limit is
        // nothing the editor can draw.
        EXPECT_EQ(lower.size(), upper.size())
            << type.id.toStdString() << " tags " << lower.size() << " lower limits and "
            << upper.size() << " upper";
        EXPECT_LE(lower.size(), 1) << type.id.toStdString() << " tags more than one lower limit";
        if (!lower.isEmpty())
            ++withLimits;
    }
    EXPECT_GT(withLimits, 0) << engineName.toStdString()
                             << " publishes no joint whose travel is limited";
}

TEST(JointCatalogue, Box2DTagsItsTravelLimits)
{
    everyLimitIsTagged(QStringLiteral("Box2D"));
}

TEST(JointCatalogue, ChipmunkTagsItsTravelLimits)
{
    everyLimitIsTagged(QStringLiteral("Chipmunk2D"));
}

// A lower stop above its upper one is a contradiction, not a narrow limit: an
// engine handed one either locks the joint solid or ignores the pair, and the
// scene stops behaving with nothing to say why. Typing one past the other
// pushes the other along, for every joint that has a pair, found by role.
void limitsStayInOrder(const QString &engineName)
{
    auto engine = physics::EngineRegistry::create(engineName);
    ASSERT_TRUE(engine) << engineName.toStdString() << " is not installed";

    int checked = 0;
    for (const physics::JointType &type : engine->jointTypes()) {
        QString lowerKey;
        QString upperKey;
        for (const physics::JointParam &param : type.params) {
            if (param.role == physics::PropertyRole::LowerLimit)
                lowerKey = param.key;
            else if (param.role == physics::PropertyRole::UpperLimit)
                upperKey = param.key;
        }
        if (lowerKey.isEmpty() || upperKey.isEmpty())
            continue;
        ++checked;

        TwoBodies bench(engineName);
        Joint *joint = bench.join(type);
        ASSERT_TRUE(joint);
        bench.scene.selectJoint(joint);

        JointPropertyPane pane;
        pane.attach(&bench.scene);

        const PropertyRow *lower = nullptr;
        const PropertyRow *upper = nullptr;
        const std::vector<PropertyRow> rows = pane.rows(EditorMode::Physics);
        for (const PropertyRow &row : rows) {
            if (row.key == lowerKey)
                lower = &row;
            else if (row.key == upperKey)
                upper = &row;
        }
        ASSERT_TRUE(lower) << type.id.toStdString() << " has no lower-limit row";
        ASSERT_TRUE(upper) << type.id.toStdString() << " has no upper-limit row";

        // Settle the pair somewhere sane, then push the lower past the upper.
        lower->setter(-50.0);
        upper->setter(50.0);
        lower->setter(80.0);
        EXPECT_GE(upper->getter().toDouble(), lower->getter().toDouble())
            << engineName.toStdString() << ": " << type.id.toStdString()
            << " kept a lower stop above its upper one";

        // And the other way about.
        lower->setter(-50.0);
        upper->setter(50.0);
        upper->setter(-90.0);
        EXPECT_LE(lower->getter().toDouble(), upper->getter().toDouble())
            << engineName.toStdString() << ": " << type.id.toStdString()
            << " kept an upper stop below its lower one";
    }
    EXPECT_GT(checked, 0) << engineName.toStdString() << " publishes no limited joint";
}

TEST(JointCatalogue, Box2DKeepsItsLimitsInOrder)
{
    limitsStayInOrder(QStringLiteral("Box2D"));
}

TEST(JointCatalogue, ChipmunkKeepsItsLimitsInOrder)
{
    limitsStayInOrder(QStringLiteral("Chipmunk2D"));
}

// One thing is picked at a time. A joint used to let go of nothing, so the
// body under it stayed picked and the Properties tab was showing one while the
// Remove button pointed at the other.
TEST(JointCatalogue, PickingOneThingLetsTheOthersGo)
{
    auto engine = physics::EngineRegistry::create(QStringLiteral("Box2D"));
    ASSERT_TRUE(engine);
    ASSERT_FALSE(engine->jointTypes().isEmpty());

    TwoBodies bench(QStringLiteral("Box2D"));
    Joint *joint = bench.join(engine->jointTypes().first());
    ASSERT_TRUE(joint);

    bench.scene.selectForPhysics(bench.hanging->shapes().first(), true);
    ASSERT_FALSE(bench.scene.physicsSelection().isEmpty());

    bench.scene.selectJoint(joint);
    EXPECT_TRUE(bench.scene.physicsSelection().isEmpty())
        << "the body stayed picked behind the joint";

    bench.scene.selectForPhysics(bench.hanging->shapes().first(), true);
    EXPECT_EQ(bench.scene.selectedJoint(), nullptr)
        << "the joint stayed picked behind the body";
}

// Where a sliding joint's rail is drawn has now been got wrong in both
// directions, so what this pins is the one thing that settles it: the rail has
// to end where the body can actually get to.
//
// A sliding joint's limits are offsets from where the joint starts -- the gap
// between its two anchors along the axis. The engine adds that gap to both of
// them when it builds the joint (`travelOrigin` in Box2DJoints.cpp), so the
// travel runs from anchor B's resting place, not from anchor A. Drawn from
// anchor A the rail falls short by the whole gap: crane.phys has 120 scene
// units of it on the trolley and 40 on the hoist, which ran its rails off the
// end of the crane.
//
// The rail does move with body B while the scene is being edited, and that is
// correct rather than a bug: the engine works the gap out afresh when the run
// starts, so where B stands now is what the next run will allow. What must not
// happen is the rail sliding after B *during* a run -- then the stops chase the
// very thing they are limiting and never show where in its travel it has got
// to -- which is why the origin is held in body A's frame for the length of one.
TEST(JointCatalogue, ASlidingJointsRailIsDrawnWhereTheTravelIs)
{
    auto engine = physics::EngineRegistry::create(QStringLiteral("Box2D"));
    ASSERT_TRUE(engine);

    QString lowerKey;
    QString upperKey;
    QString limitSwitch;
    const QVector<physics::JointType> types = engine->jointTypes();
    const physics::JointType *sliding = nullptr;
    for (const physics::JointType &type : types) {
        // Two anchors, or there is no gap and the two readings of a limit
        // coincide -- which is the case that cannot tell them apart.
        if (type.visual != physics::JointVisual::Axis || type.anchorCount < 2)
            continue;
        QString lower;
        QString upper;
        QString enabled;
        for (const physics::JointParam &param : type.params) {
            if (param.role == physics::PropertyRole::LowerLimit)
                lower = param.key;
            else if (param.role == physics::PropertyRole::UpperLimit)
                upper = param.key;
            else if (param.role == physics::PropertyRole::LimitEnabled)
                enabled = param.key;
        }
        if (lower.isEmpty() || upper.isEmpty())
            continue;
        sliding = &type;
        lowerKey = lower;
        upperKey = upper;
        limitSwitch = enabled;
        break;
    }
    ASSERT_NE(sliding, nullptr) << "the engine publishes no limited two-anchor sliding joint";

    constexpr qreal kTravel = 60.0;
    QVariantMap params;
    if (!limitSwitch.isEmpty())
        params.insert(limitSwitch, true);
    params.insert(lowerKey, 0.0);
    params.insert(upperKey, kTravel);
    // Nothing holding it anywhere but gravity, so the far stop is the only
    // place it can end up.
    for (const physics::JointParam &param : sliding->params) {
        if (param.role == physics::PropertyRole::SpringEnabled)
            params.insert(param.key, false);
    }

    TwoBodies bench(QStringLiteral("Box2D"));
    bench.scene.world().params[QStringLiteral("gravityY")] = 9.81;
    Joint *joint = bench.join(*sliding, params);
    ASSERT_TRUE(joint);

    // createJoint puts the anchors on the two centres and points the axis down
    // the span between them, so the gap lies along the axis.
    const QPointF along = joint->axisScene();
    const QPointF anchorA = joint->anchorScenePos(Joint::End::A);
    const QPointF restingB = joint->anchorScenePos(Joint::End::B);
    const QPointF gap = restingB - anchorA;
    const qreal travelOrigin = gap.x() * along.x() + gap.y() * along.y();
    ASSERT_GT(std::abs(travelOrigin), 1.0) << "the anchors coincide, so this proves nothing";

    EXPECT_EQ(bench.scene.railOf(joint).zero, restingB)
        << "zero is where the joint stands at rest, not anchor A -- the engine adds the "
           "anchor gap to both limits";

    // While editing, the rail goes where body B goes: that gap is worked out
    // again when the run starts, so this is what the next run will allow.
    bench.hanging->shapes().first()->moveBy(0, 17);
    EXPECT_EQ(bench.scene.railOf(joint).zero, joint->anchorScenePos(Joint::End::B))
        << "the rail did not follow body B while editing, so it is showing travel the next "
           "run will not have";
    bench.hanging->shapes().first()->moveBy(0, -17);

    const QPointF zero = bench.scene.railOf(joint).zero;
    const QPointF railEnd = zero + along * kTravel;

    SimulationController sim(&bench.scene, nullptr);
    sim.setEngineName(QStringLiteral("Box2D"));
    sim.start();
    ASSERT_TRUE(sim.skippedJoints().isEmpty()) << "the joint was skipped by the engine";

    CanvasScene *run = sim.runScene();
    Joint *running = inRun(sim, joint);
    ASSERT_TRUE(run && running);
    for (int i = 0; i < 30; ++i)
        sim.stepFrame();
    // Held for the length of the run, so the stops do not chase the body.
    EXPECT_EQ(run->railOf(running).zero, zero)
        << "the rail slid after body B during the run";
    EXPECT_NE(running->anchorScenePos(Joint::End::B), zero)
        << "body B never moved, so the line above proved nothing";

    for (int i = 0; i < 400; ++i)
        sim.stepFrame();

    const QPointF ended = running->anchorScenePos(Joint::End::B);
    // Generous on purpose: what is being checked is which end of the rail it
    // is at, and the two candidate answers are a whole anchor gap apart.
    EXPECT_NEAR(ended.y(), railEnd.y(), 4.0)
        << "it came to rest " << (ended.y() - railEnd.y())
        << " scene units from where the rail is drawn to end; the anchor gap is "
        << travelOrigin << ", so being out by about that much means the rail is drawn from "
        << "the wrong anchor";
    EXPECT_NEAR(ended.x(), railEnd.x(), 4.0);
    sim.stop();
}

// The same joint built the other way round: body A the one that moves, body B
// the one bolted down. domino.phys's lifts are built that way, and the rail
// used to be pinned to body A's frame whichever body that was -- so as the
// lift rose its own limit line rose with it, 425 scene units up and off the
// top of the scene, which is the one thing a limit line must never do.
//
// The rail belongs to whichever end holds still. Nothing here looks at which
// that is: it runs the joint and requires the rail to be where it started.
TEST(JointCatalogue, ASlidingJointsRailHoldsStillWhicheverEndMoves)
{
    auto engine = physics::EngineRegistry::create(QStringLiteral("Box2D"));
    ASSERT_TRUE(engine);

    const QVector<physics::JointType> types = engine->jointTypes();
    QString lowerKey;
    QString upperKey;
    QString limitSwitch;
    const physics::JointType *sliding = nullptr;
    for (const physics::JointType &type : types) {
        if (type.visual != physics::JointVisual::Axis || type.anchorCount < 2)
            continue;
        QString lower;
        QString upper;
        QString enabled;
        for (const physics::JointParam &param : type.params) {
            if (param.role == physics::PropertyRole::LowerLimit)
                lower = param.key;
            else if (param.role == physics::PropertyRole::UpperLimit)
                upper = param.key;
            else if (param.role == physics::PropertyRole::LimitEnabled)
                enabled = param.key;
        }
        if (lower.isEmpty() || upper.isEmpty())
            continue;
        sliding = &type;
        lowerKey = lower;
        upperKey = upper;
        limitSwitch = enabled;
        break;
    }
    ASSERT_NE(sliding, nullptr);

    QVariantMap params;
    if (!limitSwitch.isEmpty())
        params.insert(limitSwitch, true);
    params.insert(lowerKey, 0.0);
    params.insert(upperKey, 60.0);
    for (const physics::JointParam &param : sliding->params) {
        if (param.role == physics::PropertyRole::SpringEnabled)
            params.insert(param.key, false);
    }

    TwoBodies bench(QStringLiteral("Box2D"));
    bench.scene.world().params[QStringLiteral("gravityY")] = 9.81;
    // hanging is the dynamic one, so this is a joint whose body A moves and
    // whose body B is bolted down -- the way round the bench's join() cannot
    // make.
    Joint *joint = bench.scene.createJoint(sliding->id, bench.hanging, bench.anchor,
                                           sliding->anchorCount, params);
    ASSERT_TRUE(joint);
    ASSERT_EQ(bench.hanging->props().type, physics::BodyType::Dynamic);
    ASSERT_EQ(bench.anchor->props().type, physics::BodyType::Static);

    const QPointF zero = bench.scene.railOf(joint).zero;
    const QPointF movingAnchorWas = joint->anchorScenePos(Joint::End::A);

    SimulationController sim(&bench.scene, nullptr);
    sim.setEngineName(QStringLiteral("Box2D"));
    sim.start();
    ASSERT_TRUE(sim.skippedJoints().isEmpty()) << "the joint was skipped by the engine";
    for (int i = 0; i < 400; ++i)
        sim.stepFrame();

    Joint *running = inRun(sim, joint);
    ASSERT_TRUE(running);
    const QPointF moved = running->anchorScenePos(Joint::End::A);
    ASSERT_GT(QLineF(movingAnchorWas, moved).length(), 5.0)
        << "body A never travelled, so there was nothing for the rail to follow";

    const QPointF now = sim.runScene()->railOf(running).zero;
    EXPECT_NEAR(now.x(), zero.x(), 0.01)
        << "the rail travelled with body A instead of staying where the travel is";
    EXPECT_NEAR(now.y(), zero.y(), 0.01)
        << "the rail moved " << (now.y() - zero.y()) << " scene units while body A moved "
        << (moved.y() - movingAnchorWas.y());
    sim.stop();
}
