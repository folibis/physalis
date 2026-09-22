// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ruslan Muhlinin. See LICENSE.
#include "CanvasScene.h"
#include "EngineRegistry.h"
#include "IPhysicsEngine.h"
#include "PhysicsBody.h"
#include "PropertyPane/PhysicsPropertyPane.h"
#include "RectangleItem.h"
#include "SceneSerializer.h"
#include "ShapeItem.h"

#include <QJsonObject>
#include <QSet>
#include <gtest/gtest.h>

// The catalogue is a promise: a property published as the scene's to keep gets
// a row in the property table, holds what is typed into it, survives a save,
// and arrives at the engine. Every other test in this suite checks one
// property somebody had trouble with; this checks all of them, for both
// engines, so a property added to a catalogue and wired to nothing fails here
// rather than years later in a scene.

namespace {

struct Bench {
    CanvasScene scene;
    ShapeItem *shape = nullptr;
    PhysicsBody *body = nullptr;
    PhysicsPropertyPane pane;

    explicit Bench(const QString &engineName)
    {
        scene.setSimulationEngineName(engineName);
        shape = scene.addRectangle(QPointF(0, 0));
        shape->setRect(QRectF(0, 0, 60, 40));
        scene.setEditorMode(EditorMode::Physics);
        scene.selectForPhysics(shape);
        body = scene.createBodyFromSelection();
        scene.selectForPhysics(shape);
        pane.attach(&scene);
    }

    std::vector<PropertyRow> rows() const { return pane.rows(EditorMode::Physics); }

    const PropertyRow *row(const QString &key) const
    {
        static std::vector<PropertyRow> kept;
        kept = rows();
        for (const PropertyRow &row : kept) {
            if (row.key == key)
                return &row;
        }
        return nullptr;
    }
};

// A value other than the one the property carries, within its range.
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
        wanted = qRound(wanted);
    if (qFuzzyCompare(wanted, was))
        wanted = qBound(property.minValue, was + qMax(property.step, 1.0), property.maxValue);
    if (qFuzzyCompare(wanted, was))
        return {};
    return wanted;
}

bool sameValue(const QVariant &a, const QVariant &b, const physics::JointParam &property)
{
    if (property.type == physics::ParamType::Bool)
        return a.toBool() == b.toBool();
    if (property.type == physics::ParamType::Choice)
        return a.toInt() == b.toInt();
    return qAbs(a.toDouble() - b.toDouble()) < 1e-3;
}

// What the catalogue says the scene keeps for a body and for a shape.
physics::PropertyList storedOnes(const physics::PropertyList &all)
{
    physics::PropertyList kept;
    for (const physics::JointParam &property : all) {
        if (property.stored)
            kept.append(property);
    }
    return kept;
}

} // namespace

// Every stored property of a body and of a shape has a row, and the row is
// editable: a property the scene keeps that the table never shows can only be
// set by editing the file by hand.
void everyStoredPropertyHasARow(const QString &engineName)
{
    auto engine = physics::EngineRegistry::create(engineName);
    ASSERT_TRUE(engine) << engineName.toStdString() << " is not installed";
    Bench bench(engineName);

    QSet<QString> shown;
    QSet<QString> editable;
    for (const PropertyRow &row : bench.rows()) {
        if (row.key.isEmpty())
            continue;
        shown.insert(row.key);
        if (!row.readOnly)
            editable.insert(row.key);
    }

    for (const physics::PropertyList &list : { engine->bodyProperties(), engine->shapeProperties() }) {
        for (const physics::JointParam &property : storedOnes(list)) {
            const std::string where = (engineName + QLatin1String(": ") + property.key).toStdString();
            EXPECT_TRUE(shown.contains(property.key))
                << where << " is the scene's to keep but the table never shows it";
            EXPECT_TRUE(editable.contains(property.key))
                << where << " is the scene's to keep but the table will not let it be edited";
        }
    }
}

TEST(PropertyCatalogue, Box2DShowsEveryStoredProperty)
{
    everyStoredPropertyHasARow(QStringLiteral("Box2D"));
}

TEST(PropertyCatalogue, ChipmunkShowsEveryStoredProperty)
{
    everyStoredPropertyHasARow(QStringLiteral("Chipmunk2D"));
}

