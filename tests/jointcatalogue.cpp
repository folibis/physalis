// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ruslan Muhlinin. See LICENSE.
#include "CanvasScene.h"
#include "EngineRegistry.h"
#include "IPhysicsEngine.h"
#include "Joint.h"
#include "PhysicsBody.h"
#include "RectangleItem.h"
#include "SceneSerializer.h"
#include "ShapeItem.h"
#include "SimulationController.h"

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

        SimulationController sim(&bench.scene, nullptr);
        sim.setEngineName(engineName);
        sim.start();
        for (int i = 0; i < 5; ++i)
            sim.stepFrame();
        for (const physics::JointParam &reading : engine->jointReadables(type.id)) {
            if (!reading.liveReadable)
                continue;
            const std::string what =
                (engineName + QLatin1String(": ") + type.id + QLatin1Char('.') + reading.key).toStdString();
            EXPECT_TRUE(sim.readValue(joint->name(), reading.key).isValid())
                << what << " is offered as something to measure and the engine answers nothing";
        }
        sim.stop();
    }
}

TEST(JointCatalogue, Box2DAnswersForEverythingItMeasures)
{
    everyReadingAnswers(QStringLiteral("Box2D"));
}

TEST(JointCatalogue, ChipmunkAnswersForEverythingItMeasures)
{
    everyReadingAnswers(QStringLiteral("Chipmunk2D"));
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
