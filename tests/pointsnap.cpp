#include "CanvasScene.h"
#include "ExplosionItem.h"
#include "RayItem.h"

#include <QApplication>
#include <QGraphicsSceneMouseEvent>
#include <gtest/gtest.h>

namespace {

// Drags whatever sits at `from` to `to`, the way a mouse does.
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
}

} // namespace

// Snapping applied to shapes and left points alone: a blast or a rangefinder
// dragged across the canvas landed wherever the mouse happened to be, however
// the grid was set. They are points, so the point itself is what snaps.
TEST(PointSnap, ABlastAndARayLandOnTheGrid)
{
    CanvasScene scene;
    scene.setEditorMode(EditorMode::Physics);
    scene.setSnapToGrid(true);
    scene.setSnapStep(20.0);
    scene.setSnapSensitivity(10.0);

    ExplosionItem *blast = scene.addExplosion(QPointF(0, 0));
    ASSERT_TRUE(blast);
    drag(&scene, QPointF(0, 0), QPointF(97, 63));
    EXPECT_DOUBLE_EQ(blast->pos().x(), 100.0) << "the blast snapped across";
    EXPECT_DOUBLE_EQ(blast->pos().y(), 60.0) << "and down";

    RayItem *ray = scene.addRay(QPointF(0, 200));
    ASSERT_TRUE(ray);
    drag(&scene, QPointF(0, 200), QPointF(58, 243));
    EXPECT_DOUBLE_EQ(ray->pos().x(), 60.0) << "so does a rangefinder";
    EXPECT_DOUBLE_EQ(ray->pos().y(), 240.0);
}

TEST(PointSnap, WithSnapOffTheyGoWhereTheyArePut)
{
    CanvasScene scene;
    scene.setEditorMode(EditorMode::Physics);
    scene.setSnapToGrid(false);

    ExplosionItem *blast = scene.addExplosion(QPointF(0, 0));
    ASSERT_TRUE(blast);
    drag(&scene, QPointF(0, 0), QPointF(97, 63));
    EXPECT_DOUBLE_EQ(blast->pos().x(), 97.0);
    EXPECT_DOUBLE_EQ(blast->pos().y(), 63.0);
}
