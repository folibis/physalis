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
#include "EditorMode.h"
#include "MainWindow.h"
#include <QAction>
#include <QCoreApplication>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsView>
#include <QKeyEvent>

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

namespace {

// Three rectangles at different places and sizes, the lead first.
QVector<ShapeItem *> threeShapes(CanvasScene *scene)
{
    QVector<ShapeItem *> made;
    const QList<QRectF> places { QRectF(0, 0, 40, 40), QRectF(0, 0, 100, 20),
                                 QRectF(0, 0, 60, 80) };
    const QList<QPointF> at { QPointF(10, 10), QPointF(200, 60), QPointF(120, 300) };
    for (int i = 0; i < places.size(); ++i) {
        auto *shape = new RectangleItem;
        shape->setRect(places.at(i));
        shape->setPos(at.at(i));
        shape->setName(QStringLiteral("shape_%1").arg(i + 1));
        scene->addItem(shape);
        made << shape;
    }
    scene->notifyShapesChanged();
    return made;
}

QRectF boxOf(ShapeItem *shape)
{
    return shape->mapToScene(shape->rect()).boundingRect();
}

} // namespace

// Every shape is lined up on one edge of the box they all sit in, and nothing
// else about them changes: the box they cover keeps its size, and the shapes
// that already sat on that edge do not move.
TEST(Alignment, EveryShapeMeetsTheChosenEdge)
{
    CanvasScene scene;
    scene.setEditorMode(EditorMode::Edit);
    const QVector<ShapeItem *> shapes = threeShapes(&scene);
    scene.selectShape(shapes.at(0));
    for (int i = 1; i < shapes.size(); ++i)
        scene.addToEditSelection(shapes.at(i));
    ASSERT_EQ(scene.selectedShapes().size(), 3);

    QRectF all;
    QVector<QSizeF> sizes;
    for (ShapeItem *shape : shapes) {
        all = all.isNull() ? boxOf(shape) : all.united(boxOf(shape));
        sizes << boxOf(shape).size();
    }

    struct Case {
        CanvasScene::Align edge;
        const char *what;
    };
    static const Case cases[] {
        { CanvasScene::Align::Left, "left" },
        { CanvasScene::Align::HorizontalCentre, "centre across" },
        { CanvasScene::Align::Right, "right" },
        { CanvasScene::Align::Top, "top" },
        { CanvasScene::Align::VerticalCentre, "centre down" },
        { CanvasScene::Align::Bottom, "bottom" },
    };

    for (const Case &one : cases) {
        CanvasScene fresh;
        fresh.setEditorMode(EditorMode::Edit);
        const QVector<ShapeItem *> again = threeShapes(&fresh);
        fresh.selectShape(again.at(0));
        for (int i = 1; i < again.size(); ++i)
            fresh.addToEditSelection(again.at(i));

        fresh.alignSelection(one.edge);

        for (int i = 0; i < again.size(); ++i) {
            const QRectF box = boxOf(again.at(i));
            EXPECT_NEAR(box.width(), sizes.at(i).width(), 0.001)
                << one.what << " resized " << again.at(i)->name().toStdString();
            EXPECT_NEAR(box.height(), sizes.at(i).height(), 0.001)
                << one.what << " resized " << again.at(i)->name().toStdString();
            switch (one.edge) {
            case CanvasScene::Align::Left:
                EXPECT_NEAR(box.left(), all.left(), 0.001) << one.what;
                break;
            case CanvasScene::Align::HorizontalCentre:
                EXPECT_NEAR(box.center().x(), all.center().x(), 0.001) << one.what;
                break;
            case CanvasScene::Align::Right:
                EXPECT_NEAR(box.right(), all.right(), 0.001) << one.what;
                break;
            case CanvasScene::Align::Top:
                EXPECT_NEAR(box.top(), all.top(), 0.001) << one.what;
                break;
            case CanvasScene::Align::VerticalCentre:
                EXPECT_NEAR(box.center().y(), all.center().y(), 0.001) << one.what;
                break;
            case CanvasScene::Align::Bottom:
                EXPECT_NEAR(box.bottom(), all.bottom(), 0.001) << one.what;
                break;
            }
        }
    }
}

