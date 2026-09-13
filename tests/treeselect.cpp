#include "CanvasScene.h"
#include "Joint.h"
#include "MainWindow.h"
#include "PhysicsBody.h"
#include "RectangleItem.h"
#include "ShapeItem.h"
#include "SimulationController.h"

#include <QApplication>
#include <QTreeWidget>
#include <QVariantMap>
#include <gtest/gtest.h>

namespace {

void settle()
{
    for (int i = 0; i < 6; ++i)
        QCoreApplication::processEvents();
}

} // namespace

// Picking a row in the scene tree used to do nothing for the whole length of a
// run -- and since the flag is set at start() and cleared only at stop(),
// pausing to look at something left the tree dead too. Picking is not editing:
// it points the panels at an object, which is the one time you most want it.
TEST(TreeSelect, Behaves)
{
    MainWindow window;
    window.resize(1200, 800);
    window.show();
    settle();

    auto *scene = window.findChild<CanvasScene *>();
    ASSERT_TRUE(scene != nullptr);

    auto *a = new RectangleItem;
    a->setRect(QRectF(0, 0, 200, 20));
    a->setPos(0, 0);
    scene->addItem(a);
    auto *b = new RectangleItem;
    b->setRect(QRectF(0, 0, 40, 40));
    b->setPos(80, -60);
    scene->addItem(b);
    scene->notifyShapesChanged();
    scene->setEditorMode(EditorMode::Physics);

    scene->selectForPhysics(a, true);
    PhysicsBody *bodyA = scene->createBodyFromSelection();
    scene->clearPhysicsSelection();
    scene->selectForPhysics(b, true);
    PhysicsBody *bodyB = scene->createBodyFromSelection();
    scene->clearPhysicsSelection();
    ASSERT_TRUE(bodyA && bodyB);
    Joint *joint = scene->createJoint(QStringLiteral("revolute"), bodyA, bodyB, 1, QVariantMap());
    ASSERT_TRUE(joint != nullptr);
    settle();

    auto *tree = window.findChild<QTreeWidget *>();
    auto *sim = window.findChild<SimulationController *>();
    ASSERT_TRUE(tree && sim);

    // Bodies group, then Joints group; the joint is the first row under it.
    ASSERT_GE(tree->invisibleRootItem()->childCount(), 2);
    QTreeWidgetItem *jointRow = tree->invisibleRootItem()->child(1)->child(0);
    ASSERT_TRUE(jointRow != nullptr);

    const auto pick = [&] {
        scene->selectJoint(nullptr);
        settle();
        emit tree->itemClicked(jointRow, 0);
        settle();
        return scene->selectedJoint();
    };

    EXPECT_EQ(pick(), joint) << "a joint picked in the tree is the selected joint";

    sim->setEngineName(QStringLiteral("Box2D"));
    sim->start();
    settle();
    ASSERT_FALSE(scene->selectionAllowed()) << "a run is under way";
    EXPECT_EQ(pick(), joint) << "and picking still works while it runs";

    sim->pause();
    settle();
    ASSERT_TRUE(sim->isActive() && !sim->isRunning()) << "paused, not stopped";
    EXPECT_EQ(pick(), joint) << "and while it is paused, which is when you look";

    sim->stop();
    settle();
    EXPECT_EQ(pick(), joint) << "and after it ends";

    // The mode buttons are disabled for the length of a run, so the tree must
    // not go round them: a shape picked mid-run is selected where the run can
    // show it rather than by flipping the editor into Edit mode.
    sim->start();
    settle();
    const EditorMode before = scene->editorMode();
    QTreeWidgetItem *shapeRow = tree->invisibleRootItem()->child(0)->child(0)->child(0);
    ASSERT_TRUE(shapeRow != nullptr);
    emit tree->itemClicked(shapeRow, 0);
    settle();
    EXPECT_EQ(scene->editorMode(), before) << "a run is not a place to change mode";
    EXPECT_FALSE(scene->physicsSelection().isEmpty()) << "but the shape is still picked";
    sim->stop();
}
