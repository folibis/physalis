// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ruslan Muhlinin. See LICENSE.
#include "CanvasScene.h"
#include "EngineRegistry.h"
#include "IPhysicsEngine.h"
#include "PhysicsBody.h"
#include "RectangleItem.h"
#include "ShapeItem.h"
#include "SimulationController.h"

#include <QSet>
#include <cmath>
#include <gtest/gtest.h>

// A property in the table is a promise that setting it does something. Storing
// it and handing it back is not that promise -- Warm Starting was stored and
// handed back by the panel for months while the world was built without it.
// So: set each one to something other than its default, build the world, and
// ask the *engine* what it holds. Not the scene, not the table: the engine.
//
// Everything a catalogue publishes as stored is checked here or named in
// kUnreadable below with the reason it cannot be, so a property added to a
// catalogue and wired to nothing fails this test on the day it is added.

namespace {

// Stored, but the engine cannot be asked what it did with it. Each one needs a
// test of its own that proves it by what the scene does, named here.
const QHash<QString, const char *> kUnreadable {
    // An argument to every b2World_Step rather than a field of the world.
    { QStringLiteral("subStepCount"), "worldtune.cpp" },
    // Set through Box2D's length unit before the world exists.
    { QStringLiteral("contactMargin"), "contactmargin.cpp" },
    // b2Body_SetAllowFastRotation has no getter in Box2D 3.1.
    { QStringLiteral("allowFastRotation"), "spinfast.cpp" },
};

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
    // std::round, not qRound: qRound gives an int, and a collision mask's
    // default is every bit of a 64-bit word set -- which wraps.
    if (property.decimals == 0)
        wanted = std::round(wanted);
    if (qFuzzyCompare(wanted, was))
        wanted = qBound(property.minValue, was + qMax(property.step, 1.0), property.maxValue);
    if (qFuzzyCompare(wanted, was))
        return {};
    return wanted;
}

// One dynamic box, at the scale the engines treat as 1:1 so that a value which
// is scaled on the way in comes back as itself.
struct OneBox {
    CanvasScene scene;
    ShapeItem *shape = nullptr;
    PhysicsBody *body = nullptr;

    explicit OneBox(const QString &engineName)
    {
        scene.setSimulationEngineName(engineName);
        scene.setPixelsPerMeter(physics::kReferencePixelsPerMeter);
        scene.setEditorMode(EditorMode::Physics);
        shape = scene.addRectangle(QPointF(0, 0));
        shape->setRect(QRectF(0, 0, 60, 40));
        shape->setName(QStringLiteral("box"));
        scene.notifyShapesChanged();
        scene.selectForPhysics(shape);
        body = scene.createBodyFromSelection();
        body->setName(QStringLiteral("boxBody"));
        scene.clearPhysicsSelection();
    }
};

enum class On { Body, Shape };

// Sets one property, builds the world, and gives back what the engine holds.
QVariant throughTheEngine(const QString &engineName, On where,
                          const QString &key, const QVariant &value)
{
    OneBox bench(engineName);
    if (where == On::Body)
        bench.body->props().params[key] = value;
    else
        bench.shape->part().params[key] = value;

    SimulationController sim(&bench.scene, nullptr);
    sim.setEngineName(engineName);
    sim.start();
    const QString name = where == On::Body ? bench.body->name() : bench.shape->name();
    const QVariant got = sim.readValue(name, key);
    sim.stop();
    return got;
}

void everyStoredPropertyArrives(const QString &engineName, On where)
{
    auto engine = physics::EngineRegistry::create(engineName);
    ASSERT_TRUE(engine) << engineName.toStdString() << " is not installed";
    const physics::PropertyList list =
        where == On::Body ? engine->bodyProperties() : engine->shapeProperties();

    int checked = 0;
    for (const physics::JointParam &property : list) {
        if (!property.stored)
            continue;
        const std::string what = (engineName + QLatin1String(": ") + property.key).toStdString();

        if (!property.liveReadable) {
            EXPECT_TRUE(kUnreadable.contains(property.key))
                << what << " is stored, cannot be read back, and nothing here says"
                           " which test proves it does anything";
            continue;
        }
        const QVariant wanted = somethingElse(property);
        if (!wanted.isValid())
            continue;

        const QVariant got = throughTheEngine(engineName, where, property.key, wanted);
        ++checked;
        ASSERT_TRUE(got.isValid()) << what << ": the engine answers nothing for it";
        if (property.type == physics::ParamType::Bool) {
            EXPECT_EQ(got.toBool(), wanted.toBool())
                << what << ": the table sets it, the engine was built without it";
        } else if (property.type == physics::ParamType::Choice) {
            EXPECT_EQ(got.toInt(), wanted.toInt())
                << what << ": the table sets it, the engine was built without it";
        } else {
            EXPECT_NEAR(got.toDouble(), wanted.toDouble(), 1e-3)
                << what << ": the table sets it, the engine was built without it";
        }
    }
    EXPECT_GT(checked, 0) << "nothing was checked at all";
}

} // namespace

TEST(StoredProperties, Box2DBuildsTheBodyFromWhatTheTableSet)
{
    everyStoredPropertyArrives(QStringLiteral("Box2D"), On::Body);
}

TEST(StoredProperties, Box2DBuildsTheShapeFromWhatTheTableSet)
{
    everyStoredPropertyArrives(QStringLiteral("Box2D"), On::Shape);
}

TEST(StoredProperties, ChipmunkBuildsTheBodyFromWhatTheTableSet)
{
    everyStoredPropertyArrives(QStringLiteral("Chipmunk2D"), On::Body);
}

TEST(StoredProperties, ChipmunkBuildsTheShapeFromWhatTheTableSet)
{
    everyStoredPropertyArrives(QStringLiteral("Chipmunk2D"), On::Shape);
}

// The exemption list is not a place to hide a property: every name in it has
// to be a property some engine really publishes and really cannot answer for.
TEST(StoredProperties, TheExemptionListIsHonest)
{
    QSet<QString> unreadable;
    for (const char *engineName : { "Box2D", "Chipmunk2D" }) {
        auto engine = physics::EngineRegistry::create(QString::fromLatin1(engineName));
        ASSERT_TRUE(engine);
        physics::PropertyList all = engine->bodyProperties();
        all += engine->shapeProperties();
        all += engine->worldProperties();
        for (const physics::JointParam &property : all) {
            if (property.stored && !property.liveReadable)
                unreadable.insert(property.key);
        }
    }
    for (auto it = kUnreadable.cbegin(); it != kUnreadable.cend(); ++it) {
        EXPECT_TRUE(unreadable.contains(it.key()))
            << it.key().toStdString() << " is exempted from being checked, and no engine"
                                        " publishes it as a stored property that cannot be read";
    }
    for (const QString &key : unreadable) {
        EXPECT_TRUE(kUnreadable.contains(key))
            << key.toStdString() << " is stored, cannot be read back, and is not in the list";
    }
}
