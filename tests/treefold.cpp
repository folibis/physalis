#include "CanvasScene.h"
#include "Joint.h"
#include "MainWindow.h"
#include "PhysicsBody.h"
#include "RectangleItem.h"
#include "SceneTree.h"

#include <QApplication>
#include <QTreeWidget>
#include <QVariantMap>
#include <gtest/gtest.h>

namespace {

void settle()
{
    for (int i = 0; i < 8; ++i)
        QCoreApplication::processEvents();
}

QTreeWidgetItem *groupStarting(QTreeWidget *tree, const char *prefix)
{
    QTreeWidgetItem *root = tree->invisibleRootItem();
    for (int i = 0; i < root->childCount(); ++i) {
        if (root->child(i)->text(0).startsWith(QString::fromLatin1(prefix)))
            return root->child(i);
    }
    return nullptr;
}

PhysicsBody *bodyFrom(CanvasScene *scene, const QPointF &at, const char *name)
{
    auto *shape = new RectangleItem;
    shape->setRect(QRectF(0, 0, 40, 40));
    shape->setPos(at);
    shape->setName(QString::fromLatin1(name));
    scene->addItem(shape);
    scene->notifyShapesChanged();
    scene->selectForPhysics(shape, true);
    PhysicsBody *body = scene->createBodyFromSelection();
    scene->clearPhysicsSelection();
    return body;
}

} // namespace

// The tree is built again from scratch whenever the scene changes, and it
// opens everything when it does. So a row folded shut sprang open the moment
// anything moved -- which made folding useless. What is folded is remembered
// by a key that outlives the rebuild, rather than by the row itself.
TEST(TreeFold, Behaves)
{
    MainWindow window;
    window.resize(1000, 800);
    window.show();
    settle();

    auto *scene = window.findChild<CanvasScene *>();
    auto *tree = window.findChild<QTreeWidget *>();
    ASSERT_TRUE(scene && tree);
    scene->setEditorMode(EditorMode::Physics);

    PhysicsBody *a = bodyFrom(scene, QPointF(0, 0), "left");
    PhysicsBody *b = bodyFrom(scene, QPointF(200, 0), "right");
    ASSERT_TRUE(a && b);
    ASSERT_TRUE(scene->createJoint(QStringLiteral("revolute"), a, b, 1, QVariantMap()) != nullptr);
    settle();

    QTreeWidgetItem *bodies = groupStarting(tree, "Bodies");
    ASSERT_TRUE(bodies != nullptr) << "the tree lists the bodies";
    EXPECT_TRUE(bodies->isExpanded()) << "open to begin with";

    bodies->setExpanded(false);
    settle();
    EXPECT_FALSE(bodies->isExpanded());

    // Anything that changes the scene rebuilds the tree.
    bodyFrom(scene, QPointF(400, 0), "third");
    settle();

    QTreeWidgetItem *rebuilt = groupStarting(tree, "Bodies");
    ASSERT_TRUE(rebuilt != nullptr);
    EXPECT_FALSE(rebuilt->isExpanded())
        << "still folded after the tree was built again";
    EXPECT_TRUE(rebuilt->text(0).contains(QStringLiteral("3")))
        << "and it is the rebuilt row, not the old one" << " -- " << rebuilt->text(0).toStdString();

    // Joints fold on their own, independently of the bodies.
    QTreeWidgetItem *joints = groupStarting(tree, "Joints");
    ASSERT_TRUE(joints != nullptr);
    EXPECT_TRUE(joints->isExpanded()) << "folding one group leaves the other alone";

    rebuilt->setExpanded(true);
    settle();
    bodyFrom(scene, QPointF(600, 0), "fourth");
    settle();
    QTreeWidgetItem *again = groupStarting(tree, "Bodies");
    ASSERT_TRUE(again != nullptr);
    EXPECT_TRUE(again->isExpanded()) << "and unfolding is remembered the same way";
}