// Lining up across does not move anything down, and the other way about: a
// shape pushed left keeps the height it was at.
TEST(Alignment, OneDirectionLeavesTheOtherAlone)
{
    CanvasScene scene;
    scene.setEditorMode(EditorMode::Edit);
    const QVector<ShapeItem *> shapes = threeShapes(&scene);
    scene.selectShape(shapes.at(0));
    for (int i = 1; i < shapes.size(); ++i)
        scene.addToEditSelection(shapes.at(i));

    QVector<qreal> tops;
    for (ShapeItem *shape : shapes)
        tops << boxOf(shape).top();

    scene.alignSelection(CanvasScene::Align::Left);
    for (int i = 0; i < shapes.size(); ++i) {
        EXPECT_NEAR(boxOf(shapes.at(i)).top(), tops.at(i), 0.001)
            << shapes.at(i)->name().toStdString() << " moved down as well as across";
    }
}

// Nothing to line up against: one shape, or none, and the button is off.
TEST(Alignment, OneShapeIsNothingToLineUp)
{
    CanvasScene scene;
    scene.setEditorMode(EditorMode::Edit);
    const QVector<ShapeItem *> shapes = threeShapes(&scene);

    EXPECT_FALSE(scene.canAlignSelection()) << "nothing is picked and aligning is offered";

    scene.selectShape(shapes.at(0));
    EXPECT_FALSE(scene.canAlignSelection()) << "one shape is picked and aligning is offered";

    const QRectF before = boxOf(shapes.at(0));
    scene.alignSelection(CanvasScene::Align::Right);
    EXPECT_EQ(boxOf(shapes.at(0)), before) << "a lone shape was moved by aligning";

    scene.addToEditSelection(shapes.at(1));
    EXPECT_TRUE(scene.canAlignSelection()) << "two shapes are picked and aligning is not offered";
}

// The button, not just the scene behind it. Picking a second shape does not
// change which one is active, so the toolbar has to be told about it separately
// -- and it was not: the enabling ran only when the editor mode changed, so
// selecting two shapes left Align greyed out with nothing to say why.
TEST(Alignment, TheButtonFollowsTheSelection)
{
    MainWindow window;
    auto *scene = window.findChild<CanvasScene *>();
    ASSERT_NE(scene, nullptr) << "the window has no scene";
    auto *align = window.findChild<QAction *>(QStringLiteral("actionAlign"));
    ASSERT_NE(align, nullptr) << "the window has no Align action";

    scene->setEditorMode(EditorMode::Edit);
    const QVector<ShapeItem *> shapes = threeShapes(scene);

    scene->selectShape(shapes.at(0));
    EXPECT_FALSE(align->isEnabled()) << "one shape is picked and Align is offered";

    scene->addToEditSelection(shapes.at(1));
    EXPECT_TRUE(align->isEnabled())
        << "two shapes are picked and Align is still greyed out";

    scene->addToEditSelection(shapes.at(2));
    EXPECT_TRUE(align->isEnabled()) << "three shapes are picked and Align is greyed out";

    // Clicking a different shape starts a new selection of one. (Clicking the
    // one already active keeps the others, which is how a shape under another
    // stays reachable, so that is not the way back to one.)
    scene->selectShape(shapes.at(1));
    EXPECT_FALSE(align->isEnabled())
        << "the selection came back to one shape and Align stayed on";

    scene->addToEditSelection(shapes.at(2));
    ASSERT_TRUE(align->isEnabled());
    scene->clearEditSelection();
    EXPECT_FALSE(align->isEnabled())
        << "the extra shapes were dropped and Align stayed on";
}

namespace {

// A press, a move and a release at scene positions, with whatever modifiers.
void press(CanvasScene *scene, const QPointF &at, Qt::KeyboardModifiers mods)
{
    QGraphicsSceneMouseEvent event(QEvent::GraphicsSceneMousePress);
    event.setScenePos(at);
    event.setButton(Qt::LeftButton);
    event.setButtons(Qt::LeftButton);
    event.setModifiers(mods);
    QCoreApplication::sendEvent(scene, &event);
}

void moveTo(CanvasScene *scene, const QPointF &at, Qt::KeyboardModifiers mods)
{
    QGraphicsSceneMouseEvent event(QEvent::GraphicsSceneMouseMove);
    event.setScenePos(at);
    event.setButtons(Qt::LeftButton);
    event.setModifiers(mods);
    QCoreApplication::sendEvent(scene, &event);
}

void release(CanvasScene *scene, const QPointF &at, Qt::KeyboardModifiers mods)
{
    QGraphicsSceneMouseEvent event(QEvent::GraphicsSceneMouseRelease);
    event.setScenePos(at);
    event.setButton(Qt::LeftButton);
    event.setModifiers(mods);
    QCoreApplication::sendEvent(scene, &event);
}

} // namespace

