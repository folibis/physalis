#include "CanvasScene.h"
#include "MainWindow.h"
#include "PhysicsBody.h"
#include "RectangleItem.h"

#include <QAction>
#include <QApplication>
#include <QMenu>
#include <QVariantMap>
#include <gtest/gtest.h>

namespace {

void settle()
{
    for (int i = 0; i < 15; ++i)
        QCoreApplication::processEvents();
}

QStringList jointMenuLabels(MainWindow *window)
{
    QStringList labels;
    if (auto *menu = window->findChild<QMenu *>(QStringLiteral("menuJointType"))) {
        for (const QAction *action : menu->actions())
            labels << action->text();
    }
    return labels;
}

// A scene with something in it: two bodies and a joint between them.
void fill(CanvasScene *scene, const QString &jointType, int anchors)
{
    ShapeItem *first = scene->addRectangle(QPointF(0, 0));
    ShapeItem *second = scene->addRectangle(QPointF(200, 0));
    scene->setEditorMode(EditorMode::Physics);
    scene->selectForPhysics(first, true);
    PhysicsBody *bodyA = scene->createBodyFromSelection();
    scene->clearPhysicsSelection();
    scene->selectForPhysics(second, true);
    PhysicsBody *bodyB = scene->createBodyFromSelection();
    scene->clearPhysicsSelection();
    scene->createJoint(jointType, bodyA, bodyB, anchors, QVariantMap());
}

} // namespace

// Engines do not share joint types, so a scene cannot be handed from one to
// the other: changing the engine closes the scene and starts an empty one, and
// everything the engine has a say in is filled again from it.
TEST(EngineSwitch, ChangingTheEngineClosesTheSceneAndRefillsTheUi)
{
    MainWindow window;
    window.resize(1200, 800);
    window.show();
    settle();

    auto *scene = window.findChild<CanvasScene *>();
    ASSERT_TRUE(scene);
    EXPECT_EQ(scene->simulationEngineName(), QStringLiteral("Box2D"))
        << "a new scene starts on the engine the options name";
    EXPECT_TRUE(jointMenuLabels(&window).contains(QStringLiteral("Revolute")))
        << "and the menu offers that engine's joints";

    fill(scene, QStringLiteral("revolute"), 1);
    settle();
    ASSERT_EQ(scene->shapes().size(), 2);
    ASSERT_EQ(scene->joints().size(), 1);

    ASSERT_TRUE(window.adoptEngineForTest(QStringLiteral("Chipmunk2D")));
    settle();

    EXPECT_EQ(scene->simulationEngineName(), QStringLiteral("Chipmunk2D"));
    EXPECT_TRUE(scene->shapes().isEmpty() && scene->joints().isEmpty())
        << "the scene it could not have understood is closed, not carried over";
    EXPECT_EQ(scene->editorMode(), EditorMode::Edit)
        << "and an empty scene starts where a scene starts";

    const QStringList labels = jointMenuLabels(&window);
    EXPECT_TRUE(labels.contains(QStringLiteral("Groove")) && labels.contains(QStringLiteral("Ratchet")))
        << "the joint menu is the new engine's -- " << labels.join(QStringLiteral(", ")).toStdString();
    EXPECT_FALSE(labels.contains(QStringLiteral("Revolute")))
        << "with none of the old one's left in it";

    window.close();
}

TEST(EngineSwitch, AskingForTheEngineAlreadyInUseKeepsTheScene)
{
    MainWindow window;
    window.resize(1200, 800);
    window.show();
    settle();

    auto *scene = window.findChild<CanvasScene *>();
    ASSERT_TRUE(scene);
    fill(scene, QStringLiteral("revolute"), 1);
    settle();

    EXPECT_TRUE(window.adoptEngineForTest(scene->simulationEngineName()));
    EXPECT_EQ(scene->shapes().size(), 2) << "nothing was closed";
    EXPECT_EQ(scene->joints().size(), 1);

    window.close();
}
