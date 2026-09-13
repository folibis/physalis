#include "CanvasScene.h"
#include "PolygonItem.h"

#include <QApplication>
#include <QGraphicsSceneMouseEvent>
#include <QKeyEvent>
#include <gtest/gtest.h>

namespace {

void click(CanvasScene *scene, const QPointF &at)
{
    QGraphicsSceneMouseEvent press(QEvent::GraphicsSceneMousePress);
    press.setScenePos(at);
    press.setButton(Qt::LeftButton);
    press.setButtons(Qt::LeftButton);
    QApplication::sendEvent(scene, &press);

    QGraphicsSceneMouseEvent release(QEvent::GraphicsSceneMouseRelease);
    release.setScenePos(at);
    release.setButton(Qt::LeftButton);
    QApplication::sendEvent(scene, &release);
}

// Draws a triangle and finishes it with Enter and the given modifiers.
PolygonItem *drawTriangle(CanvasScene *scene, Qt::KeyboardModifiers modifiers)
{
    scene->startPolygonDrawing();
    click(scene, QPointF(0, 0));
    click(scene, QPointF(100, 0));
    click(scene, QPointF(50, 80));

    QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, modifiers);
    QApplication::sendEvent(scene, &enter);

    PolygonItem *made = nullptr;
    for (QGraphicsItem *item : scene->items()) {
        if (auto *polygon = dynamic_cast<PolygonItem *>(item))
            made = polygon;
    }
    return made;
}

} // namespace

// Shift+Enter closes the outline being drawn; Enter alone leaves it open.
// Ctrl no longer does anything here -- multi-select moved to Shift as well, so
// Shift is the one modifier the editor asks for.
TEST(PolygonClose, ShiftEnterClosesIt)
{
    {
        CanvasScene scene;
        scene.setSnapToGrid(false);
        PolygonItem *polygon = drawTriangle(&scene, Qt::ShiftModifier);
        ASSERT_NE(polygon, nullptr) << "the outline was made";
        EXPECT_TRUE(polygon->isClosed()) << "Shift+Enter closes it";
    }
    {
        CanvasScene scene;
        scene.setSnapToGrid(false);
        PolygonItem *polygon = drawTriangle(&scene, Qt::NoModifier);
        ASSERT_NE(polygon, nullptr);
        EXPECT_FALSE(polygon->isClosed()) << "Enter alone leaves it open";
    }
    {
        CanvasScene scene;
        scene.setSnapToGrid(false);
        PolygonItem *polygon = drawTriangle(&scene, Qt::ControlModifier);
        ASSERT_NE(polygon, nullptr);
        EXPECT_FALSE(polygon->isClosed()) << "Ctrl+Enter is not the way to close it any more";
    }
}