// Shift means two things: add a shape to the selection, and suspend snapping
// while dragging. A press on a resize handle can only be the second -- so
// holding Shift before grabbing one used to pick the shape underneath instead,
// and the drag never began. The only way to drag without snapping was to start
// the drag and press Shift afterwards.
TEST(ShiftDrag, ShiftOnAResizeHandleStartsTheDrag)
{
    CanvasScene scene;
    scene.setEditorMode(EditorMode::Edit);

    auto *box = new RectangleItem;
    box->setRect(QRectF(0, 0, 100, 60));
    box->setPos(0, 0);
    box->setName(QStringLiteral("box"));
    scene.addItem(box);
    scene.notifyShapesChanged();
    scene.selectShape(box);
    ASSERT_EQ(box->mode(), ShapeMode::Selected);

    // The bottom-right handle, in scene coordinates.
    const QRectF handle = box->handleRect(HandleId::BottomRight);
    ASSERT_FALSE(handle.isNull());
    const QPointF grab = box->mapToScene(handle.center());
    ASSERT_NE(box->handleAt(box->mapFromScene(grab)), HandleId::None)
        << "the test is not aiming at a handle";

    const QSizeF before = box->rect().size();
    press(&scene, grab, Qt::ShiftModifier);
    moveTo(&scene, grab + QPointF(40, 30), Qt::ShiftModifier);
    release(&scene, grab + QPointF(40, 30), Qt::ShiftModifier);

    EXPECT_NE(box->rect().size(), before)
        << "Shift was held before grabbing the handle and the shape never resized";
    EXPECT_TRUE(scene.selectedShapes().size() == 1)
        << "Shift on a handle picked another shape instead of resizing";
}

// The same for a polygon's nodes, where Shift also picks vertices: a Shift drag
// moves the node, and a Shift click that never moved still picks it.
TEST(ShiftDrag, ShiftDragsANodeAndShiftClickStillPicksOne)
{
    CanvasScene scene;
    scene.setEditorMode(EditorMode::Edit);

    QPolygonF outline;
    outline << QPointF(0, 0) << QPointF(100, 0) << QPointF(100, 80) << QPointF(0, 80);
    auto *shape = new PolygonItem(outline, true);
    shape->setName(QStringLiteral("poly"));
    scene.addItem(shape);
    scene.notifyShapesChanged();
    scene.selectShape(shape);
    scene.switchActiveToEditing();
    ASSERT_EQ(shape->mode(), ShapeMode::Editing);

    const QPointF node = shape->nodePosition(1);
    const QPointF grab = shape->mapToScene(node);

    press(&scene, grab, Qt::ShiftModifier);
    moveTo(&scene, grab + QPointF(25, 15), Qt::ShiftModifier);
    release(&scene, grab + QPointF(25, 15), Qt::ShiftModifier);

    EXPECT_NE(shape->nodePosition(1), node)
        << "Shift was held and the node never moved";

    // And a Shift click that does not move is still how a node is picked. There
    // is no way to ask which nodes are picked, so the test asks what that means:
    // a second node joins the selection, and dragging one then moves both.
    const QPointF second = shape->nodePosition(2);
    press(&scene, shape->mapToScene(second), Qt::ShiftModifier);
    release(&scene, shape->mapToScene(second), Qt::ShiftModifier);

    const QPointF movedNode = shape->nodePosition(1);
    const QPointF from = shape->mapToScene(movedNode);
    press(&scene, from, Qt::NoModifier);
    moveTo(&scene, from + QPointF(30, 0), Qt::NoModifier);
    release(&scene, from + QPointF(30, 0), Qt::NoModifier);

    EXPECT_NE(shape->nodePosition(2), second)
        << "a Shift click did not add the node to the selection: dragging another"
           " node left it behind";
}

