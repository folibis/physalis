#include "CanvasScene.h"
#include "ExplosionItem.h"
#include "PhysicsBody.h"
#include "RectangleItem.h"
#include "ShapeItem.h"
#include "SimulationController.h"

#include <QApplication>
#include <QKeyEvent>
#include <gtest/gtest.h>

namespace {

void press(CanvasScene *scene, int key)
{
    QKeyEvent event(QEvent::KeyPress, key, Qt::NoModifier);
    QCoreApplication::sendEvent(scene, &event);
}

RectangleItem *rect(CanvasScene *scene, const QPointF &at, const char *name)
{
    auto *item = new RectangleItem;
    item->setRect(QRectF(0, 0, 40, 40));
    item->setPos(at);
    item->setName(QString::fromLatin1(name));
    scene->addItem(item);
    return item;
}

} // namespace

// One scene unit an arrow press, in either mode, for whatever is picked. It
// deliberately ignores the grid: nudging is for the placement a drag cannot
// land on.
TEST(Nudge, Behaves)
{
    CanvasScene scene;
    scene.setSnapToGrid(true);           // must not round the step away

    RectangleItem *a = rect(&scene, QPointF(100, 100), "a");
    RectangleItem *b = rect(&scene, QPointF(300, 100), "b");
    scene.notifyShapesChanged();

    // --- Edit mode: the active shape, and anything picked with it ---------
    scene.setEditorMode(EditorMode::Edit);
    scene.selectShape(a);
    press(&scene, Qt::Key_Right);
    EXPECT_EQ(a->pos(), QPointF(101, 100)) << "right moves one unit, grid or no grid";
    press(&scene, Qt::Key_Down);
    EXPECT_EQ(a->pos(), QPointF(101, 101)) << "down is positive Y, as the scene has it";
    press(&scene, Qt::Key_Left);
    press(&scene, Qt::Key_Up);
    EXPECT_EQ(a->pos(), QPointF(100, 100)) << "and back again";

    scene.addToEditSelection(b);
    press(&scene, Qt::Key_Right);
    EXPECT_EQ(a->pos(), QPointF(101, 100)) << "a co-selected group moves together";
    EXPECT_EQ(b->pos(), QPointF(301, 100));

    // --- Physics mode: the whole body, so its joints keep their anchors ---
    scene.setEditorMode(EditorMode::Physics);
    scene.selectForPhysics(a, true);
    scene.selectForPhysics(b, true);
    PhysicsBody *body = scene.createBodyFromSelection();
    ASSERT_TRUE(body != nullptr);
    const QPointF beforeA = a->pos();
    const QPointF beforeB = b->pos();

    scene.clearPhysicsSelection();
    scene.selectForPhysics(a);           // one shape picked, whole body moves
    press(&scene, Qt::Key_Down);
    EXPECT_EQ(a->pos(), beforeA + QPointF(0, 1)) << "the picked shape moves";
    EXPECT_EQ(b->pos(), beforeB + QPointF(0, 1))
        << "and so does the rest of its body, or the joints on it are left behind";

    // --- an explosion is an object too -----------------------------------
    scene.clearPhysicsSelection();
    ExplosionItem *boom = scene.addExplosion(QPointF(500, 500));
    scene.selectExplosion(boom);
    press(&scene, Qt::Key_Left);
    EXPECT_EQ(boom->pos(), QPointF(499, 500)) << "a picked explosion nudges too";

    // --- nothing picked, nothing moves ------------------------------------
    scene.selectExplosion(nullptr);
    scene.clearPhysicsSelection();
    const QPointF held = a->pos();
    press(&scene, Qt::Key_Right);
    EXPECT_EQ(a->pos(), held) << "with nothing picked the arrows are not ours";
}

// Setting a position during a run does not move a body, it teleports it --
// past the solver, through whatever was in the way. The arrows stay out of it.
TEST(NudgeDuringRun, Behaves)
{
    CanvasScene scene;
    RectangleItem *a = rect(&scene, QPointF(100, 100), "a");
    scene.notifyShapesChanged();
    scene.setEditorMode(EditorMode::Physics);
    scene.selectForPhysics(a, true);
    ASSERT_TRUE(scene.createBodyFromSelection() != nullptr);
    scene.clearPhysicsSelection();
    scene.selectForPhysics(a);

    SimulationController sim(&scene, nullptr);
    sim.setEngineName(QStringLiteral("Box2D"));
    sim.start();
    ASSERT_FALSE(scene.selectionAllowed());

    const QPointF held = a->pos();
    press(&scene, Qt::Key_Right);
    EXPECT_EQ(a->pos(), held) << "a run owns the positions";

    sim.stop();
    press(&scene, Qt::Key_Right);
    EXPECT_EQ(a->pos(), held + QPointF(1, 0)) << "and hands them back when it ends";
}
