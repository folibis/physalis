// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ruslan Muhlinin. See LICENSE.
#include "CanvasScene.h"
#include "CircleItem.h"
#include "PhysicsBody.h"
#include "PolygonItem.h"
#include "RectangleItem.h"
#include "SceneSerializer.h"
#include "ShapeItem.h"
#include "SimulationController.h"

#include <QJsonObject>
#include <gtest/gtest.h>

// Drawing and editing shapes: what the canvas is for. Every kind the editor
// can make, every edit it can do to one, and in each case the three things
// that have to stay true -- the shape is what the canvas draws, the engine is
// given the same shape, and the file gives it back.

namespace {

// The shapes a user can draw, each as the editor makes it.
struct Drawn {
    const char *what;
    std::function<ShapeItem *(CanvasScene &)> make;
    physics::GeometryKind kind;
};

const QVector<Drawn> &everyKind()
{
    static const QVector<Drawn> kinds {
        { "rectangle",
          [](CanvasScene &scene) -> ShapeItem * {
              ShapeItem *shape = scene.addRectangle(QPointF(0, 0));
              shape->setRect(QRectF(0, 0, 80, 40));
              return shape;
          },
          physics::GeometryKind::Box },
        { "circle",
          [](CanvasScene &scene) -> ShapeItem * {
              ShapeItem *shape = scene.addCircle(QPointF(0, 0));
              shape->setRect(QRectF(0, 0, 60, 60));
              return shape;
          },
          physics::GeometryKind::Circle },
        { "polygon",
          [](CanvasScene &scene) -> ShapeItem * {
              QPolygonF points;
              points << QPointF(0, 0) << QPointF(80, 0) << QPointF(80, 50) << QPointF(0, 50);
              auto *shape = new PolygonItem(points, true);
              shape->setName(QStringLiteral("polygon"));
              scene.addItem(shape);
              return shape;
          },
          physics::GeometryKind::Polygon },
        { "polyline",
          [](CanvasScene &scene) -> ShapeItem * {
              QPolygonF points;
              points << QPointF(0, 0) << QPointF(60, 20) << QPointF(120, 0);
              auto *shape = new PolygonItem(points, false);
              shape->setName(QStringLiteral("polyline"));
              scene.addItem(shape);
              return shape;
          },
          physics::GeometryKind::Chain },
        { "hollow polygon",
          [](CanvasScene &scene) -> ShapeItem * {
              QPolygonF points;
              points << QPointF(0, 0) << QPointF(90, 0) << QPointF(90, 60) << QPointF(0, 60);
              auto *shape = new PolygonItem(points, true);
              shape->setName(QStringLiteral("hollow"));
              shape->setPreferOutline(true);
              scene.addItem(shape);
              return shape;
          },
          physics::GeometryKind::Chain },
        { "smooth chain",
          [](CanvasScene &scene) -> ShapeItem * {
              QPolygonF points;
              points << QPointF(0, 0) << QPointF(60, 10) << QPointF(120, 0) << QPointF(180, 10);
              auto *shape = new PolygonItem(points, false);
              shape->setName(QStringLiteral("rail"));
              shape->setSmoothChain(true);
              scene.addItem(shape);
              return shape;
          },
          physics::GeometryKind::Chain },
    };
    return kinds;
}

PhysicsBody *bodyAround(CanvasScene &scene, ShapeItem *shape, physics::BodyType type)
{
    scene.notifyShapesChanged();
    scene.setEditorMode(EditorMode::Physics);
    scene.selectForPhysics(shape, true);
    PhysicsBody *body = scene.createBodyFromSelection();
    if (body)
        body->props().type = type;
    scene.clearPhysicsSelection();
    return body;
}

} // namespace

// Each kind draws as something with area on the canvas and reaches the engine
// as the geometry it is, not as a bounding box.
TEST(Shapes, EveryKindIsDrawnAndBuilt)
{
    for (const Drawn &kind : everyKind()) {
        CanvasScene scene;
        scene.setSimulationEngineName(QStringLiteral("Box2D"));
        ShapeItem *shape = kind.make(scene);
        ASSERT_TRUE(shape) << kind.what << " could not be drawn";

        EXPECT_FALSE(shape->sceneBoundingRect().isEmpty()) << kind.what << " has nothing on screen";
        EXPECT_EQ(shape->physicsGeometry().kind, kind.kind)
            << kind.what << " reaches the engine as the wrong kind of geometry";

        // An outline has no area, so it can only be scenery; everything else
        // can fall. Either way the engine has to take it.
        const bool solid = shape->hasInterior();
        PhysicsBody *body = bodyAround(scene, shape,
                                       solid ? physics::BodyType::Dynamic
                                             : physics::BodyType::Static);
        ASSERT_TRUE(body) << kind.what << " could not be made into a body";

        SimulationController sim(&scene, nullptr);
        sim.setEngineName(QStringLiteral("Box2D"));
        sim.start();
        EXPECT_TRUE(sim.skippedBodies().isEmpty())
            << kind.what << " was skipped by the engine: "
            << sim.skippedBodies().join(QLatin1Char(',')).toStdString();
        EXPECT_TRUE(sim.readValue(shape->name(), QStringLiteral("friction")).isValid())
            << kind.what << " is not in the world under its own name";
        sim.stop();
    }
}

