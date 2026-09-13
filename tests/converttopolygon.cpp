#include "CanvasScene.h"
#include "PhysicsBody.h"
#include "PolygonItem.h"
#include "RectangleItem.h"
#include "ShapeItem.h"

#include <QApplication>
#include <gtest/gtest.h>

// Converting a rectangle to a polygon has to be a change of outline and
// nothing else: the shape keeps its name, so rules and the log that named it
// go on naming it; it keeps its place in its body, so the body's origin does
// not jump to a different shape; and it keeps its physics. What it loses is
// the corner radius, which only a rectangle has -- Box2D rounds a box without
// growing it, and there is no such rounding for an arbitrary polygon.
TEST(ConvertToPolygon, Behaves)
{
    CanvasScene scene;

    auto *first = new RectangleItem;
    first->setRect(QRectF(-60, -40, 120, 80));
    first->setPos(300, 200);
    first->setRotation(30.0);
    first->setName(QStringLiteral("plank"));
    first->setCornerRadius(12.0);
    first->setBodyColor(QColor(10, 20, 30, 200));
    first->setBorderWidth(3.5);
    first->part().params["density"] = 2.75;
    first->part().params["friction"] = 0.42;
    first->part().params["isSensor"] = true;
    first->part().params["enableContactEvents"] = true;
    scene.addItem(first);

    auto *second = new RectangleItem;
    second->setRect(QRectF(0, 0, 40, 40));
    second->setPos(500, 200);
    second->setName(QStringLiteral("block"));
    scene.addItem(second);
    scene.notifyShapesChanged();

    scene.setEditorMode(EditorMode::Physics);
    scene.selectForPhysics(first, true);
    scene.selectForPhysics(second, true);
    PhysicsBody *body = scene.createBodyFromSelection();
    scene.clearPhysicsSelection();
    ASSERT_TRUE(body != nullptr);
    ASSERT_EQ(body->shapes().size(), 2);
    ASSERT_EQ(body->shapes().first(), first) << "the plank is the body's reference shape";

    const QPointF bodyOrigin = body->originScenePos();
    const QRectF rect = first->rect();
    const QPointF pos = first->pos();
    const qreal rotation = first->rotation();
    const QPointF origin = first->origin();

    ShapeItem *converted = scene.convertToPolygon(first);
    ASSERT_TRUE(converted != nullptr);

    EXPECT_EQ(converted->typeName(), QStringLiteral("polygon")) << "it is a polygon now";
    EXPECT_EQ(converted->name(), QStringLiteral("plank")) << "under the same name";
    EXPECT_EQ(converted->pos(), pos) << "in the same place";
    EXPECT_DOUBLE_EQ(converted->rotation(), rotation) << "turned the same way";
    EXPECT_EQ(converted->origin(), origin) << "about the same origin";
    EXPECT_EQ(converted->rect(), rect) << "covering the same rect";

    auto *polygon = dynamic_cast<PolygonItem *>(converted);
    ASSERT_TRUE(polygon != nullptr);
    EXPECT_EQ(polygon->points().size(), 4) << "four corners, and no more";
    EXPECT_TRUE(polygon->isClosed()) << "closed, as a rectangle is";

    EXPECT_DOUBLE_EQ(converted->cornerRadius(), 0.0)
        << "the radius goes rather than lingering with no effect";
    EXPECT_DOUBLE_EQ(converted->part().params["density"].toDouble(), 2.75) << "the physics comes across";
    EXPECT_DOUBLE_EQ(converted->part().params["friction"].toDouble(), 0.42);
    EXPECT_TRUE(converted->part().params["isSensor"].toBool());
    EXPECT_TRUE(converted->part().params["enableContactEvents"].toBool());
    EXPECT_EQ(converted->borderWidth(), 3.5) << "and so does the look of it";
    EXPECT_EQ(converted->bodyColor(), QColor(10, 20, 30, 200));

    ASSERT_EQ(body->shapes().size(), 2) << "the body still has two shapes";
    EXPECT_EQ(converted->body(), body) << "and the new one belongs to it";
    EXPECT_EQ(body->shapes().first(), converted)
        << "in the slot the old one held, or the body's origin moves";
    EXPECT_EQ(body->originScenePos(), bodyOrigin) << "so the body has not shifted";

    const QVector<ShapeItem *> left = scene.shapes();
    EXPECT_EQ(left.size(), 2) << "the rectangle is gone, not merely detached";
    EXPECT_TRUE(left.contains(converted));

    // A polygon has no second conversion to make.
    EXPECT_TRUE(scene.convertToPolygon(converted) == nullptr)
        << "converting a polygon again does nothing";
}
