#include "CanvasScene.h"
#include "Joint.h"
#include "PhysicsBody.h"
#include "PropertyPane/JointPropertyPane.h"
#include "PropertyPane/PhysicsPropertyPane.h"
#include "RectangleItem.h"

#include <QApplication>
#include <QVariantMap>
#include <gtest/gtest.h>

namespace {

// The rows a pane offers, by label.
const PropertyRow *rowNamed(const std::vector<PropertyRow> &rows, const char *label)
{
    for (const PropertyRow &row : rows) {
        if (row.label == QString::fromLatin1(label))
            return &row;
    }
    return nullptr;
}

} // namespace

// A row whose setter does nothing has to say so. Left editable it takes what
// is typed, discards it, and puts the old value back on the next refresh --
// which reads as the edit having been rejected for some reason of its own,
// rather than as a fact about the object that was never yours to change.
TEST(ReadOnlyRows, Behaves)
{
    CanvasScene scene;
    scene.setEditorMode(EditorMode::Physics);

    auto *a = new RectangleItem;
    a->setRect(QRectF(0, 0, 60, 40));
    a->setPos(0, 0);
    a->setName(QStringLiteral("left"));
    scene.addItem(a);
    auto *b = new RectangleItem;
    b->setRect(QRectF(0, 0, 60, 40));
    b->setPos(200, 0);
    b->setName(QStringLiteral("right"));
    scene.addItem(b);
    scene.notifyShapesChanged();

    scene.selectForPhysics(a, true);
    PhysicsBody *bodyA = scene.createBodyFromSelection();
    scene.clearPhysicsSelection();
    scene.selectForPhysics(b, true);
    PhysicsBody *bodyB = scene.createBodyFromSelection();
    scene.clearPhysicsSelection();
    ASSERT_TRUE(bodyA && bodyB);

    Joint *joint = scene.createJoint(QStringLiteral("revolute"), bodyA, bodyB, 1, QVariantMap());
    ASSERT_TRUE(joint != nullptr);

    // What a joint is and what it joins are settled when it is made.
    scene.selectJoint(joint);
    JointPropertyPane jointPane;
    jointPane.attach(&scene);
    const std::vector<PropertyRow> jointRows = jointPane.rows(EditorMode::Physics);
    for (const char *label : { "Type", "Body A", "Body B" }) {
        const PropertyRow *row = rowNamed(jointRows, label);
        ASSERT_TRUE(row != nullptr) << "the joint pane offers " << label;
        EXPECT_TRUE(row->readOnly) << label << " cannot be typed into";
    }

    // How many shapes a body has is decided by grouping them on the canvas.
    scene.selectJoint(nullptr);
    scene.selectForPhysics(a);
    PhysicsPropertyPane physicsPane;
    physicsPane.attach(&scene);
    const std::vector<PropertyRow> bodyRows = physicsPane.rows(EditorMode::Physics);
    for (const char *label : { "Shapes", "Body" }) {
        const PropertyRow *row = rowNamed(bodyRows, label);
        ASSERT_TRUE(row != nullptr) << "the physics pane offers " << label;
        EXPECT_TRUE(row->readOnly) << label << " cannot be typed into";
    }

    // And the ones that are genuinely editable still are.
    const PropertyRow *name = rowNamed(bodyRows, "Name");
    ASSERT_TRUE(name != nullptr);
    EXPECT_FALSE(name->readOnly) << "a name is still typed in";
}
