#include "CanvasScene.h"
#include "MainWindow.h"
#include "RectangleItem.h"
#include "ShapeItem.h"

#include <QApplication>
#include <QGraphicsSceneMouseEvent>
#include <QAction>
#include <QDir>
#include <QGraphicsView>
#include <QtMath>
#include <gtest/gtest.h>

namespace {

void settle()
{
    for (int i = 0; i < 15; ++i)
        QCoreApplication::processEvents();
}

void click(CanvasScene *scene, const QPointF &at,
           Qt::KeyboardModifiers modifiers = Qt::NoModifier)
{
    QGraphicsSceneMouseEvent press(QEvent::GraphicsSceneMousePress);
    press.setScenePos(at);
    press.setButton(Qt::LeftButton);
    press.setButtons(Qt::LeftButton);
    press.setModifiers(modifiers);
    QApplication::sendEvent(scene, &press);

    QGraphicsSceneMouseEvent release(QEvent::GraphicsSceneMouseRelease);
    release.setScenePos(at);
    release.setButton(Qt::LeftButton);
    release.setModifiers(modifiers);
    QApplication::sendEvent(scene, &release);
    settle();
}

void drag(CanvasScene *scene, const QPointF &from, const QPointF &to)
{
    QGraphicsSceneMouseEvent press(QEvent::GraphicsSceneMousePress);
    press.setScenePos(from);
    press.setButton(Qt::LeftButton);
    press.setButtons(Qt::LeftButton);
    QApplication::sendEvent(scene, &press);

    QGraphicsSceneMouseEvent move(QEvent::GraphicsSceneMouseMove);
    move.setScenePos(to);
    move.setButtons(Qt::LeftButton);
    QApplication::sendEvent(scene, &move);

    QGraphicsSceneMouseEvent release(QEvent::GraphicsSceneMouseRelease);
    release.setScenePos(to);
    release.setButton(Qt::LeftButton);
    QApplication::sendEvent(scene, &release);
    settle();
}

RectangleItem *box(CanvasScene *scene, const QString &name, const QPointF &at)
{
    auto *item = new RectangleItem;
    item->setRect(QRectF(0, 0, 60, 60));
    item->setPos(at);
    item->setName(name);
    scene->addItem(item);
    return item;
}

}