namespace {

// Clicking a node picks it; Shift-clicking adds another. Driven through the
// scene's own mouse handling rather than a back door, so the picking under test
// is the picking a person does.
void clickNode(CanvasScene *scene, ShapeItem *shape, int index, Qt::KeyboardModifiers mods)
{
    const QPointF at = shape->mapToScene(shape->nodePosition(index));
    QGraphicsSceneMouseEvent down(QEvent::GraphicsSceneMousePress);
    down.setScenePos(at);
    down.setButton(Qt::LeftButton);
    down.setButtons(Qt::LeftButton);
    down.setModifiers(mods);
    QCoreApplication::sendEvent(scene, &down);

    QGraphicsSceneMouseEvent up(QEvent::GraphicsSceneMouseRelease);
    up.setScenePos(at);
    up.setButton(Qt::LeftButton);
    up.setModifiers(mods);
    QCoreApplication::sendEvent(scene, &up);
}

// A square, in node editing, with the given nodes picked.
PolygonItem *squareWith(CanvasScene *scene, const QList<int> &picked, bool closed = true)
{
    scene->setEditorMode(EditorMode::Edit);
    QPolygonF outline;
    outline << QPointF(0, 0) << QPointF(100, 0) << QPointF(100, 100) << QPointF(0, 100);
    auto *shape = new PolygonItem(outline, closed);
    shape->setName(QStringLiteral("poly"));
    scene->addItem(shape);
    scene->notifyShapesChanged();
    scene->selectShape(shape);
    scene->switchActiveToEditing();
    for (int at = 0; at < picked.size(); ++at)
        clickNode(scene, shape, picked.at(at), at == 0 ? Qt::NoModifier : Qt::ShiftModifier);
    return shape;
}

void enterWith(CanvasScene *scene, Qt::KeyboardModifiers mods)
{
    QKeyEvent event(QEvent::KeyPress, Qt::Key_Return, mods);
    QCoreApplication::sendEvent(scene, &event);
}

void ctrlEnter(CanvasScene *scene) { enterWith(scene, Qt::ShiftModifier); }

} // namespace

// Ctrl+Enter puts a node in the middle of every edge the picked nodes span.
// Picking neighbours splits the edges between them, one each.
TEST(Subdivide, NeighbouringNodesSplitTheEdgesBetweenThem)
{
    CanvasScene scene;
    PolygonItem *shape = squareWith(&scene, QList<int> { 0, 1, 2 });
    ASSERT_EQ(shape->nodeCount(), 4);

    ctrlEnter(&scene);

    EXPECT_EQ(shape->nodeCount(), 6)
        << "three nodes in a row span two edges, so two nodes should have been added";
    // The new ones sit between the old, in order, so the shape keeps its outline.
    EXPECT_EQ(shape->nodePosition(1), QPointF(50, 0)) << "the first edge was not halved";
    EXPECT_EQ(shape->nodePosition(3), QPointF(100, 50)) << "the second edge was not halved";
}

// And two nodes with others between them split the whole run, picked or not --
// which is the case plain Enter will not touch.
TEST(Subdivide, TwoNodesSplitEverythingBetweenThem)
{
    CanvasScene scene;
    PolygonItem *shape = squareWith(&scene, QList<int> { 0, 3 });
    ASSERT_EQ(shape->nodeCount(), 4);

    ctrlEnter(&scene);

    EXPECT_EQ(shape->nodeCount(), 7)
        << "the run from the first node to the last covers three edges, so three"
           " nodes should have been added";
}

// An open run reads the same way: the nodes between the two picked ones are
// split whether or not they were picked themselves.
TEST(Subdivide, AnOpenRunSplitsAlongItsLength)
{
    CanvasScene scene;
    PolygonItem *line = squareWith(&scene, QList<int> { 0, 3 }, false);
    ASSERT_FALSE(line->isClosed());
    ctrlEnter(&scene);
    EXPECT_EQ(line->nodeCount(), 7) << "an open run was not split along its length";
}

// One node, or none, is nothing to span.
TEST(Subdivide, OneNodeSplitsNothing)
{
    CanvasScene scene;
    PolygonItem *shape = squareWith(&scene, QList<int> { 2 });
    ctrlEnter(&scene);
    EXPECT_EQ(shape->nodeCount(), 4) << "a single picked node added one anyway";

    // Clicking inside the shape but away from every node drops the selection.
    QGraphicsSceneMouseEvent inside(QEvent::GraphicsSceneMousePress);
    inside.setScenePos(shape->mapToScene(QPointF(50, 50)));
    inside.setButton(Qt::LeftButton);
    inside.setButtons(Qt::LeftButton);
    QCoreApplication::sendEvent(&scene, &inside);
    ctrlEnter(&scene);
    EXPECT_EQ(shape->nodeCount(), 4) << "nothing picked added one anyway";
}

