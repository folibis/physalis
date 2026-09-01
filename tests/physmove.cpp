#include "CanvasScene.h"
#include "MainWindow.h"
#include "PhysicsBody.h"
#include "RectangleItem.h"
#include "ShapeItem.h"
#include "SimulationController.h"

#include <QApplication>
#include <gtest/gtest.h>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsView>
#include <QScrollBar>
#include <cstdio>

static void settle()
{
    for (int i = 0; i < 15; ++i)
        QCoreApplication::processEvents();
}

static void drag(CanvasScene *scene, QGraphicsView *view, const QPointF &from,
                 const QPointF &to)
{
    const QPoint screenFrom = view->viewport()->mapToGlobal(view->mapFromScene(from));
    const QPoint screenTo = view->viewport()->mapToGlobal(view->mapFromScene(to));

    QGraphicsSceneMouseEvent press(QEvent::GraphicsSceneMousePress);
    press.setScenePos(from); press.setScreenPos(screenFrom);
    press.setButton(Qt::LeftButton); press.setButtons(Qt::LeftButton);
    QApplication::sendEvent(scene, &press);

    QGraphicsSceneMouseEvent move(QEvent::GraphicsSceneMouseMove);
    move.setScenePos(to); move.setScreenPos(screenTo);
    move.setButtons(Qt::LeftButton);
    QApplication::sendEvent(scene, &move);

    QGraphicsSceneMouseEvent release(QEvent::GraphicsSceneMouseRelease);
    release.setScenePos(to); release.setScreenPos(screenTo);
    release.setButton(Qt::LeftButton);
    QApplication::sendEvent(scene, &release);
    settle();
}

TEST(PhysMove, Behaves)
{
    MainWindow window;
    window.resize(1200, 800);
    window.show();
    settle();

    auto *scene = window.findChild<CanvasScene *>();
    auto *view = window.findChild<QGraphicsView *>(QStringLiteral("canvasView"));
    auto *sim = window.findChild<SimulationController *>();
    scene->setSnapToGrid(false);

    // A two-shape body, so a drag has to carry both.
    auto *left = new RectangleItem;
    left->setRect(QRectF(0, 0, 80, 40)); left->setPos(-100, 0);
    left->setName(QStringLiteral("left"));
    auto *right = new RectangleItem;
    right->setRect(QRectF(0, 0, 80, 40)); right->setPos(20, 0);
    right->setName(QStringLiteral("right"));
    scene->addItem(left); scene->addItem(right);
    scene->notifyShapesChanged();
    scene->setEditorMode(EditorMode::Physics);
    settle();

    scene->selectForPhysics(left, true);
    scene->selectForPhysics(right, true);
    scene->createBodyFromSelection();
    scene->clearPhysicsSelection();
    settle();

    const QPointF leftStart = left->pos();
    const QPointF rightStart = right->pos();
    const QPointF grab = left->sceneBoundingRect().center();
    drag(scene, view, grab, grab + QPointF(150, 60));

    EXPECT_TRUE(left->pos() != leftStart) << "the shape moved";
    EXPECT_TRUE(right->pos() != rightStart) << "the rest of its body came too";
    EXPECT_TRUE(qFuzzyCompare((right->pos() - left->pos()).x(),
                        (rightStart - leftStart).x())) << "and they kept their spacing";

    const QPoint before(view->horizontalScrollBar()->value(),
                        view->verticalScrollBar()->value());
    const QPointF nowhere(-900, -400);
    const QPointF held = left->pos();
    drag(scene, view, nowhere, nowhere + QPointF(80, 40));
    const QPoint after(view->horizontalScrollBar()->value(),
                       view->verticalScrollBar()->value());
    EXPECT_TRUE(before != after) << "the view scrolled" << " -- " << (QStringLiteral("(%1,%2) -> (%3,%4)").arg(before.x()).arg(before.y())
              .arg(after.x()).arg(after.y())).toStdString();
    EXPECT_TRUE(left->pos() == held) << "and nothing was dragged";

    sim->start();
    for (int f = 0; f < 20; ++f) sim->stepFrame();
    settle();
    const QPointF running = left->pos();
    drag(scene, view, left->sceneBoundingRect().center(),
         left->sceneBoundingRect().center() + QPointF(200, 0));
    // The sim keeps stepping it, so compare against a drag-sized jump.
    EXPECT_TRUE(qAbs((left->pos() - running).x()) < 100.0) << "a drag does not teleport it" << " -- " << (QStringLiteral("moved %1 px in x").arg((left->pos() - running).x(), 0, 'f', 0)).toStdString();
    sim->stop();
    settle();

    window.close();
}