// Typing into each row leaves the value on the object, and saving and loading
// gives it back. A row whose setter writes somewhere the scene does not save
// looks right until the file is reopened.
void everyRowHoldsWhatIsTypedIntoIt(const QString &engineName)
{
    auto engine = physics::EngineRegistry::create(engineName);
    ASSERT_TRUE(engine) << engineName.toStdString() << " is not installed";
    Bench bench(engineName);

    physics::PropertyList all = engine->bodyProperties();
    all += engine->shapeProperties();

    int checked = 0;
    for (const physics::JointParam &property : storedOnes(all)) {
        const PropertyRow *row = bench.row(property.key);
        if (!row || row->readOnly || !row->setter || !row->getter)
            continue;   // reported by the test above; not this one's business
        const QVariant wanted = somethingElse(property);
        if (!wanted.isValid())
            continue;

        const std::string where = (engineName + QLatin1String(": ") + property.key).toStdString();
        row->setter(wanted);
        EXPECT_TRUE(sameValue(bench.row(property.key)->getter(), wanted, property))
            << where << " does not keep what the table put in it";
        ++checked;
    }
    EXPECT_GT(checked, 0) << "nothing was editable at all";

    // And all of it together survives the file.
    const QJsonObject document = SceneSerializer::save(&bench.scene);
    CanvasScene reopened;
    QString error;
    ASSERT_TRUE(SceneSerializer::load(&reopened, document, &error)) << error.toStdString();
    ASSERT_EQ(reopened.bodies().size(), 1);
    ASSERT_EQ(reopened.shapes().size(), 1);

    const QVariantMap savedBody = bench.body->props().params;
    const QVariantMap savedShape = bench.shape->part().params;
    const QVariantMap loadedBody = reopened.bodies().first()->props().params;
    const QVariantMap loadedShape = reopened.shapes().first()->part().params;

    for (auto it = savedBody.cbegin(); it != savedBody.cend(); ++it) {
        EXPECT_TRUE(loadedBody.contains(it.key()))
            << engineName.toStdString() << ": the body's " << it.key().toStdString()
            << " did not survive the file";
    }
    for (auto it = savedShape.cbegin(); it != savedShape.cend(); ++it) {
        EXPECT_TRUE(loadedShape.contains(it.key()))
            << engineName.toStdString() << ": the shape's " << it.key().toStdString()
            << " did not survive the file";
    }
}

TEST(PropertyCatalogue, Box2DKeepsWhatTheTableSets)
{
    everyRowHoldsWhatIsTypedIntoIt(QStringLiteral("Box2D"));
}

TEST(PropertyCatalogue, ChipmunkKeepsWhatTheTableSets)
{
    everyRowHoldsWhatIsTypedIntoIt(QStringLiteral("Chipmunk2D"));
}

// What a run answers with is shown, and shown read-only: a reading the engine
// publishes but the table cannot show is a rule menu entry with no counterpart
// anybody can see, and one shown as editable accepts typing it then discards.
void everyReadingIsShownReadOnly(const QString &engineName)
{
    auto engine = physics::EngineRegistry::create(engineName);
    ASSERT_TRUE(engine) << engineName.toStdString() << " is not installed";
    Bench bench(engineName);

    QSet<QString> readOnlyRows;
    QSet<QString> editableRows;
    for (const PropertyRow &row : bench.rows()) {
        if (row.key.isEmpty())
            continue;
        (row.readOnly ? readOnlyRows : editableRows).insert(row.key);
    }

    physics::PropertyList all = engine->bodyProperties();
    all += engine->shapeProperties();
    for (const physics::JointParam &property : all) {
        // Only what a run answers for and the scene does not keep, minus the
        // ones the catalogue deliberately keeps for the rule menus alone.
        if (!property.liveReadable || property.stored || property.rulesOnly
            || property.mirrorsSetting)
            continue;
        const std::string where = (engineName + QLatin1String(": ") + property.key).toStdString();
        EXPECT_TRUE(readOnlyRows.contains(property.key))
            << where << " is something a run answers for, but the table does not show it";
        EXPECT_FALSE(editableRows.contains(property.key))
            << where << " is a reading, and the table offers it as editable";
    }
}

TEST(PropertyCatalogue, Box2DShowsItsReadingsReadOnly)
{
    everyReadingIsShownReadOnly(QStringLiteral("Box2D"));
}

TEST(PropertyCatalogue, ChipmunkShowsItsReadingsReadOnly)
{
    everyReadingIsShownReadOnly(QStringLiteral("Chipmunk2D"));
}
