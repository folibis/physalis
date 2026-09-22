// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ruslan Muhlinin. See LICENSE.
#include "EngineRegistry.h"
#include "IPhysicsEngine.h"

#include <gtest/gtest.h>

// Every other test here is a scenario -- drop a ball, fire a rule, save and
// load -- so a world property is only ever checked if somebody thought to
// check it. That leaves one thing unasked: whether a property the engine
// *publishes* as the scene's to keep is wired to anything at all. Warm Starting
// and Speculative Contacts were published, stored nowhere and applied nowhere,
// and the whole suite passed. This walks the catalogue instead of a scenario:
// set each one to something other than its default, build the world, and ask
// the engine what it got.

namespace {

// Something other than the default, inside whatever range the property gives.
QVariant somethingElse(const physics::JointParam &property)
{
    switch (property.type) {
    case physics::ParamType::Bool:
        return !property.defaultValue.toBool();
    case physics::ParamType::Choice: {
        const int was = property.defaultValue.toInt();
        if (property.choices.size() < 2)
            return {};
        return was == 0 ? 1 : 0;
    }
    case physics::ParamType::Integer:
    case physics::ParamType::Real: {
        const qreal was = property.defaultValue.toDouble();
        // Half of it, or a step up from zero -- whichever stays in range and
        // is not what it already was.
        qreal wanted = was != 0.0 ? was / 2.0 : qMax(property.minValue, 1.0);
        wanted = qBound(property.minValue, wanted, property.maxValue);
        if (qFuzzyCompare(wanted, was))
            wanted = qBound(property.minValue, was + qMax(property.step, 1.0), property.maxValue);
        // A whole number is written as a Real with no decimals -- the
        // catalogues never use ParamType::Integer -- and the engine hands it
        // to something that takes a count, so a fraction would not survive the
        // trip and the mismatch would be the test's doing, not the engine's.
        if (property.decimals == 0)
            wanted = qRound(wanted);
        if (qFuzzyCompare(wanted, was))
            return {};
        return wanted;
    }
    }
    return {};
}

void checkEveryStoredProperty(const QString &engineName)
{
    auto engine = physics::EngineRegistry::create(engineName);
    ASSERT_TRUE(engine) << engineName.toStdString() << " is not installed";
    const physics::PropertyList properties = engine->worldProperties();
    ASSERT_FALSE(properties.isEmpty());

    int checked = 0;
    for (const physics::JointParam &property : properties) {
        // Only what the scene keeps, and only what the world will answer for:
        // a value the engine cannot be asked about (the sub-step count, the
        // contact margin) has to be checked by what it does to a scene, which
        // is what the scenario tests are for.
        if (!property.stored || !property.liveReadable)
            continue;
        const QVariant wanted = somethingElse(property);
        if (!wanted.isValid())
            continue;

        physics::WorldDesc desc;
        // The scale the engine treats as 1:1, so a value that is scaled on the
        // way in comes back as itself rather than off by the scene's scale.
        desc.pixelsPerMeter = physics::kReferencePixelsPerMeter;
        desc.params.insert(property.key, wanted);
        engine->createWorld(desc);
        const QVariant got = engine->worldValue(property.key);
        engine->destroyWorld();
        ++checked;

        const std::string where = (engineName + QLatin1String(": ") + property.key).toStdString();
        ASSERT_TRUE(got.isValid()) << where << " is stored but the world does not answer for it";
        if (property.type == physics::ParamType::Bool) {
            EXPECT_EQ(got.toBool(), wanted.toBool())
                << where << " is stored but building the world ignores it";
        } else {
            EXPECT_NEAR(got.toDouble(), wanted.toDouble(), 1e-3)
                << where << " is stored but building the world ignores it";
        }
    }
    EXPECT_GT(checked, 0) << engineName.toStdString() << " published nothing to check";
}

} // namespace

TEST(WorldCatalogue, Box2DAppliesWhatItPublishes)
{
    checkEveryStoredProperty(QStringLiteral("Box2D"));
}

TEST(WorldCatalogue, ChipmunkAppliesWhatItPublishes)
{
    checkEveryStoredProperty(QStringLiteral("Chipmunk2D"));
}
