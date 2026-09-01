#include "MainWindow.h"
#include "CanvasScene.h"
#include "ShapeItem.h"

#include <QApplication>
#include <gtest/gtest.h>
#include <QAction>
#include <QToolBar>
#include <QMenuBar>
#include <QMenu>
#include <cstdio>

static QAction *action(QWidget *w, const char *name)
{
    return w->findChild<QAction *>(QString::fromLatin1(name));
}

TEST(AutoWire, Behaves)
{
    MainWindow window;
    window.show();
    QCoreApplication::processEvents();
    auto *scene = window.findChild<CanvasScene *>();

    // Each of these is wired only by connectSlotsByName -- nothing in C++
    // connects them -- so if the naming is wrong they silently do nothing.
    action(&window, "actionNewScene")->trigger();
    EXPECT_TRUE(scene->shapes().isEmpty()) << "New Scene runs (clean scene, so no question asked)";

    const int before = scene->shapes().size();
    action(&window, "actionAddRectangle")->trigger();
    EXPECT_TRUE(scene->shapes().size() == before + 1) << "Add Rectangle adds a shape";

    action(&window, "actionAddCircle")->trigger();
    EXPECT_TRUE(scene->shapes().size() == before + 2) << "Add Circle adds another";

    // Add Polygon starts an interactive drawing rather than dropping a shape,
    // so what it proves is that the scene entered that mode.
    action(&window, "actionAddPolygon")->trigger();
    EXPECT_TRUE(scene->isPolygonDrawing()) << "Add Polygon starts polygon drawing";
    QMetaObject::invokeMethod(scene, "cancelPolygonDrawing");

    // Undo and Redo are reached the same way -- nothing in C++ connects them.
    const int after = scene->shapes().size();
    action(&window, "actionUndo")->trigger();
    EXPECT_TRUE(scene->shapes().size() == after - 1) << "Undo removes the last shape";
    action(&window, "actionRedo")->trigger();
    EXPECT_TRUE(scene->shapes().size() == after) << "Redo brings it back";

    QStringList menus;
    for (QAction *a : window.menuBar()->actions())
        menus << a->text().remove(QLatin1Char('&'));
    EXPECT_TRUE(menus == QStringList({ QStringLiteral("File"), QStringLiteral("Edit"),
                                       QStringLiteral("Shapes"), QStringLiteral("Help") }))
        << "menu bar carries File, Edit, Shapes and Help -- got "
        << menus.join(QStringLiteral(", ")).toStdString();
    auto *bar = window.findChild<QToolBar *>("toolBar");
    EXPECT_TRUE(bar && bar->actions().size() > 15) << "toolbar exists with its actions";
    EXPECT_TRUE(window.findChild<QWidget *>("canvasView") != nullptr) << "canvas view is in the form";
    EXPECT_TRUE(window.findChild<QWidget *>("topRuler") && window.findChild<QWidget *>("leftRuler")) << "both rulers are in the form";
    EXPECT_TRUE(window.findChild<QWidget *>("propertyDock")
              && window.findChild<QWidget *>("propertyPanel")
              && window.findChild<QWidget *>("sceneTree")
              && window.findChild<QWidget *>("rulesPanel")) << "dock and its three panels are in the form";
    EXPECT_TRUE(window.findChild<QMenu *>("menuJointType")
              && !window.findChild<QMenu *>("menuJointType")->actions().isEmpty()) << "the joint menu was filled from the engine";

    window.close();
}