// Several shapes can be picked at once in Edit mode, and then move and turn
// together as one piece.
TEST(MultiSelect, MovesAndRotatesTogether)
{
    MainWindow window;
    window.resize(1200, 800);
    window.show();
    settle();

    auto *scene = window.findChild<CanvasScene *>();
    scene->setSnapToGrid(false);

    RectangleItem *a = box(scene, QStringLiteral("a"), QPointF(0, 0));
    RectangleItem *b = box(scene, QStringLiteral("b"), QPointF(200, 0));
    RectangleItem *c = box(scene, QStringLiteral("c"), QPointF(0, 200));
    scene->notifyShapesChanged();
    settle();

    const QPointF insideA(30, 30), insideB(230, 30), insideC(30, 230);

    click(scene, insideA);
    ASSERT_EQ(scene->activeItem(), a) << "a plain click picks one shape";
    EXPECT_TRUE(scene->editSelection().isEmpty());

    click(scene, insideB, Qt::ShiftModifier);
    EXPECT_EQ(scene->activeItem(), a) << "the first shape keeps the handles";
    ASSERT_EQ(scene->editSelection().size(), 1);
    EXPECT_EQ(scene->editSelection().first(), b) << "Shift adds rather than replaces";
    EXPECT_TRUE(b->isCoSelected()) << "and it is drawn as selected";

    click(scene, insideC, Qt::ControlModifier);
    EXPECT_EQ(scene->editSelection().size(), 2) << "Ctrl adds too";

    // Dragging the lead carries the others by exactly the same amount.
    const QPointF startA = a->pos(), startB = b->pos(), startC = c->pos();
    drag(scene, insideA, insideA + QPointF(50, 25));
    const QPointF moved = a->pos() - startA;
    EXPECT_GT(QLineF(QPointF(), moved).length(), 1.0) << "something moved";
    EXPECT_EQ(b->pos() - startB, moved) << "b follows the lead";
    EXPECT_EQ(c->pos() - startC, moved) << "c follows the lead";

    // Grabbing one of the others drags the set too, not just that shape.
    const QPointF beforeA = a->pos();
    drag(scene, insideB + moved, insideB + moved + QPointF(-30, 0));
    EXPECT_EQ(a->pos() - beforeA, QPointF(-30, 0))
        << "the group can be grabbed by any of its shapes";

    // The pivot belongs to the group, not to any one shape: it starts at the
    // centre of everything picked.
    QRectF bounds = a->sceneBoundingRect()
                        .united(b->sceneBoundingRect())
                        .united(c->sceneBoundingRect());
    EXPECT_NEAR(scene->editSelectionOrigin().x(), bounds.center().x(), 0.01);
    EXPECT_NEAR(scene->editSelectionOrigin().y(), bounds.center().y(), 0.01);

    // Move mode shows the group's box and corner handles, not the pivot; the
    // Rotate button is what brings the pivot out.
    EXPECT_FALSE(scene->editSelectionRotating());
    EXPECT_EQ(scene->editSelectionBounds(),
              a->mapToScene(a->rect()).boundingRect()
                  .united(b->mapToScene(b->rect()).boundingRect())
                  .united(c->mapToScene(c->rect()).boundingRect()));
    auto *rotate = window.findChild<QAction *>(QStringLiteral("actionRotate"));
    ASSERT_NE(rotate, nullptr);
    EXPECT_TRUE(rotate->isEnabled()) << "Rotate is offered for a group";

    if (auto *view = window.findChild<QGraphicsView *>(QStringLiteral("canvasView")))
        view->grab().save(QDir(QDir::tempPath()).filePath(
            QStringLiteral("multiselect_picked.png")));

    // Dragging a corner scales everything picked about the opposite corner.
    {
        const QRectF before = scene->editSelectionBounds();
        const QPointF armBefore = b->mapToScene(b->origin()) - before.topLeft();
        const QSizeF sizeBefore = b->rect().size();
        drag(scene, before.bottomRight(),
             before.topLeft() + QPointF(before.width(), before.height()) * 2.0);
        const QRectF after = scene->editSelectionBounds();
        EXPECT_NEAR(after.width() / before.width(), 2.0, 0.05) << "the group grew";
        EXPECT_NEAR(after.topLeft().x(), before.topLeft().x(), 0.01)
            << "the far corner stayed put";
        EXPECT_NEAR(b->rect().width() / sizeBefore.width(), 2.0, 0.05)
            << "each shape grew with it";
        const QPointF armAfter = b->mapToScene(b->origin()) - before.topLeft();
        EXPECT_NEAR(armAfter.x(), armBefore.x() * 2.0, 0.5) << "and moved out with it";
        EXPECT_NEAR(armAfter.y(), armBefore.y() * 2.0, 0.5);
        // Back to where it was, so the turns below start from round numbers.
        drag(scene, scene->editSelectionBounds().bottomRight(), before.bottomRight());
    }

    // Turning: every shape, the lead included, gains the same angle and orbits
    // that one pivot, so the arrangement turns as a single piece.
    // The button is the way in, and it is what brings the pivot out.
    rotate->trigger();
    settle();
    ASSERT_TRUE(scene->editSelectionRotating()) << "Rotate turned the group over";
    const QPointF pivot = scene->editSelectionOrigin();
    const QPointF armA = a->mapToScene(a->origin()) - pivot;
    const QPointF armB = b->mapToScene(b->origin()) - pivot;
    const qreal rotA = a->rotation(), rotB = b->rotation();

    const QPointF grab = a->mapToScene(a->rect().center());
    drag(scene, grab, pivot + QPointF(0, QLineF(pivot, grab).length()));

    const qreal turned = a->rotation() - rotA;
    EXPECT_GT(qAbs(turned), 5.0) << "the group actually turned";
    EXPECT_NEAR(b->rotation() - rotB, turned, 0.001)
        << "every shape turned by the same angle";

    QTransform expected;
    expected.rotate(turned);
    const auto orbited = [&](ShapeItem *shape, const QPointF &arm, const char *who) {
        const QPointF now = shape->mapToScene(shape->origin()) - pivot;
        EXPECT_NEAR(now.x(), expected.map(arm).x(), 0.5) << who << " orbited the pivot";
        EXPECT_NEAR(now.y(), expected.map(arm).y(), 0.5) << who << " orbited the pivot";
    };
    orbited(a, armA, "the lead");
    orbited(b, armB, "a follower");

    if (auto *view = window.findChild<QGraphicsView *>(QStringLiteral("canvasView")))
        view->grab().save(QDir(QDir::tempPath()).filePath(
            QStringLiteral("multiselect_turned.png")));

    // The pivot itself can be dragged, and the group then turns about where it
    // was put.
    scene->switchActiveToSelected();
    settle();
    const QPointF moved_pivot = pivot + QPointF(120, 60);
    drag(scene, pivot, moved_pivot);
    EXPECT_NEAR(scene->editSelectionOrigin().x(), moved_pivot.x(), 0.01)
        << "the pivot follows the mouse";
    EXPECT_NEAR(scene->editSelectionOrigin().y(), moved_pivot.y(), 0.01);
    EXPECT_EQ(a->rotation() - rotA, turned) << "dragging the pivot turns nothing";

    rotate->trigger();
    settle();
    const QPointF armFromNew = a->mapToScene(a->origin()) - moved_pivot;
    const QPointF grab2 = a->mapToScene(a->rect().center());
    drag(scene, grab2, moved_pivot + QPointF(0, QLineF(moved_pivot, grab2).length()));
    const qreal turnedAgain = a->rotation() - rotA - turned;
    ASSERT_GT(qAbs(turnedAgain), 5.0) << "the second turn happened at all";
    QTransform second;
    second.rotate(turnedAgain);
    const QPointF nowFromNew = a->mapToScene(a->origin()) - moved_pivot;
    EXPECT_NEAR(nowFromNew.x(), second.map(armFromNew).x(), 0.5)
        << "the second turn used the pivot where it was dropped";
    EXPECT_NEAR(nowFromNew.y(), second.map(armFromNew).y(), 0.5);
    scene->switchActiveToSelected();
    settle();

    // Picking a shape again drops it, and a plain click starts over.
    a->setMode(ShapeMode::Selected);
    click(scene, b->mapToScene(b->rect().center()), Qt::ShiftModifier);
    EXPECT_EQ(scene->editSelection().size(), 1) << "Shift on a picked shape drops it";
    EXPECT_FALSE(b->isCoSelected());

    click(scene, c->mapToScene(c->rect().center()));
    EXPECT_EQ(scene->activeItem(), c);
    EXPECT_TRUE(scene->editSelection().isEmpty())
        << "a plain click starts a new selection";

    // Delete takes the whole selection, not just the shape with the handles.
    click(scene, c->mapToScene(c->rect().center()));
    click(scene, a->mapToScene(a->rect().center()), Qt::ShiftModifier);
    ASSERT_EQ(scene->editSelection().size(), 1);
    const int before = scene->shapes().size();
    scene->deleteActiveItem();
    settle();
    EXPECT_EQ(scene->shapes().size(), before - 2) << "both picked shapes go";
    EXPECT_EQ(scene->activeItem(), nullptr);
    EXPECT_TRUE(scene->editSelection().isEmpty());

    window.close();
}
