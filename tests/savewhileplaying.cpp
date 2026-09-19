#include "CanvasScene.h"
#include "MainWindow.h"
#include "PhysicsBody.h"
#include "RectangleItem.h"

#include <QAction>
#include <QApplication>
#include <gtest/gtest.h>

// A run moves everything, so what Save would write mid-run is a moment of the
// run rather than the scene -- and anything else that changes the scene, or
// puts another in its place, would change a moment of the run. All of it is
// off until the run is stopped.

namespace {

void settle()
{
    for (int i = 0; i < 30; ++i)
        QCoreApplication::processEvents();
}

QAction *action(MainWindow &window, const char *name)
{
    return window.findChild<QAction *>(QString::fromLatin1(name));
}

} // namespace

TEST(SaveWhilePlaying, SavingIsOffDuringARun)
{
    MainWindow window;
    window.show();
    settle();

    auto *scene = window.findChild<CanvasScene *>();
    ASSERT_TRUE(scene);
    ShapeItem *box = scene->addRectangle(QPointF(0, 0));
    scene->notifyShapesChanged();
    scene->setEditorMode(EditorMode::Physics);
    scene->selectForPhysics(box, true);
    ASSERT_TRUE(scene->createBodyFromSelection());
    scene->clearPhysicsSelection();
    settle();

    QAction *save = action(window, "actionSaveScene");
    QAction *saveAs = action(window, "actionSaveSceneAs");
    ASSERT_TRUE(save && saveAs);
    // Save follows whether there is anything unsaved; that is not the point here.
    const bool saveBefore = save->isEnabled();
    EXPECT_TRUE(saveAs->isEnabled());

    action(window, "actionSimulate")->trigger();
    settle();
    EXPECT_FALSE(save->isEnabled()) << "Save is on while the scene plays";
    EXPECT_FALSE(saveAs->isEnabled()) << "Save As is on while the scene plays";

    action(window, "actionStop")->trigger();
    settle();
    EXPECT_EQ(save->isEnabled(), saveBefore) << "Stop gives Save back as it was";
    EXPECT_TRUE(saveAs->isEnabled());
}

TEST(SaveWhilePlaying, NothingChangesTheSceneDuringARun)
{
    MainWindow window;
    window.show();
    settle();

    auto *scene = window.findChild<CanvasScene *>();
    ASSERT_TRUE(scene);
    ShapeItem *box = scene->addRectangle(QPointF(0, 0));
    scene->notifyShapesChanged();
    scene->setEditorMode(EditorMode::Physics);
    scene->selectForPhysics(box, true);
    ASSERT_TRUE(scene->createBodyFromSelection());
    scene->clearPhysicsSelection();
    settle();

    const char *locked[] = { "actionNewScene", "actionLoadScene", "actionOptions",
                             "actionSaveScene", "actionSaveSceneAs", "actionUndo", "actionRedo",
                             "actionAddShape", "actionAddRectangle", "actionAddCircle",
                             "actionAddPolygon", "actionAddRay", "actionAddExplosion",
                             "actionAddJoint", "actionDeleteJoint", "actionCreateBody",
                             "actionDissolveBody", "actionCopy", "actionPaste", "actionDelete",
                             "actionMoveScale", "actionEditNodes", "actionRotate" };

    action(window, "actionSimulate")->trigger();
    settle();
    // A selection made mid-run runs the handlers that decide these; they must
    // not turn anything back on.
    scene->selectForPhysics(box, true);
    settle();
    for (const char *name : locked) {
        QAction *a = action(window, name);
        ASSERT_TRUE(a) << name;
        EXPECT_FALSE(a->isEnabled()) << name << " is on while the scene plays";
    }
    EXPECT_TRUE(action(window, "actionStop")->isEnabled());
    EXPECT_TRUE(action(window, "actionSaveScreenshot")->isEnabled()) << "looking at the run is fine";

    action(window, "actionStop")->trigger();
    settle();
    for (const char *name : { "actionNewScene", "actionLoadScene", "actionOptions",
                              "actionSaveSceneAs", "actionAddRay", "actionAddExplosion" })
        EXPECT_TRUE(action(window, name)->isEnabled()) << name << " did not come back after Stop";
}
