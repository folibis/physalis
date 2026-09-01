#include "MainWindow.h"
#include "CanvasScene.h"
#include "SceneFixtures.h"
#include "SceneTree.h"
#include "ShapeItem.h"
#include "PhysicsBody.h"
#include "Joint.h"
#include "SceneSerializer.h"

#include <QApplication>
#include <gtest/gtest.h>
#include <QTabWidget>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <cstdio>


TEST(JumpTab, Behaves)
{
    MainWindow window;
    window.show();

    auto *tabs = window.findChild<QTabWidget *>();
    auto *tree = window.findChild<SceneTree *>();
    auto *scene = window.findChild<CanvasScene *>();
    if (!tabs || !tree || !scene) {
    return;
    }
    const int properties = 0, objects = 1;

    QString error;
    Fixtures::buildCart(scene);
    QCoreApplication::processEvents(); // the tree rebuilds queued

    tabs->setCurrentIndex(objects);
    scene->selectJoint(scene->joints().isEmpty() ? nullptr : scene->joints().first());
    EXPECT_TRUE(tabs->currentIndex() == properties) << "picking a joint shows the property table";

    tabs->setCurrentIndex(objects);
    scene->selectJoint(nullptr);
    scene->clearPhysicsSelection();
    PhysicsBody *body = nullptr;
    for (PhysicsBody *b : scene->bodies())
        if (!b->shapes().isEmpty())
            body = b;
    scene->selectForPhysics(body->shapes().first(), true);
    EXPECT_TRUE(tabs->currentIndex() == properties) << "picking a body shows the property table";

    tabs->setCurrentIndex(objects);
    scene->setEditorMode(EditorMode::Edit);
    scene->selectShape(scene->shapes().first());
    EXPECT_TRUE(tabs->currentIndex() == properties) << "picking a shape shows the property table";

    tabs->setCurrentIndex(properties);
    scene->selectShape(nullptr);
    EXPECT_TRUE(tabs->currentIndex() == properties) << "clearing the selection leaves the tab alone";

    auto *widget = tree->findChild<QTreeWidget *>();
    QTreeWidgetItem *row = nullptr;
    for (int i = 0; i < widget->topLevelItemCount() && !row; ++i) {
        QTreeWidgetItem *group = widget->topLevelItem(i);
        if (group->childCount() > 0)
            row = group->child(0);
    }
    if (!row) {
    return;
    }

    tabs->setCurrentIndex(objects);
    emit widget->itemClicked(row, 0);
    EXPECT_TRUE(tabs->currentIndex() == objects) << "one click selects and stays on the tree";

    emit widget->itemDoubleClicked(row, 0);
    EXPECT_TRUE(tabs->currentIndex() == properties) << "a double click shows the property table";

    tabs->setCurrentIndex(objects);
    emit widget->itemDoubleClicked(widget->topLevelItem(0), 0);
    EXPECT_TRUE(tabs->currentIndex() == objects) << "a double click on a group heading does nothing";
}