// Every kind, with everything about it, through the file and back.
TEST(Shapes, EveryKindSurvivesTheFile)
{
    for (const Drawn &kind : everyKind()) {
        CanvasScene scene;
        scene.setSimulationEngineName(QStringLiteral("Box2D"));
        ShapeItem *shape = kind.make(scene);
        ASSERT_TRUE(shape);
        shape->setPos(120, -60);
        shape->setRotation(30);
        scene.notifyShapesChanged();

        const QString name = shape->name();
        const physics::Geometry before = shape->physicsGeometry();

        const QJsonObject document = SceneSerializer::save(&scene);
        CanvasScene reopened;
        QString error;
        ASSERT_TRUE(SceneSerializer::load(&reopened, document, &error)) << error.toStdString();
        ASSERT_EQ(reopened.shapes().size(), 1) << kind.what << " did not survive the file";

        ShapeItem *loaded = reopened.shapes().first();
        const physics::Geometry after = loaded->physicsGeometry();
        EXPECT_EQ(loaded->name(), name) << kind.what << " came back under another name";
        EXPECT_EQ(int(after.kind), int(before.kind)) << kind.what << " came back as another kind";
        EXPECT_EQ(after.points.size(), before.points.size()) << kind.what << " lost points";
        EXPECT_EQ(after.closed, before.closed) << kind.what << " opened or closed itself";
        EXPECT_EQ(after.smoothChain, before.smoothChain) << kind.what << " changed how it is built";
        EXPECT_NEAR(loaded->rotation(), 30.0, 1e-6) << kind.what << " lost its angle";
        EXPECT_NEAR(loaded->pos().x(), 120.0, 1e-6) << kind.what << " lost where it was";
        EXPECT_NEAR(loaded->pos().y(), -60.0, 1e-6);
    }
}

// Dragging a corner resizes, and the engine is built from the size on screen
// rather than the size it was drawn at.
TEST(Shapes, ResizingChangesWhatTheEngineIsGiven)
{
    CanvasScene scene;
    scene.setSimulationEngineName(QStringLiteral("Box2D"));
    RectangleItem *shape = scene.addRectangle(QPointF(0, 0));
    shape->setRect(QRectF(0, 0, 80, 40));
    scene.notifyShapesChanged();

    const QPointF wasHalf = shape->physicsGeometry().halfExtents;
    shape->resizeByHandle(HandleId::BottomRight, QPointF(160, 120));
    const QPointF nowHalf = shape->physicsGeometry().halfExtents;

    EXPECT_GT(nowHalf.x(), wasHalf.x()) << "dragging the corner out made it wider";
    EXPECT_GT(nowHalf.y(), wasHalf.y()) << "and taller";
    EXPECT_NEAR(nowHalf.x() * 2.0, shape->rect().width(), 1e-6)
        << "the engine is given the size that is on screen";
    EXPECT_NEAR(nowHalf.y() * 2.0, shape->rect().height(), 1e-6);
}

// A polygon's points can be moved, added and taken away, and each edit is what
// the engine is then built from.
TEST(Shapes, PolygonPointsCanBeEdited)
{
    CanvasScene scene;
    scene.setSimulationEngineName(QStringLiteral("Box2D"));
    QPolygonF points;
    points << QPointF(0, 0) << QPointF(100, 0) << QPointF(100, 80) << QPointF(0, 80);
    auto *shape = new PolygonItem(points, true);
    shape->setName(QStringLiteral("plate"));
    scene.addItem(shape);
    scene.notifyShapesChanged();

    ASSERT_EQ(shape->nodeCount(), 4);
    EXPECT_EQ(shape->physicsGeometry().points.size(), 4);

    shape->moveNode(1, QPointF(140, -20));
    EXPECT_NEAR(shape->nodePosition(1).x(), 140.0, 1e-6) << "the point went where it was put";
    EXPECT_NEAR(shape->nodePosition(1).y(), -20.0, 1e-6);

    shape->insertNodeBetween(0, 1);
    EXPECT_EQ(shape->nodeCount(), 5) << "a point can be added between two others";
    EXPECT_EQ(shape->physicsGeometry().points.size(), 5) << "and the engine is given it";

    shape->deleteNode(4);
    EXPECT_EQ(shape->nodeCount(), 4) << "and taken away again";
    EXPECT_EQ(shape->physicsGeometry().points.size(), 4);
}

// A rectangle converted to a polygon keeps its place and its size, and becomes
// something with points to edit.
TEST(Shapes, ARectangleConvertsToAPolygon)
{
    CanvasScene scene;
    scene.setSimulationEngineName(QStringLiteral("Box2D"));
    RectangleItem *rectangle = scene.addRectangle(QPointF(40, 20));
    rectangle->setRect(QRectF(0, 0, 100, 60));
    scene.notifyShapesChanged();
    const QRectF was = rectangle->sceneBoundingRect();

    ShapeItem *converted = scene.convertToPolygon(rectangle);
    ASSERT_TRUE(converted) << "the rectangle converted";
    EXPECT_TRUE(converted->supportsNodeEditing()) << "and has points to edit";
    EXPECT_EQ(converted->nodeCount(), 4) << "four of them";
    EXPECT_NEAR(converted->sceneBoundingRect().width(), was.width(), 1.0)
        << "and it is still the size it was";
    EXPECT_NEAR(converted->sceneBoundingRect().height(), was.height(), 1.0);
    EXPECT_EQ(scene.shapes().size(), 1) << "the rectangle is gone, not left underneath";
}