// Between any two nodes of a closed outline there are two runs, so which one is
// meant comes from the order they were picked: it starts at the one picked
// first and goes clockwise. Picking the same two the other way round splits the
// other half.
TEST(Subdivide, TheRunStartsAtTheNodePickedFirst)
{
    const auto splitFrom = [](int first, int second) {
        CanvasScene scene;
        PolygonItem *shape = squareWith(&scene, QList<int> { first, second });
        ctrlEnter(&scene);
        QList<QPointF> made;
        for (int at = 0; at < shape->nodeCount(); ++at)
            made << shape->nodePosition(at);
        return made;
    };

    // The square runs (0,0) (100,0) (100,100) (0,100), which is clockwise on a
    // canvas whose y grows downwards.
    const QList<QPointF> topFirst = splitFrom(0, 2);
    EXPECT_TRUE(topFirst.contains(QPointF(50, 0)))
        << "picking the top-left first should split the top edge";
    EXPECT_TRUE(topFirst.contains(QPointF(100, 50)))
        << "picking the top-left first should split the right edge";
    EXPECT_FALSE(topFirst.contains(QPointF(50, 100)))
        << "the bottom edge is the other way round and should not have been split";

    const QList<QPointF> bottomFirst = splitFrom(2, 0);
    EXPECT_TRUE(bottomFirst.contains(QPointF(50, 100)))
        << "picking the bottom-right first should split the bottom edge";
    EXPECT_TRUE(bottomFirst.contains(QPointF(0, 50)))
        << "picking the bottom-right first should split the left edge";
    EXPECT_FALSE(bottomFirst.contains(QPointF(50, 0)))
        << "the top edge is the other way round and should not have been split";
}

// And clockwise means clockwise on screen, whichever way round the outline
// happens to store its points: the same two corners give the same two edges.
TEST(Subdivide, ClockwiseIsClockwiseWhicheverWayThePointsRun)
{
    CanvasScene scene;
    scene.setEditorMode(EditorMode::Edit);
    // The same square, wound the other way.
    QPolygonF widdershins;
    widdershins << QPointF(0, 0) << QPointF(0, 100) << QPointF(100, 100) << QPointF(100, 0);
    auto *shape = new PolygonItem(widdershins, true);
    shape->setName(QStringLiteral("poly"));
    scene.addItem(shape);
    scene.notifyShapesChanged();
    scene.selectShape(shape);
    scene.switchActiveToEditing();
    // The top-left corner first, then the bottom-right: node 0 and node 2 again.
    clickNode(&scene, shape, 0, Qt::NoModifier);
    clickNode(&scene, shape, 2, Qt::ShiftModifier);

    ctrlEnter(&scene);

    QList<QPointF> made;
    for (int at = 0; at < shape->nodeCount(); ++at)
        made << shape->nodePosition(at);
    EXPECT_TRUE(made.contains(QPointF(50, 0)))
        << "going clockwise from the top-left should still split the top edge";
    EXPECT_TRUE(made.contains(QPointF(100, 50)))
        << "going clockwise from the top-left should still split the right edge";
    EXPECT_FALSE(made.contains(QPointF(50, 100)))
        << "it went anticlockwise because the points are stored that way";
}

// Shift+Enter is the key, the same as the one that closes a polygon while it is
// being drawn. Plain Enter keeps its own job: one node between two neighbours,
// or closing an unclosed outline.
TEST(Subdivide, ShiftEnterSplitsAndPlainEnterDoesNot)
{
    CanvasScene scene;
    PolygonItem *shape = squareWith(&scene, QList<int> { 0, 1, 2 });
    ASSERT_EQ(shape->nodeCount(), 4);

    enterWith(&scene, Qt::NoModifier);
    EXPECT_EQ(shape->nodeCount(), 4)
        << "plain Enter split the run, which is Shift+Enter's job";

    enterWith(&scene, Qt::ShiftModifier);
    EXPECT_EQ(shape->nodeCount(), 6)
        << "three nodes in a row span two edges and Shift+Enter added none";
}
