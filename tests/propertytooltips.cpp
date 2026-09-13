#include "CanvasScene.h"
#include "SimulationController.h"
#include "SceneFixtures.h"
#include "Joint.h"
#include "PhysicsBody.h"
#include "ShapeItem.h"
#include "PropertyPane/PropertyPane.h"
#include "PropertyPane/PropertyPaneFactory.h"

#include <QApplication>
#include <algorithm>
#include <gtest/gtest.h>

// Every row in the property table explains itself on hover, and the words are
// the engine's rather than the editor's. Two things had to be true for that and
// neither was: the panes were dropping the tooltip on the way from the
// catalogue to the row, and the panel was hanging it on the cell rather than on
// the label -- which is the widget actually under the pointer, and which does
// not inherit its parent's.
namespace {

QStringList untipped(const std::vector<PropertyRow> &rows)
{
    QStringList missing;
    for (const PropertyRow &row : rows) {
        if (row.tooltip.isEmpty())
            missing << row.label;
    }
    return missing;
}

} // namespace

TEST(PropertyTooltips, BodyAndShapeRowsCarryThem)
{
    CanvasScene scene;
    Fixtures::buildCart(&scene);
    scene.setEditorMode(EditorMode::Physics);

    ShapeItem *chassis = nullptr;
    for (ShapeItem *s : scene.shapes())
        if (s->name() == QLatin1String("chassis"))
            chassis = s;
    ASSERT_NE(chassis, nullptr);
    scene.selectForPhysics(chassis, true);

    PropertyPaneFactory factory(nullptr);
    PropertyPane *pane = factory.paneForPhysics(&scene);
    ASSERT_NE(pane, nullptr);
    pane->attach(&scene);

    const std::vector<PropertyRow> rows = pane->rows(EditorMode::Physics);
    ASSERT_FALSE(rows.empty()) << "the physics pane offered no rows at all";

    const QStringList missing = untipped(rows);
    EXPECT_TRUE(missing.isEmpty())
        << "rows with nothing to show on hover: " << missing.join(QStringLiteral(", ")).toStdString();
}

TEST(PropertyTooltips, JointRowsCarryThem)
{
    CanvasScene scene;
    Fixtures::buildCart(&scene);
    scene.setEditorMode(EditorMode::Physics);
    ASSERT_FALSE(scene.joints().isEmpty());
    scene.selectJoint(scene.joints().first());

    PropertyPaneFactory factory(nullptr);
    PropertyPane *pane = factory.paneForJoints(&scene);
    ASSERT_NE(pane, nullptr);
    pane->attach(&scene);

    const std::vector<PropertyRow> rows = pane->rows(EditorMode::Physics);
    ASSERT_FALSE(rows.empty()) << "the joint pane offered no rows at all";

    const QStringList missing = untipped(rows);
    EXPECT_TRUE(missing.isEmpty())
        << "rows with nothing to show on hover: " << missing.join(QStringLiteral(", ")).toStdString();
}

// A shape in edit mode: its geometry and how it is drawn. None of it is the
// engine's business, so the editor explains it itself.
TEST(PropertyTooltips, ShapeRowsCarryThemInEditMode)
{
    CanvasScene scene;
    Fixtures::buildCart(&scene);
    scene.setEditorMode(EditorMode::Edit);

    PropertyPaneFactory factory(nullptr);
    for (ShapeItem *shape : scene.shapes()) {
        PropertyPane *pane = factory.paneFor(shape);
        ASSERT_NE(pane, nullptr) << shape->name().toStdString();
        pane->attach(shape);

        const std::vector<PropertyRow> rows = pane->rows(EditorMode::Edit);
        if (rows.empty())
            continue;
        const QStringList missing = untipped(rows);
        EXPECT_TRUE(missing.isEmpty())
            << shape->name().toStdString() << " has rows with nothing to show on hover: "
            << missing.join(QStringLiteral(", ")).toStdString();
    }
}

// The field is the editor's own idea -- its size, its grid, how far the view is
// zoomed -- so nothing about it comes from the engine, and it is the one pane
// that has rows in edit mode at all.
TEST(PropertyTooltips, FieldRowsCarryThemInBothModes)
{
    CanvasScene scene;
    Fixtures::buildCart(&scene);

    PropertyPaneFactory factory(nullptr);
    PropertyPane *pane = factory.paneForField(&scene);
    ASSERT_NE(pane, nullptr);
    pane->attach(&scene);

    for (EditorMode mode : { EditorMode::Edit, EditorMode::Physics }) {
        scene.setEditorMode(mode);
        const std::vector<PropertyRow> rows = pane->rows(mode);
        ASSERT_FALSE(rows.empty())
            << "the field pane offered no rows in "
            << (mode == EditorMode::Edit ? "edit" : "physics") << " mode";

        const QStringList missing = untipped(rows);
        EXPECT_TRUE(missing.isEmpty())
            << "rows with nothing to show on hover in "
            << (mode == EditorMode::Edit ? "edit" : "physics") << " mode: "
            << missing.join(QStringLiteral(", ")).toStdString();
    }
}

// b2RevoluteJoint_GetAngle and its like: what the joint measures rather than
// what it is told. They belong in the table read-only, and they have to carry
// the engine's key -- that is the only handle the log has on a value.
TEST(PropertyTooltips, JointMeasuredValuesAreReadOnlyAndLoggable)
{
    CanvasScene scene;
    Fixtures::buildCart(&scene);
    scene.setEditorMode(EditorMode::Physics);
    ASSERT_FALSE(scene.joints().isEmpty());
    scene.selectJoint(scene.joints().first());

    PropertyPaneFactory factory(nullptr);
    PropertyPane *pane = factory.paneForJoints(&scene);
    pane->attach(&scene);

    // These are what the solver produces, so they are offered while it is
    // producing them and not before: outside a run nothing can answer one, and
    // a row that reads zero whatever the joint is doing looks like a
    // measurement without being one.
    const std::vector<PropertyRow> idle = pane->rows(EditorMode::Physics);
    EXPECT_TRUE(std::none_of(idle.begin(), idle.end(), [](const PropertyRow &row) {
        return row.group == QStringLiteral("Measured");
    })) << "nothing measured is offered while nothing is running";

    SimulationController sim(&scene, nullptr);
    sim.setEngineName(QStringLiteral("Box2D"));
    sim.start();

    int measured = 0;
    for (const PropertyRow &row : pane->rows(EditorMode::Physics)) {
        if (!row.readOnly || row.key.isEmpty())
            continue;
        ++measured;
        EXPECT_FALSE(row.tooltip.isEmpty())
            << row.label.toStdString() << " is offered with no explanation";
    }
    EXPECT_GT(measured, 0)
        << "the joint reports nothing measurable, so nothing about it can be logged";
    sim.stop();
}
