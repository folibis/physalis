// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ruslan Muhlinin. See LICENSE.
#include "CanvasScene.h"
#include "CircleItem.h"
#include "PhysicsBody.h"
#include "PolygonItem.h"
#include "RectangleItem.h"
#include "RayItem.h"
#include "ExplosionItem.h"
#include "SceneControl.h"
#include "SceneSerializer.h"
#include "ShapeItem.h"
#include "SimulationController.h"
#include "RunCopy.h"
#include "EngineRegistry.h"
#include "Joint.h"
#include "UndoStack.h"

#include "ToolOptionsBar.h"
#include <QDoubleSpinBox>
#include <QClipboard>
#include <QGuiApplication>
#include <QPainter>
#include <QImage>
#include <QPainterPath>
#include "PropertyPane/PropertyPane.h"
#include "PropertyPanel.h"
#include <memory>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <gtest/gtest.h>
#include "EditorMode.h"
#include "ClipboardProbe.h"
#include "MainWindow.h"
#include "SceneTree.h"
#include <QAction>
#include <QCoreApplication>
#include <QDir>
#include <QDropEvent>
#include <QLineEdit>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QElapsedTimer>
#include <QSignalSpy>
#include <QMenu>
#include <QFile>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsSceneWheelEvent>
#include <QGraphicsView>
#include <QKeyEvent>
#include <QMimeData>
#include <QTemporaryDir>
#include <QUrl>

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
// The commands the keys reach. Duplicate -- the one command the mechanism copy
// was written for -- was on a context menu only, with no key at all.
// Shapes used to stack by the order they were added, with no way to change it:
// zValue was only ever read, to copy it during a conversion, and the file
// carried nothing.
TEST(Editing, StackingOrderCanBeChangedAndIsSaved)
{
    MainWindow window;
    auto *scene = window.findChild<CanvasScene *>();
    ASSERT_NE(scene, nullptr);
    scene->setEditorMode(EditorMode::Edit);

    const QVector<ShapeItem *> shapes = threeShapes(scene);
    ASSERT_EQ(shapes.size(), 3);
    // Added in order, so that is the order they are painted in.
    EXPECT_EQ(scene->shapesInStackOrder(), shapes);

    const auto order = [scene] {
        QStringList names;
        for (ShapeItem *shape : scene->shapesInStackOrder())
            names << shape->name();
        return names.join(QLatin1Char(','));
    };

    // The bottom one to the top.
    ASSERT_TRUE(scene->restackShapes({ shapes.at(0) }, CanvasScene::Restack::ToFront));
    EXPECT_EQ(order(), QStringLiteral("shape_2,shape_3,shape_1"));

    // And one step at a time, back down.
    ASSERT_TRUE(scene->restackShapes({ shapes.at(0) }, CanvasScene::Restack::Backward));
    EXPECT_EQ(order(), QStringLiteral("shape_2,shape_1,shape_3"));
    ASSERT_TRUE(scene->restackShapes({ shapes.at(0) }, CanvasScene::Restack::ToBack));
    EXPECT_EQ(order(), QStringLiteral("shape_1,shape_2,shape_3"));

    // A move that changes nothing says so, so no undo state is recorded for it.
    EXPECT_FALSE(scene->restackShapes({ shapes.at(0) }, CanvasScene::Restack::Backward))
        << "the bottom shape claimed to move down";
    EXPECT_FALSE(scene->restackShapes({ shapes.at(2) }, CanvasScene::Restack::ToFront))
        << "the top shape claimed to move up";
    EXPECT_FALSE(scene->restackShapes({}, CanvasScene::Restack::ToFront));

    // Two picked shapes keep their own order and do not swap with each other.
    ASSERT_TRUE(scene->restackShapes({ shapes.at(0), shapes.at(1) },
                                     CanvasScene::Restack::ToFront));
    EXPECT_EQ(order(), QStringLiteral("shape_3,shape_1,shape_2"));

    // And the file gives the order back.
    QJsonObject document = SceneSerializer::save(scene);
    CanvasScene reloaded;
    QString error;
    ASSERT_TRUE(SceneSerializer::load(&reloaded, document, &error)) << qPrintable(error);
    QStringList back;
    for (ShapeItem *shape : reloaded.shapesInStackOrder())
        back << shape->name();
    EXPECT_EQ(back.join(QLatin1Char(',')), QStringLiteral("shape_3,shape_1,shape_2"))
        << "the saved scene came back in a different order";
}

// Rules address objects by name and there was no way to find one by name.
TEST(Editing, TheObjectsTreeCanBeFilteredByName)
{
    MainWindow window;
    auto *scene = window.findChild<CanvasScene *>();
    auto *tree = window.findChild<SceneTree *>();
    ASSERT_NE(scene, nullptr);
    ASSERT_NE(tree, nullptr);
    auto *filter = tree->findChild<QLineEdit *>();
    auto *rows = tree->findChild<QTreeWidget *>();
    ASSERT_NE(filter, nullptr) << "the Objects tab has no find box";
    ASSERT_NE(rows, nullptr);

    threeShapes(scene);
    auto *odd = new RectangleItem;
    odd->setRect(QRectF(0, 0, 20, 20));
    odd->setName(QStringLiteral("lever_arm"));
    scene->addItem(odd);
    scene->notifyShapesChanged();
    QCoreApplication::processEvents(); // the tree rebuilds queued

    const auto showing = [rows] {
        QStringList names;
        QTreeWidgetItemIterator it(rows, QTreeWidgetItemIterator::NotHidden);
        for (; *it; ++it)
            names << (*it)->text(0);
        return names;
    };

    ASSERT_TRUE(showing().contains(QStringLiteral("shape_1")));
    ASSERT_TRUE(showing().contains(QStringLiteral("lever_arm")));

    filter->setText(QStringLiteral("lever"));
    QStringList shown = showing();
    EXPECT_TRUE(shown.contains(QStringLiteral("lever_arm"))) << "the match was hidden";
    EXPECT_FALSE(shown.contains(QStringLiteral("shape_1"))) << "a shape that does not match stayed";
    // The group holding it has to survive, or the match is hidden with it.
    EXPECT_FALSE(shown.isEmpty());

    // Case does not matter, and a stem matches every shape sharing it.
    filter->setText(QStringLiteral("SHAPE_"));
    shown = showing();
    EXPECT_TRUE(shown.contains(QStringLiteral("shape_1")));
    EXPECT_TRUE(shown.contains(QStringLiteral("shape_3")));
    EXPECT_FALSE(shown.contains(QStringLiteral("lever_arm")));

    // Nothing matching leaves the tree empty of objects rather than unfiltered.
    filter->setText(QStringLiteral("no_such_thing"));
    for (const QString &name : showing())
        EXPECT_FALSE(name.contains(QStringLiteral("shape_")));

    // And clearing it puts everything back.
    filter->clear();
    shown = showing();
    EXPECT_TRUE(shown.contains(QStringLiteral("shape_1")));
    EXPECT_TRUE(shown.contains(QStringLiteral("lever_arm")));
}

// Names on the canvas, and the commands that were unreachable from the canvas:
// the empty-canvas menu offered Add and nothing else.
TEST(Editing, NamesAreOffAndTheCanvasMenuCarriesTheViewCommands)
{
    MainWindow window;
    auto *scene = window.findChild<CanvasScene *>();
    ASSERT_NE(scene, nullptr);
    auto *names = window.findChild<QAction *>(QStringLiteral("actionShowObjectNames"));
    ASSERT_NE(names, nullptr) << "there is no Show Names action";

    EXPECT_TRUE(names->isCheckable());
    EXPECT_FALSE(names->isChecked()) << "Show Names starts on";
    EXPECT_FALSE(scene->objectNamesShown()) << "the scene starts with names drawn";

    names->trigger();
    EXPECT_TRUE(scene->objectNamesShown()) << "ticking Show Names did not reach the scene";
    names->trigger();
    EXPECT_FALSE(scene->objectNamesShown());

    // The canvas commands are on the menu whatever is under the pointer, and
    // stay there during a run, which is when framing the field matters most.
    QMenu menu(&window);
    window.addCanvasActionsForTest(&menu);
    QStringList offered;
    for (QAction *action : menu.actions())
        offered << action->text();
    for (const QString &wanted : { QStringLiteral("&Fit to Field"), QStringLiteral("Select &All"),
                                   QStringLiteral("&Paste"), QStringLiteral("Show &Names") }) {
        EXPECT_TRUE(offered.contains(wanted))
            << qPrintable(wanted) << " is not on the canvas menu; it has: "
            << qPrintable(offered.join(QStringLiteral(", ")));
    }
}

TEST(Editing, TheSelectionCommandsHaveKeysAndFollowTheSelection)
{
    MainWindow window;
    auto *scene = window.findChild<CanvasScene *>();
    ASSERT_NE(scene, nullptr);
    auto *selectAll = window.findChild<QAction *>(QStringLiteral("actionSelectAll"));
    auto *duplicate = window.findChild<QAction *>(QStringLiteral("actionDuplicate"));
    ASSERT_NE(selectAll, nullptr) << "there is no Select All action";
    ASSERT_NE(duplicate, nullptr) << "there is no Duplicate action";

    EXPECT_EQ(selectAll->shortcut(), QKeySequence(QStringLiteral("Ctrl+A")));
    EXPECT_EQ(duplicate->shortcut(), QKeySequence(QStringLiteral("Ctrl+D")));

    scene->setEditorMode(EditorMode::Edit);
    EXPECT_FALSE(selectAll->isEnabled()) << "an empty field offers Select All";

    const QVector<ShapeItem *> shapes = threeShapes(scene);
    EXPECT_TRUE(selectAll->isEnabled()) << "the field has shapes and Select All is greyed out";
    EXPECT_FALSE(duplicate->isEnabled()) << "nothing is picked and Duplicate is offered";

    // Edit mode: one active shape and the rest alongside it.
    selectAll->trigger();
    ASSERT_NE(scene->activeItem(), nullptr) << "Select All left no active shape";
    // The shape carrying the handles is deliberately not in the co-selection,
    // so "all of them" is the active one plus the rest.
    for (ShapeItem *shape : shapes) {
        EXPECT_TRUE(shape == scene->activeItem() || shape->isCoSelected())
            << qPrintable(shape->name()) << " was left unpicked by Select All";
    }
    EXPECT_EQ(scene->editSelection().size(), shapes.size() - 1);
    EXPECT_TRUE(duplicate->isEnabled()) << "a selection is in hand and Duplicate is greyed out";

    duplicate->trigger();
    EXPECT_EQ(scene->shapes().size(), shapes.size() * 2) << "Duplicate copied nothing";

    // Physics mode keeps its own selection, and picking a shape there picks
    // the whole of its body.
    scene->setEditorMode(EditorMode::Physics);
    scene->clearPhysicsSelection();
    ASSERT_TRUE(scene->physicsSelection().isEmpty());
    selectAll->trigger();
    EXPECT_EQ(scene->physicsSelection().size(), scene->shapes().size())
        << "Select All did not fill the physics selection";
}

// Simulate, Step and Stop were mouse-only, and so were Create Body and the
// command that dissolves one.
// Select All on a real sceneful. Written as a loop over selectForPhysics it
// repainted the scene and rebuilt every panel once per shape, which took
// seconds on a few hundred of them. The budget is loose on purpose -- this is
// catching a quadratic, not policing milliseconds.
// The box looked for shapes and nothing else, so sweeping a field of
// rangefinders and blasts did nothing whatever -- not even clear what was
// picked before. And a selection used to be one kind at a time, so even once
// the box saw them it could only have kept the topmost.
TEST(Selection, ASweptBoxTakesBlastsRangefindersAndBodiesTogether)
{
    MainWindow window;
    auto *scene = window.findChild<CanvasScene *>();
    ASSERT_NE(scene, nullptr);
    scene->setEditorMode(EditorMode::Physics);

    RayItem *first = scene->addRay(QPointF(200, 200));
    RayItem *second = scene->addRay(QPointF(300, 200));
    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);
    for (RayItem *ray : { first, second }) {
        ray->setAngleDegrees(0.0);
        ray->setLength(40.0);
    }
    ExplosionItem *left = scene->addExplosion(QPointF(200, 300));
    ExplosionItem *right = scene->addExplosion(QPointF(300, 300));
    ASSERT_NE(left, nullptr);
    ASSERT_NE(right, nullptr);

    auto *plate = new RectangleItem;
    plate->setRect(QRectF(0, 0, 40, 40));
    plate->setPos(QPointF(220, 380));
    plate->setName(QStringLiteral("plate"));
    scene->addItem(plate);
    scene->notifyShapesChanged();
    scene->selectForPhysics(plate, false);
    PhysicsBody *body = scene->createBodyFromSelection();
    ASSERT_NE(body, nullptr);
    scene->clearCanvasSelection();

    // One sweep over the lot: two blasts, two rangefinders and a body.
    scene->applyRubberBand(QRectF(150, 150, 300, 300), false);
    EXPECT_EQ(scene->raySelection().size(), 2) << "the box kept fewer rangefinders than it swept";
    EXPECT_EQ(scene->explosionSelection().size(), 2) << "the box kept fewer blasts than it swept";
    EXPECT_EQ(scene->physicsSelection().size(), 1) << "the box missed the body";

    // The whole set moves together, which is most of what picking it is for.
    const QPointF wasRay = first->pos();
    const QPointF wasBlast = right->pos();
    const QPointF wasShape = plate->pos();
    ASSERT_TRUE(scene->nudgeSelection(QPointF(10, 0)));
    EXPECT_EQ(first->pos(), wasRay + QPointF(10, 0)) << "the rangefinder stayed put";
    EXPECT_EQ(right->pos(), wasBlast + QPointF(10, 0)) << "the blast stayed put";
    EXPECT_EQ(plate->pos(), wasShape + QPointF(10, 0)) << "the body stayed put";

    // A box round one kind replaces the selection rather than adding to it.
    scene->applyRubberBand(QRectF(150, 250, 300, 100), false);
    EXPECT_EQ(scene->explosionSelection().size(), 2);
    EXPECT_TRUE(scene->raySelection().isEmpty()) << "the rangefinders survived a fresh sweep";
    EXPECT_TRUE(scene->physicsSelection().isEmpty()) << "the body survived a fresh sweep";

    // Shift adds, the way it does for shapes.
    scene->applyRubberBand(QRectF(150, 150, 300, 100), true);
    EXPECT_EQ(scene->explosionSelection().size(), 2) << "adding dropped the blasts";
    EXPECT_EQ(scene->raySelection().size(), 2) << "Shift did not add the rangefinders";

    // Delete takes all of it, not just whichever one the pane was showing.
    scene->deleteActiveItem();
    EXPECT_TRUE(scene->rays().isEmpty()) << "Delete left rangefinders behind";
    EXPECT_TRUE(scene->explosions().isEmpty()) << "Delete left blasts behind";
    EXPECT_TRUE(scene->raySelection().isEmpty());
    EXPECT_TRUE(scene->explosionSelection().isEmpty());
}

TEST(Editing, PickingManyShapesIsOnePassOverThem)
{
    MainWindow window;
    window.resize(900, 700);
    window.show();
    QCoreApplication::processEvents();

    auto *scene = window.findChild<CanvasScene *>();
    ASSERT_NE(scene, nullptr);
    auto *selectAll = window.findChild<QAction *>(QStringLiteral("actionSelectAll"));
    ASSERT_NE(selectAll, nullptr);

    constexpr int kShapes = 400;
    for (int i = 0; i < kShapes; ++i) {
        auto *shape = new RectangleItem;
        shape->setRect(QRectF(0, 0, 10, 10));
        shape->setPos(QPointF((i % 20) * 14, (i / 20) * 14));
        shape->setName(QStringLiteral("box_%1").arg(i));
        scene->addItem(shape);
    }
    scene->notifyShapesChanged();
    ASSERT_EQ(scene->shapes().size(), kShapes);

    // One signal for the whole command, not one per shape: that count is what
    // every panel rebuild hangs off.
    QSignalSpy editAnnounced(scene, &CanvasScene::editSelectionChanged);
    QSignalSpy physicsAnnounced(scene, &CanvasScene::physicsSelectionChanged);

    scene->setEditorMode(EditorMode::Edit);
    QElapsedTimer clock;
    clock.start();
    selectAll->trigger();
    const qint64 editMs = clock.elapsed();
    EXPECT_EQ(scene->editSelection().size(), kShapes - 1);
    EXPECT_EQ(editAnnounced.count(), 1)
        << "Select All announced the edit selection " << editAnnounced.count()
        << " times for " << kShapes << " shapes";

    scene->setEditorMode(EditorMode::Physics);
    scene->clearPhysicsSelection();
    physicsAnnounced.clear();
    clock.restart();
    selectAll->trigger();
    const qint64 physicsMs = clock.elapsed();
    EXPECT_EQ(scene->physicsSelection().size(), kShapes);
    EXPECT_EQ(physicsAnnounced.count(), 1)
        << "Select All announced the physics selection " << physicsAnnounced.count()
        << " times for " << kShapes << " shapes";

    // The naive loop measured 6.9 s and 26 s here; one pass measures a few
    // hundred milliseconds, nearly all of it building the shapes.
    EXPECT_LT(editMs, 1500) << "Select All in Edit mode took " << editMs << " ms";
    EXPECT_LT(physicsMs, 1500) << "Select All in Physics mode took " << physicsMs << " ms";

    // A swept box picks the same way, and used to loop the same way. The band
    // covers the whole field, so it catches every shape.
    const QRectF whole = scene->itemsBoundingRect().adjusted(-50, -50, 50, 50);

    scene->clearPhysicsSelection();
    physicsAnnounced.clear();
    clock.restart();
    scene->applyRubberBand(whole, false);
    const qint64 bandPhysicsMs = clock.elapsed();
    EXPECT_EQ(scene->physicsSelection().size(), kShapes) << "the box missed shapes";
    EXPECT_EQ(physicsAnnounced.count(), 1)
        << "the box announced the physics selection " << physicsAnnounced.count() << " times";
    EXPECT_LT(bandPhysicsMs, 1500) << "a box over " << kShapes << " shapes took "
                                   << bandPhysicsMs << " ms";

    scene->setEditorMode(EditorMode::Edit);
    scene->clearEditSelection();
    editAnnounced.clear();
    clock.restart();
    scene->applyRubberBand(whole, false);
    const qint64 bandEditMs = clock.elapsed();
    EXPECT_EQ(scene->editSelection().size(), kShapes - 1) << "the box missed shapes";
    EXPECT_LE(editAnnounced.count(), 2)
        << "the box announced the edit selection " << editAnnounced.count() << " times";
    EXPECT_LT(bandEditMs, 1500) << "a box over " << kShapes << " shapes took " << bandEditMs
                                << " ms";

    // And so does what a paste leaves picked -- the same method, so the count
    // is what matters here rather than the clock, which is mostly the copying.
    auto *duplicate = window.findChild<QAction *>(QStringLiteral("actionDuplicate"));
    ASSERT_NE(duplicate, nullptr);
    ASSERT_TRUE(duplicate->isEnabled());
    editAnnounced.clear();
    duplicate->trigger();
    ASSERT_EQ(scene->shapes().size(), kShapes * 2) << "the duplicate put down no shapes";
    EXPECT_EQ(scene->editSelection().size(), kShapes - 1)
        << "the duplicate left the wrong shapes picked";
    EXPECT_LE(editAnnounced.count(), 3)
        << "selecting what was pasted announced the edit selection " << editAnnounced.count()
        << " times for " << kShapes << " shapes";
}

TEST(Editing, TheToolbarCommandsHaveTheirKeys)
{
    MainWindow window;
    const QVector<QPair<QString, QString>> wanted {
        { QStringLiteral("actionSimulate"), QStringLiteral("F5") },
        { QStringLiteral("actionStep"), QStringLiteral("F6") },
        { QStringLiteral("actionStop"), QStringLiteral("F8") },
        { QStringLiteral("actionCreateBody"), QStringLiteral("Ctrl+G") },
        { QStringLiteral("actionDissolveBody"), QStringLiteral("Ctrl+Shift+G") },
        { QStringLiteral("actionFitToField"), QStringLiteral("Ctrl+Shift+0") },
    };
    for (const auto &one : wanted) {
        auto *action = window.findChild<QAction *>(one.first);
        ASSERT_NE(action, nullptr) << qPrintable(one.first) << " is not on the window";
        EXPECT_EQ(action->shortcut(), QKeySequence(one.second))
            << qPrintable(one.first) << " has no key, or the wrong one";
    }

    // View is rebuilt from scratch every time it opens, so the zoom commands on
    // it are added inside that rebuild and are easy to leave out of it.
    auto *view = window.findChild<QMenu *>(QStringLiteral("menuView"));
    ASSERT_NE(view, nullptr) << "there is no View menu";
    emit view->aboutToShow();
    QStringList names;
    for (QAction *action : view->actions())
        names << action->text();
    EXPECT_TRUE(names.contains(QStringLiteral("&Fit to Field")))
        << "View does not offer Fit to Field; it has: " << qPrintable(names.join(QStringLiteral(", ")));
    EXPECT_TRUE(names.contains(QStringLiteral("Reset Zoom")))
        << "View does not offer Reset Zoom; it has: " << qPrintable(names.join(QStringLiteral(", ")));
    // And it still lists the panels, which is what it was there for.
    EXPECT_TRUE(names.contains(QStringLiteral("Properties")))
        << "the zoom commands pushed the panel list off View";

    // Every chord is one command's own: two on the same keys means one of them
    // never fires, and Qt says nothing about it.
    QHash<QString, QString> seen;
    for (QAction *action : window.findChildren<QAction *>()) {
        const QKeySequence key = action->shortcut();
        if (key.isEmpty())
            continue;
        const QString text = key.toString();
        EXPECT_FALSE(seen.contains(text))
            << qPrintable(text) << " is on both " << qPrintable(seen.value(text)) << " and "
            << qPrintable(action->objectName());
        seen.insert(text, action->objectName());
    }
}

// Zooming added a flat number of percent to the level, so one notch went
// 100% -> 110% and the same notch went 500% -> 510%: coming back out from far
// in took thirty of them. And only Shift zoomed, though Ctrl is what every
// other editor uses.
TEST(Editing, TheCanvasZoomsProportionallyUnderEitherModifier)
{
    MainWindow window;
    auto *scene = window.findChild<CanvasScene *>();
    ASSERT_NE(scene, nullptr);
    scene->setScaleMin(1.0);
    scene->setScaleMax(10000.0);
    scene->setScaleStep(10.0);

    const auto notch = [scene](Qt::KeyboardModifier modifier) {
        QGraphicsSceneWheelEvent event(QEvent::GraphicsSceneWheel);
        event.setScenePos(QPointF(0, 0));
        event.setDelta(120);
        event.setModifiers(modifier);
        QCoreApplication::sendEvent(scene, &event);
    };

    scene->setCurrentScale(100.0);
    notch(Qt::ControlModifier);
    EXPECT_NEAR(scene->currentScale(), 110.0, 1e-6) << "Ctrl+wheel does not zoom";

    scene->setCurrentScale(500.0);
    notch(Qt::ShiftModifier);
    EXPECT_NEAR(scene->currentScale(), 550.0, 1e-6)
        << "the step is a flat number of percent rather than a share of the level";

    // A bare wheel still does not zoom: it is how the view scrolls.
    scene->setCurrentScale(100.0);
    notch(Qt::NoModifier);
    EXPECT_NEAR(scene->currentScale(), 100.0, 1e-6) << "a bare wheel zoomed the canvas";
}

// There was no way to frame the scene: the zoom box had presets and nothing
// worked out what the field needed.
TEST(Editing, FitToFieldFramesTheWholeField)
{
    MainWindow window;
    window.resize(900, 700);
    window.show();
    QCoreApplication::processEvents();

    auto *scene = window.findChild<CanvasScene *>();
    auto *view = window.findChild<QGraphicsView *>(QStringLiteral("canvasView"));
    ASSERT_NE(scene, nullptr);
    ASSERT_NE(view, nullptr);

    scene->setScaleMin(1.0);
    scene->setScaleMax(10000.0);
    scene->setFieldSize(4000.0, 3000.0);
    scene->setCurrentScale(400.0);

    window.fitToFieldForTest();

    // The field fits in the viewport at the scale it chose, and is not so
    // small that it would fit several times over.
    const qreal factor = scene->currentScale() / 100.0;
    const QSizeF shown(4000.0 * factor, 3000.0 * factor);
    const QSize port = view->viewport()->size();
    ASSERT_FALSE(port.isEmpty()) << "the view has no viewport to fit into";
    EXPECT_LE(shown.width(), port.width() + 1.0) << "the field is wider than the view";
    EXPECT_LE(shown.height(), port.height() + 1.0) << "the field is taller than the view";
    EXPECT_GE(qMax(shown.width() / port.width(), shown.height() / port.height()), 0.8)
        << "the field was shrunk far smaller than it had to be";
}

// Dropping a .phys on the window opens it. Nothing accepted drops at all, so a
// file dragged in from the desktop did nothing.
TEST(Editing, ASceneFileDroppedOnTheWindowOpensIt)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());

    MainWindow source;
    auto *sourceScene = source.findChild<CanvasScene *>();
    ASSERT_NE(sourceScene, nullptr);
    threeShapes(sourceScene);
    const QString path = QDir(dir.path()).filePath(QStringLiteral("dropped.phys"));
    ASSERT_TRUE(source.saveSceneAsForTest(path)) << "the scene to be dropped was not written";

    MainWindow window;
    auto *scene = window.findChild<CanvasScene *>();
    ASSERT_NE(scene, nullptr);
    ASSERT_TRUE(window.acceptDrops()) << "the window refuses drops";
    ASSERT_TRUE(scene->shapes().isEmpty());

    // Through the handler rather than a posted event: Qt delivers a drop via
    // its own drag manager, so a QDropEvent sent at the window never arrives.
    const auto drop = [&window](const QString &file) {
        return window.dropUrlsForTest({ QUrl::fromLocalFile(file) });
    };

    EXPECT_TRUE(drop(path)) << "the drop was refused";
    EXPECT_EQ(scene->shapes().size(), 3) << "the dropped scene was not opened";

    // Anything else is left for whatever else wants it rather than swallowed.
    const QString other = QDir(dir.path()).filePath(QStringLiteral("notes.txt"));
    QFile note(other);
    ASSERT_TRUE(note.open(QIODevice::WriteOnly));
    note.write("not a scene");
    note.close();
    EXPECT_FALSE(drop(other)) << "a file that is not a scene was accepted";
}

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

namespace {

// The smallest distance between any two corners of shapes that end up next to
// each other on a ring -- which is the gap the ring left between them.
qreal closestNeighbours(const QVector<ShapeItem *> &made)
{
    QVector<ShapeItem *> round = made;
    QRectF all;
    for (ShapeItem *shape : round) {
        const QRectF box = shape->mapToScene(shape->rect()).boundingRect();
        all = all.isNull() ? box : all.united(box);
    }
    const QPointF centre = all.center();
    const auto bearing = [&](ShapeItem *shape) {
        const QPointF from = shape->mapToScene(shape->rect()).boundingRect().center() - centre;
        return std::atan2(from.y(), from.x());
    };
    std::sort(round.begin(), round.end(),
              [&](ShapeItem *a, ShapeItem *b) { return bearing(a) < bearing(b); });

    qreal worst = std::numeric_limits<qreal>::max();
    for (int i = 0; i < round.size(); ++i) {
        const QPolygonF a = round.at(i)->mapToScene(QPolygonF(
            static_cast<PolygonItem *>(round.at(i))->points()));
        const QPolygonF b = round.at((i + 1) % round.size())->mapToScene(QPolygonF(
            static_cast<PolygonItem *>(round.at((i + 1) % round.size()))->points()));
        qreal best = std::numeric_limits<qreal>::max();
        for (const QPointF &p : a)
            for (const QPointF &q : b)
                best = qMin(best, QLineF(p, q).length());
        worst = qMin(worst, best);
    }
    return worst;
}

// Teeth: triangles, drawn point up and point down in turn, since which way one
// was drawn must make no difference to the ring it ends up in.
QVector<ShapeItem *> teeth(CanvasScene *scene, int count)
{
    QVector<ShapeItem *> made;
    for (int i = 0; i < count; ++i) {
        QPolygonF points;
        if (i % 2)
            points << QPointF(-20, 50) << QPointF(0, -50) << QPointF(20, 50);
        else
            points << QPointF(-20, -50) << QPointF(0, 50) << QPointF(20, -50);
        auto *tooth = new PolygonItem(points, true);
        tooth->setPos(300 * std::cos(i * 2 * M_PI / count), 300 * std::sin(i * 2 * M_PI / count));
        tooth->setName(QStringLiteral("tooth%1").arg(i));
        scene->addItem(tooth);
        made.append(tooth);
    }
    scene->notifyShapesChanged();
    scene->selectShape(made.first());
    for (int i = 1; i < made.size(); ++i)
        scene->addToEditSelection(made.at(i));
    return made;
}

} // namespace

// A ring packs its shapes edge to edge, and the gap it is given is the gap it
// leaves -- measured between the shapes themselves, not between the slices of
// the turn they were given.
TEST(Alignment, ARingLeavesTheGapItWasGiven)
{
    for (const qreal gap : { 0.0, 10.0, 30.0 }) {
        CanvasScene scene;
        scene.setEditorMode(EditorMode::Edit);
        const QVector<ShapeItem *> made = teeth(&scene, 10);
        scene.alignSelection(CanvasScene::Align::RingOutside, gap);
        EXPECT_NEAR(closestNeighbours(made), gap, 0.01)
            << "a ring asked for a gap of " << gap << " did not leave one";
    }
}

// Which way up a tooth was drawn is the shape's business and not the ring's:
// every one of them ends up pointing away from the centre.
TEST(Alignment, EveryToothPointsTheSameWayWhicheverWayItWasDrawn)
{
    CanvasScene scene;
    scene.setEditorMode(EditorMode::Edit);
    const QVector<ShapeItem *> made = teeth(&scene, 8);
    scene.alignSelection(CanvasScene::Align::RingOutside);

    QRectF all;
    for (ShapeItem *shape : made) {
        const QRectF box = shape->mapToScene(shape->rect()).boundingRect();
        all = all.isNull() ? box : all.united(box);
    }
    const QPointF centre = all.center();

    for (ShapeItem *shape : made) {
        // The apex is the corner on its own; the other two are the base. The
        // apex has to be the one furthest out.
        const QPolygonF corners =
            shape->mapToScene(QPolygonF(static_cast<PolygonItem *>(shape)->points()));
        QVector<qreal> reach;
        for (const QPointF &corner : corners)
            reach << QLineF(centre, corner).length();
        std::sort(reach.begin(), reach.end());
        EXPECT_GT(reach.last(), reach.at(reach.size() - 2) + 10.0)
            << shape->name().toStdString() << " went into the ring the wrong way up";
    }
}

// Clone lays a grid of copies out from whatever is picked, the original
// standing in for the first row and column, and the spacing is the gap left
// between neighbours rather than the step from one to the next.
TEST(Clone, AGridOfCopiesLeavesTheSpacingItWasGiven)
{
    MainWindow window;
    CanvasScene *scene = window.findChild<CanvasScene *>();
    ASSERT_TRUE(scene);
    scene->setEditorMode(EditorMode::Edit);
    ShapeItem *first = scene->addRectangle(QPointF(100, 100));
    scene->selectShape(first);
    const QRectF one = first->mapToScene(first->rect()).boundingRect();

    EXPECT_EQ(window.cloneForTest(2, 3, 10.0, 5.0), 5)
        << "two rows of three leaves five copies beside the original";
    ASSERT_EQ(scene->shapes().size(), 6);

    QVector<QPointF> corners;
    for (ShapeItem *shape : scene->shapes())
        corners << shape->mapToScene(shape->rect()).boundingRect().topLeft();
    std::sort(corners.begin(), corners.end(), [](const QPointF &a, const QPointF &b) {
        return a.y() == b.y() ? a.x() < b.x() : a.y() < b.y();
    });

    for (int row = 0; row < 2; ++row) {
        for (int column = 0; column < 3; ++column) {
            const QPointF want = one.topLeft()
                                 + QPointF(column * (one.width() + 10.0),
                                           row * (one.height() + 5.0));
            const QPointF got = corners.at(row * 3 + column);
            EXPECT_NEAR(got.x(), want.x(), 0.001) << "copy " << row << "," << column;
            EXPECT_NEAR(got.y(), want.y(), 0.001) << "copy " << row << "," << column;
        }
    }
}

// Clicking a shape presses and releases on it, which looked to the undo stack
// like a move of nothing: the scene came out marked as edited, and Save lit up
// over a selection. Only a drag that went somewhere is an edit.
TEST(Selection, ClickingAShapeIsNotAnEdit)
{
    MainWindow window;
    CanvasScene *scene = window.findChild<CanvasScene *>();
    UndoStack *undo = window.findChild<UndoStack *>();
    ASSERT_TRUE(scene);
    ASSERT_TRUE(undo);
    scene->setEditorMode(EditorMode::Edit);

    ShapeItem *box = scene->addRectangle(QPointF(100, 100));
    ASSERT_TRUE(box);
    undo->markClean();
    ASSERT_TRUE(undo->isClean()) << "the test did not start from a saved scene";

    const QPointF on = box->mapToScene(box->rect().center());
    press(scene, on, Qt::NoModifier);
    release(scene, on, Qt::NoModifier);

    EXPECT_EQ(scene->activeItem(), box) << "the click did not select the shape";
    EXPECT_TRUE(undo->isClean()) << "selecting a shape marked the scene as edited";

    // A drag that goes somewhere still counts.
    press(scene, on, Qt::NoModifier);
    moveTo(scene, on + QPointF(30, 20), Qt::NoModifier);
    release(scene, on + QPointF(30, 20), Qt::NoModifier);
    EXPECT_FALSE(undo->isClean()) << "moving a shape was not recorded as an edit";
}

// Spacing evenly leaves the two end shapes where they are and the span alone:
// that is what the default gap is for. The other direction is untouched.
TEST(Alignment, SpacingEvenlyLeavesTheSpanAlone)
{
    CanvasScene scene;
    scene.setEditorMode(EditorMode::Edit);
    const QVector<ShapeItem *> shapes = threeShapes(&scene);
    scene.selectShape(shapes.at(0));
    for (int i = 1; i < shapes.size(); ++i)
        scene.addToEditSelection(shapes.at(i));

    QRectF before;
    QVector<qreal> tops;
    for (ShapeItem *shape : shapes) {
        before = before.isNull() ? boxOf(shape) : before.united(boxOf(shape));
        tops << boxOf(shape).top();
    }

    const qreal gap = scene.evenGap(true);
    scene.alignSelection(CanvasScene::Align::DistributeAcross, gap);

    QRectF after;
    for (int i = 0; i < shapes.size(); ++i) {
        after = after.isNull() ? boxOf(shapes.at(i)) : after.united(boxOf(shapes.at(i)));
        EXPECT_NEAR(boxOf(shapes.at(i)).top(), tops.at(i), 0.001)
            << shapes.at(i)->name().toStdString() << " moved down as well as across";
    }
    EXPECT_NEAR(after.left(), before.left(), 0.001) << "spacing moved the leftmost shape";
    EXPECT_NEAR(after.right(), before.right(), 0.001) << "spacing changed the span";

    // Every gap the same, which is the whole point.
    QVector<QRectF> boxes;
    for (ShapeItem *shape : shapes)
        boxes << boxOf(shape);
    std::sort(boxes.begin(), boxes.end(),
              [](const QRectF &a, const QRectF &b) { return a.left() < b.left(); });
    for (int i = 0; i + 1 < boxes.size(); ++i) {
        EXPECT_NEAR(boxes.at(i + 1).left() - boxes.at(i).right(), gap, 0.001)
            << "gap " << i << " is not the one asked for";
    }
}

// Mirror leaves a reflected copy a whole gap from the selection's edge, and
// reflects it -- a copy that is merely moved is not a mirror.
TEST(Clone, MirrorPutsAReflectedCopyBesideTheOriginal)
{
    MainWindow window;
    CanvasScene *scene = window.findChild<CanvasScene *>();
    ASSERT_TRUE(scene);
    scene->setEditorMode(EditorMode::Edit);

    QPolygonF points;
    points << QPointF(0, 0) << QPointF(80, 0) << QPointF(0, 60);
    auto *wedge = new PolygonItem(points, true);
    wedge->setPos(100, 100);
    wedge->setName(QStringLiteral("wedge"));
    scene->addItem(wedge);
    scene->notifyShapesChanged();
    scene->selectShape(wedge);

    const QRectF was = wedge->mapToScene(wedge->rect()).boundingRect();

    // Every side the panel offers, in the order it offers them.
    const char *names[] = { "right", "left", "top", "bottom" };
    for (int side = 0; side < 4; ++side) {
        ASSERT_EQ(window.mirrorForTest(side, 20.0), 1) << names[side];
        ASSERT_EQ(scene->shapes().size(), 2) << names[side];
        ShapeItem *one = scene->shapes().at(0) == wedge ? scene->shapes().at(1)
                                                        : scene->shapes().at(0);
        const QRectF box = one->mapToScene(one->rect()).boundingRect();
        EXPECT_NEAR(box.size().width(), was.width(), 0.001) << names[side] << " changed size";
        switch (side) {
        case 0: EXPECT_NEAR(box.left(), was.right() + 20.0, 0.001) << "not a margin to the right";
            break;
        case 1: EXPECT_NEAR(box.right(), was.left() - 20.0, 0.001) << "not a margin to the left";
            break;
        case 2: EXPECT_NEAR(box.bottom(), was.top() - 20.0, 0.001) << "not a margin above";
            break;
        default: EXPECT_NEAR(box.top(), was.bottom() + 20.0, 0.001) << "not a margin below";
            break;
        }
        delete one;
    }

    ASSERT_EQ(window.mirrorForTest(0, 20.0), 1);
    ASSERT_EQ(scene->shapes().size(), 2);

    ShapeItem *copy = scene->shapes().at(0) == wedge ? scene->shapes().at(1)
                                                     : scene->shapes().at(0);
    const QRectF now = copy->mapToScene(copy->rect()).boundingRect();

    // The wedge's point is on the left; reflected, it has to be on the right.
    const auto furthestRight = [](ShapeItem *shape) {
        const QPolygonF corners =
            shape->mapToScene(QPolygonF(static_cast<PolygonItem *>(shape)->points()));
        qreal best = -1e9;
        for (const QPointF &corner : corners) {
            if (qAbs(corner.y() - corners.boundingRect().bottom()) < 0.001)
                best = qMax(best, corner.x());
        }
        return best;
    };
    EXPECT_NEAR(furthestRight(wedge) - was.left(), 0.0, 0.001)
        << "the test has the original the wrong way round";
    EXPECT_NEAR(furthestRight(copy) - now.left(), now.width(), 0.001)
        << "the copy was moved but never reflected";
}

// A circle becomes the run of segments round its rim -- a chain is a shape, in
// the way Convert to Polygon makes one, and it keeps the name and body of what
// it replaced.
TEST(Shapes, ACircleConvertsToAChainOfSegments)
{
    MainWindow window;
    CanvasScene *scene = window.findChild<CanvasScene *>();
    ASSERT_TRUE(scene);
    scene->setEditorMode(EditorMode::Edit);

    ShapeItem *circle = scene->addCircle(QPointF(100, 100));
    ASSERT_TRUE(circle);
    const QString name = circle->name();
    const QRectF own = circle->rect();
    const QPointF middle = own.center();
    const qreal radius = qMin(own.width(), own.height()) / 2.0;

    constexpr int kSegments = 12;
    ShapeItem *chain = window.circleToChainForTest(circle, kSegments);
    ASSERT_TRUE(chain) << "the circle did not convert";
    EXPECT_EQ(scene->shapes().size(), 1) << "converting left the circle behind as well";
    EXPECT_EQ(chain->name(), name) << "the chain did not keep the circle's name";
    EXPECT_EQ(chain->typeName(), QLatin1String("polygon"));
    ASSERT_EQ(chain->nodeCount(), kSegments);

    // Every corner on the circle, and the sides all the same length: a chain
    // of segments round a circle is a regular polygon inscribed in it.
    qreal first = -1.0;
    for (int i = 0; i < kSegments; ++i) {
        const QPointF a = chain->nodePosition(i);
        const QPointF b = chain->nodePosition((i + 1) % kSegments);
        EXPECT_NEAR(QLineF(middle, a).length(), radius, 0.001) << "corner " << i
            << " is not on the circle";
        const qreal side = QLineF(a, b).length();
        if (first < 0)
            first = side;
        EXPECT_NEAR(side, first, 0.001) << "segment " << i << " is a different length";
    }

    // Hollow, at any count. A closed convex outline within the vertex limit is
    // otherwise built solid, which would make a six-segment chain a filled
    // hexagon with no inside for anything to fall into.
    EXPECT_FALSE(chain->hasInterior()) << "a twelve-segment chain came out solid";
    ShapeItem *few = window.circleToChainForTest(chain, 6);
    ASSERT_TRUE(few);
    EXPECT_FALSE(few->hasInterior()) << "a six-segment chain came out solid";

    // The start angle turns the corners without turning the shape.
    ShapeItem *turned = window.circleToChainForTest(few, 6, 30.0);
    ASSERT_TRUE(turned);
    EXPECT_DOUBLE_EQ(turned->rotation(), 0.0) << "the start angle rotated the shape itself";
    const QPointF corner = turned->nodePosition(0) - middle;
    EXPECT_NEAR(qRadiansToDegrees(std::atan2(corner.y(), corner.x())), 30.0, 0.001)
        << "the first corner did not move to the start angle";

    // Fewer than three is not a ring and is refused.
    EXPECT_EQ(window.circleToChainForTest(turned, 2), nullptr);
}

// Convert to Polygons makes a wheel: solid pieces round the rim, as deep as
// asked, in the circle's body and with nothing inside. A chain or segments
// are only edges and a body of them has no mass; this one has, so it moves,
// and what is inside it stays there.
TEST(Shapes, ACircleConvertsToASolidRimWithNothingInside)
{
    MainWindow window;
    CanvasScene *scene = window.findChild<CanvasScene *>();
    ASSERT_TRUE(scene);
    scene->setEditorMode(EditorMode::Edit);

    ShapeItem *circle = scene->addCircle(QPointF(0, 0));
    circle->setRect(QRectF(-100, -100, 200, 200));
    const QString name = circle->name();
    const QPointF centre = circle->mapToScene(QPointF(0, 0));
    scene->setEditorMode(EditorMode::Physics);
    scene->selectShapes({ circle }, false);
    window.createBodyForTest(true);
    PhysicsBody *wheel = circle->body();
    ASSERT_TRUE(wheel);

    window.openPolygonsToolForTest(circle);
    QCoreApplication::processEvents();
    ToolOptionsBar *panel = window.findChild<ToolOptionsBar *>();
    ASSERT_TRUE(panel);
    const QList<QDoubleSpinBox *> boxes = panel->findChildren<QDoubleSpinBox *>();
    ASSERT_EQ(boxes.size(), 3) << "segments, start angle and thickness";
    boxes.at(0)->setValue(16);
    boxes.at(2)->setValue(10);
    QCoreApplication::processEvents();
    window.applyToolForTest();
    QCoreApplication::processEvents();

    const QVector<ShapeItem *> pieces = wheel->shapes();
    ASSERT_EQ(pieces.size(), 16) << "one piece per segment, all in the circle's body";
    EXPECT_EQ(scene->shapes().size(), 16) << "the circle was left behind";
    EXPECT_EQ(pieces.first()->name(), name) << "the first piece lost the circle's name";
    const qreal inner = 100.0 * std::cos(M_PI / 16) - 10.0;
    for (ShapeItem *piece : pieces) {
        EXPECT_TRUE(piece->hasInterior()) << qPrintable(piece->name()) << " is not solid";
        qreal nearest = std::numeric_limits<qreal>::max();
        qreal furthest = 0.0;
        QPolygonF outline;
        for (int i = 0; i < piece->nodeCount(); ++i) {
            outline << piece->mapToScene(piece->nodePosition(i));
            const qreal r = QLineF(centre, outline.last()).length();
            nearest = qMin(nearest, r);
            furthest = qMax(furthest, r);
        }
        EXPECT_NEAR(furthest, 100.0, 0.001) << "a piece does not reach the rim";
        EXPECT_GT(nearest, inner - 0.001) << "a piece reaches further in than the thickness";
        EXPECT_FALSE(outline.containsPoint(centre, Qt::OddEvenFill)) << "the middle is not empty";
    }

    // A ball inside a wheel that holds still stays inside it.
    CircleItem *ball = scene->addCircle(QPointF(0, 0));
    ball->setRect(QRectF(-10, -10, 20, 20));
    scene->selectShapes({ ball }, false);
    window.createBodyForTest(false);
    ASSERT_TRUE(ball->body());
    window.startRunForTest();
    QCoreApplication::processEvents();
    SimulationController *sim = window.findChild<SimulationController *>();
    ASSERT_TRUE(sim);
    for (int i = 0; i < 120; ++i)
        sim->stepFrame();
    EXPECT_LT(QLineF(centre, inRun(*sim, ball->body())->originScenePos()).length(), inner)
        << "the ball fell through the rim";
    window.stopRunForTest();
    QCoreApplication::processEvents();

    // And a wheel that moves is simulated, which one made of lines is not.
    wheel->props().type = physics::BodyType::Dynamic;
    window.startRunForTest();
    QCoreApplication::processEvents();
    EXPECT_FALSE(sim->skippedBodies().contains(wheel->name())) << "the wheel has no mass";
    for (int i = 0; i < 30; ++i)
        sim->stepFrame();
    EXPECT_GT(inRun(*sim, wheel)->originScenePos().y(), centre.y() + 1.0) << "the wheel did not fall";
    window.stopRunForTest();
    QCoreApplication::processEvents();
}

// Convert to Segments leaves one line per side, each a shape of its own and in
// no body -- which is the point: a joint connects bodies, so the pieces have
// to be separable. A rectangle keeps its corners, however finely it is split.
TEST(Shapes, ACircleAndARectangleComeApartIntoSegments)
{
    MainWindow window;
    CanvasScene *scene = window.findChild<CanvasScene *>();
    ASSERT_TRUE(scene);
    scene->setEditorMode(EditorMode::Edit);

    ShapeItem *circle = scene->addCircle(QPointF(100, 100));
    const QRectF own = circle->rect();
    const QPointF middle = circle->mapToScene(own.center());
    const qreal radius = qMin(own.width(), own.height()) / 2.0;

    constexpr int kSegments = 12;
    const QVector<ShapeItem *> round = window.toSegmentsForTest(circle, kSegments);
    ASSERT_EQ(round.size(), kSegments);

    for (ShapeItem *piece : round) {
        EXPECT_EQ(piece->nodeCount(), 2) << "a segment is a line, not a shape with corners";
        EXPECT_FALSE(piece->hasInterior()) << "a segment enclosed something";
        EXPECT_EQ(piece->body(), nullptr) << "a segment came out already in a body";
        for (int i = 0; i < piece->nodeCount(); ++i) {
            EXPECT_NEAR(QLineF(middle, piece->mapToScene(piece->nodePosition(i))).length(),
                        radius, 0.001)
                << "a segment end is not on the circle";
        }
    }

    // Four sides split three ways each, and the corners still corners: every
    // one of the rectangle's own four is an end of some segment.
    ShapeItem *box = scene->addRectangle(QPointF(600, 100));
    const QRectF boxOwn = box->rect();
    QVector<QPointF> wanted;
    for (const QPointF &corner : { boxOwn.topLeft(), boxOwn.topRight(), boxOwn.bottomRight(),
                                   boxOwn.bottomLeft() })
        wanted << box->mapToScene(corner);

    const QVector<ShapeItem *> sides = window.toSegmentsForTest(box, 3);
    ASSERT_EQ(sides.size(), 12) << "four sides split three ways is twelve segments";

    for (const QPointF &corner : wanted) {
        qreal nearest = std::numeric_limits<qreal>::max();
        for (ShapeItem *piece : sides) {
            for (int i = 0; i < piece->nodeCount(); ++i)
                nearest = qMin(nearest,
                               QLineF(corner, piece->mapToScene(piece->nodePosition(i))).length());
        }
        EXPECT_NEAR(nearest, 0.0, 0.001) << "a corner of the rectangle is not a segment end";
    }

    // Every segment meets the next: a ring of lines with a hole in it is not
    // an outline.
    for (ShapeItem *piece : sides) {
        for (int i = 0; i < piece->nodeCount(); ++i) {
            const QPointF end = piece->mapToScene(piece->nodePosition(i));
            qreal nearest = std::numeric_limits<qreal>::max();
            for (ShapeItem *other : sides) {
                if (other == piece)
                    continue;
                for (int j = 0; j < other->nodeCount(); ++j)
                    nearest = qMin(nearest,
                                   QLineF(end, other->mapToScene(other->nodePosition(j))).length());
            }
            EXPECT_NEAR(nearest, 0.0, 0.001) << "a segment end meets no other segment";
        }
    }
}

// A concave outline is only edges, so a body made of it alone has no mass and
// cannot move. Split into Convex Pieces makes it solid without moving it: the
// pieces stay in the body, in the polygon's own frame -- the body's origin is
// its first shape's, and joints are anchored there -- and cover the same area.
TEST(Shapes, AConcaveOutlineSplitsIntoSolidPiecesInItsBody)
{
    MainWindow window;
    CanvasScene *scene = window.findChild<CanvasScene *>();
    ASSERT_TRUE(scene);
    scene->setEditorMode(EditorMode::Edit);

    const auto sceneArea = [](const ShapeItem *shape) {
        qreal twice = 0.0;
        const int n = shape->nodeCount();
        for (int i = 0; i < n; ++i) {
            const QPointF a = shape->mapToScene(shape->nodePosition(i));
            const QPointF b = shape->mapToScene(shape->nodePosition((i + 1) % n));
            twice += a.x() * b.y() - b.x() * a.y();
        }
        return qAbs(twice) / 2.0;
    };

    QPolygonF ell;
    ell << QPointF(0, 0) << QPointF(160, 0) << QPointF(160, 40) << QPointF(40, 40)
        << QPointF(40, 120) << QPointF(0, 120);
    auto *shape = new PolygonItem(ell, true);
    shape->setName(QStringLiteral("ell"));
    shape->setPos(300, 200);
    shape->setRotation(30);
    scene->addItem(shape);
    scene->notifyShapesChanged();
    const qreal area = sceneArea(shape);
    ASSERT_FALSE(shape->hasInterior()) << "the test needs an outline that is not solid";
    ASSERT_TRUE(scene->canSplitIntoConvexPieces(shape));

    scene->setEditorMode(EditorMode::Physics);
    scene->selectShapes({ shape }, false);
    window.createBodyForTest(false);
    PhysicsBody *body = shape->body();
    ASSERT_TRUE(body);
    const QPointF origin = body->originScenePos();
    const qreal turned = body->rotationDegrees();

    window.openConvexToolForTest(shape);
    QCoreApplication::processEvents();
    EXPECT_EQ(body->shapes().size(), 2) << "an L within the point limit is two pieces";

    ToolOptionsBar *panel = window.findChild<ToolOptionsBar *>();
    ASSERT_TRUE(panel);
    QDoubleSpinBox *corners = panel->findChildren<QDoubleSpinBox *>().value(0);
    ASSERT_TRUE(corners) << "the panel offers no corner limit";
    corners->setValue(3);
    QCoreApplication::processEvents();
    window.applyToolForTest();
    QCoreApplication::processEvents();

    const QVector<ShapeItem *> pieces = body->shapes();
    ASSERT_EQ(pieces.size(), 4) << "an L cut into triangles is four of them";
    EXPECT_EQ(scene->shapes().size(), 4) << "the original was left behind";
    EXPECT_EQ(pieces.first()->name(), QStringLiteral("ell")) << "the first piece lost the name";
    qreal covered = 0.0;
    for (ShapeItem *piece : pieces) {
        EXPECT_TRUE(piece->hasInterior()) << qPrintable(piece->name()) << " is not solid";
        EXPECT_EQ(piece->nodeCount(), 3) << qPrintable(piece->name()) << " is over the limit";
        covered += sceneArea(piece);
    }
    EXPECT_NEAR(covered, area, 0.01) << "the pieces do not cover what the outline did";
    EXPECT_NEAR(QLineF(body->originScenePos(), origin).length(), 0.0, 1e-6)
        << "splitting moved the body's origin, and every joint with it";
    EXPECT_DOUBLE_EQ(body->rotationDegrees(), turned);
    for (ShapeItem *picked : scene->physicsSelection())
        EXPECT_TRUE(pieces.contains(picked)) << "a deleted shape is still picked";

    // Solid, so it has mass, so it is simulated -- a massless body is dropped.
    body->props().type = physics::BodyType::Dynamic;
    window.startRunForTest();
    QCoreApplication::processEvents();
    SimulationController *sim = window.findChild<SimulationController *>();
    ASSERT_TRUE(sim);
    for (int i = 0; i < 30; ++i)
        sim->stepFrame();
    EXPECT_GT(inRun(*sim, body)->originScenePos().y(), origin.y() + 1.0)
        << "the split body did not fall";
    window.stopRunForTest();
    QCoreApplication::processEvents();

    // Cancel puts the outline back as it was. A convex outline over the point
    // limit is the other thing that cannot be solid.
    scene->setEditorMode(EditorMode::Edit);
    QPolygonF ring;
    for (int i = 0; i < 12; ++i) {
        const qreal angle = i * 2.0 * M_PI / 12;
        ring << QPointF(60 * std::cos(angle), 60 * std::sin(angle));
    }
    auto *round = new PolygonItem(ring, true);
    round->setName(QStringLiteral("ring"));
    round->setPos(800, 200);
    scene->addItem(round);
    scene->notifyShapesChanged();
    scene->selectShape(round);
    window.openConvexToolForTest(round);
    QCoreApplication::processEvents();
    EXPECT_GT(scene->shapes().size(), 5) << "twelve corners within eight did not split";
    for (ShapeItem *piece : scene->shapes())
        EXPECT_LE(piece->nodeCount(), 8) << qPrintable(piece->name()) << " is over the limit";
    window.cancelToolForTest();
    QCoreApplication::processEvents();
    ASSERT_EQ(scene->shapes().size(), 5) << "Cancel left pieces behind";
    ShapeItem *back = nullptr;
    for (ShapeItem *one : scene->shapes()) {
        if (one->name() == QLatin1String("ring"))
            back = one;
    }
    ASSERT_TRUE(back) << "Cancel did not put the outline back";
    EXPECT_EQ(back->nodeCount(), 12);

    // An outline that crosses itself has no inside to cut, and is left alone.
    QPolygonF bow;
    bow << QPointF(0, 0) << QPointF(100, 100) << QPointF(100, 0) << QPointF(0, 100);
    auto *tie = new PolygonItem(bow, true);
    scene->addItem(tie);
    EXPECT_TRUE(scene->splitIntoConvexPieces(tie, 8).isEmpty());
    EXPECT_EQ(tie->scene(), scene);
}

// Driving a tool the way the window does, not by calling the conversion
// underneath it: the panel opens, its numbers are typed into, and Apply keeps
// what is on screen. Converting deletes the shape that was selected, and a
// shape deleted out from under the scene left the active item pointing at
// freed memory -- the crash came on the next paint or the next click, not on
// the conversion.
TEST(Shapes, ConvertingTheSelectedShapeLeavesNothingDangling)
{
    for (const bool toSegments : { true, false }) {
        MainWindow window;
        CanvasScene *scene = window.findChild<CanvasScene *>();
        ASSERT_TRUE(scene);
        scene->setEditorMode(EditorMode::Edit);

        ShapeItem *circle = scene->addCircle(QPointF(300, 300));
        scene->selectShape(circle);
        ASSERT_EQ(scene->activeItem(), circle);

        if (toSegments)
            window.openSegmentsToolForTest(circle);
        else
            window.openChainToolForTest(circle);
        QCoreApplication::processEvents();

        ToolOptionsBar *panel = window.findChild<ToolOptionsBar *>();
        ASSERT_TRUE(panel);
        QDoubleSpinBox *count = panel->findChildren<QDoubleSpinBox *>().value(0);
        ASSERT_TRUE(count) << "the panel offers no segment count";
        count->setValue(20);
        QCoreApplication::processEvents();

        window.applyToolForTest();
        QCoreApplication::processEvents();

        // Whatever the scene still points at has to be something it still has.
        const QVector<ShapeItem *> left = scene->shapes();
        EXPECT_EQ(left.size(), toSegments ? 20 : 1);
        EXPECT_TRUE(scene->activeItem() == nullptr || left.contains(scene->activeItem()))
            << "the active item was deleted and not forgotten";
        for (ShapeItem *picked : scene->editSelection()) {
            EXPECT_TRUE(left.contains(picked)) << "a deleted shape is still co-selected";
        }

        // The paint that used to be the crash.
        QImage image(400, 400, QImage::Format_ARGB32);
        QPainter painter(&image);
        scene->render(&painter);
        painter.end();
    }
}

// Picking several things in Physics mode and dragging one of them moves all of
// them. Taking only the body under the pointer made a multiple selection
// pointless: they could be picked together and then only moved one at a time.
TEST(Selection, DraggingOneOfSeveralPickedBodiesMovesThemAll)
{
    CanvasScene scene;
    scene.setEditorMode(EditorMode::Edit);

    QVector<ShapeItem *> made;
    for (int i = 0; i < 3; ++i) {
        auto *box = new RectangleItem;
        box->setRect(QRectF(0, 0, 40, 40));
        box->setPos(i * 120, 0);
        box->setName(QStringLiteral("box%1").arg(i));
        scene.addItem(box);
        made.append(box);
    }
    scene.notifyShapesChanged();

    scene.setEditorMode(EditorMode::Physics);
    scene.selectForPhysics(made.at(0), false);
    scene.selectForPhysics(made.at(1), true);
    scene.selectForPhysics(made.at(2), true);
    ASSERT_EQ(scene.physicsSelection().size(), 3);

    QVector<QPointF> before;
    for (ShapeItem *box : made)
        before << box->pos();

    // Grab the middle one and drag. The others are not under the pointer.
    const QPointF on = made.at(1)->mapToScene(made.at(1)->rect().center());
    const QPointF to = on + QPointF(70, 45);
    press(&scene, on, Qt::NoModifier);
    moveTo(&scene, to, Qt::NoModifier);
    release(&scene, to, Qt::NoModifier);

    for (int i = 0; i < made.size(); ++i) {
        EXPECT_NEAR(made.at(i)->pos().x(), before.at(i).x() + 70, 0.001)
            << made.at(i)->name().toStdString() << " did not travel with the selection";
        EXPECT_NEAR(made.at(i)->pos().y(), before.at(i).y() + 45, 0.001)
            << made.at(i)->name().toStdString() << " did not travel with the selection";
    }

    // One picked on its own still moves on its own.
    scene.selectForPhysics(made.at(0), false);
    const QPointF alone = made.at(0)->pos();
    const QPointF others = made.at(1)->pos();
    const QPointF grab = made.at(0)->mapToScene(made.at(0)->rect().center());
    press(&scene, grab, Qt::NoModifier);
    moveTo(&scene, grab + QPointF(30, 0), Qt::NoModifier);
    release(&scene, grab + QPointF(30, 0), Qt::NoModifier);
    EXPECT_NEAR(made.at(0)->pos().x(), alone.x() + 30, 0.001);
    EXPECT_EQ(made.at(1)->pos(), others) << "an unpicked body travelled with the dragged one";
}

// The other half of that: a press that never moves is a click, and a click
// without a modifier narrows a selection of several to the one clicked.
TEST(Selection, ClickingOneOfSeveralPickedBodiesNarrowsToIt)
{
    CanvasScene scene;
    scene.setEditorMode(EditorMode::Edit);

    QVector<ShapeItem *> made;
    for (int i = 0; i < 3; ++i) {
        auto *box = new RectangleItem;
        box->setRect(QRectF(0, 0, 40, 40));
        box->setPos(i * 120, 0);
        box->setName(QStringLiteral("box%1").arg(i));
        scene.addItem(box);
        made.append(box);
    }
    scene.notifyShapesChanged();

    scene.setEditorMode(EditorMode::Physics);
    scene.selectForPhysics(made.at(0), false);
    scene.selectForPhysics(made.at(1), true);
    scene.selectForPhysics(made.at(2), true);
    ASSERT_EQ(scene.physicsSelection().size(), 3);

    const QPointF on = made.at(2)->mapToScene(made.at(2)->rect().center());
    press(&scene, on, Qt::NoModifier);
    release(&scene, on, Qt::NoModifier);

    ASSERT_EQ(scene.physicsSelection().size(), 1) << "a click did not narrow the selection";
    EXPECT_EQ(scene.physicsSelection().first(), made.at(2));
}

// One rule about mass, said once. An outline has no area and so no mass, and a
// body of outlines starts Kinematic -- but that is a default, not a refusal:
// the Body panel sets it Dynamic afterwards, and a body gains area the moment
// a filled shape joins it. Creating used to refuse what the panel then allowed.
TEST(BodyTypes, AnOutlineBodyDefaultsToKinematicAndIsNotStuckThere)
{
    MainWindow window;
    CanvasScene *scene = window.findChild<CanvasScene *>();
    ASSERT_TRUE(scene);
    scene->setEditorMode(EditorMode::Edit);

    // A segment: two nodes, no interior.
    QPolygonF line;
    line << QPointF(0, 0) << QPointF(80, 0);
    auto *segment = new PolygonItem(line, false);
    segment->setPreferOutline(true);
    segment->setPos(100, 100);
    segment->setName(QStringLiteral("segment"));
    scene->addItem(segment);
    scene->notifyShapesChanged();
    ASSERT_FALSE(segment->hasInterior());

    scene->setEditorMode(EditorMode::Physics);
    scene->selectForPhysics(segment, false);
    // A plain double-click, which is the canvas asking for the body that moves.
    window.createBodyForTest(false);

    PhysicsBody *body = segment->body();
    ASSERT_TRUE(body) << "no body was made";
    EXPECT_EQ(body->props().type, physics::BodyType::Kinematic)
        << "an outline body should default to the type that can still be driven";

    // Nothing stops it being Dynamic afterwards -- which is what the Body
    // panel does, and what made the refusal at creation incoherent.
    body->props().type = physics::BodyType::Dynamic;
    body->notifyPropertyChanged();
    EXPECT_EQ(body->props().type, physics::BodyType::Dynamic);

    // Asking for Static gives Static: the default only stands in for Dynamic.
    CanvasScene second;
    second.setEditorMode(EditorMode::Edit);
    auto *other = new PolygonItem(line, false);
    other->setPreferOutline(true);
    other->setName(QStringLiteral("segment"));
    second.addItem(other);
    second.notifyShapesChanged();
    second.setEditorMode(EditorMode::Physics);
    second.selectForPhysics(other, false);
    PhysicsBody *made = second.createBodyFromSelection();
    ASSERT_TRUE(made);
    made->props().type = physics::BodyType::Static;
    EXPECT_EQ(made->props().type, physics::BodyType::Static);
}

// A ring closes on rounded boxes too. The packing takes each shape's share as
// the chord its face subtends, which is exact only while the corners are
// square: rounding cuts away the very material that was doing the touching, so
// a ring of rounded boxes stood with a gap at every joint, wider the rounder
// they were.
TEST(Alignment, ARingOfRoundedBoxesStillCloses)
{
    // The drawn outline, rounded corners and all.
    const auto drawn = [](ShapeItem *shape) {
        QPainterPath path;
        const QRectF own = shape->rect();
        if (shape->cornerRadius() > 0.0)
            path.addRoundedRect(own, shape->cornerRadius(), shape->cornerRadius());
        else
            path.addRect(own);
        return shape->mapToScene(path.toFillPolygon());
    };

    for (const qreal rho : { 0.0, 5.0, 12.0, 20.0 }) {
        CanvasScene scene;
        scene.setEditorMode(EditorMode::Edit);

        QVector<ShapeItem *> made;
        for (int i = 0; i < 12; ++i) {
            auto *box = new RectangleItem;
            box->setRect(QRectF(0, 0, 60, 40));
            box->setCornerRadius(rho);
            box->setPos(300 * std::cos(i * M_PI / 6), 300 * std::sin(i * M_PI / 6));
            // Turned to face the middle, the way they would be if they had
            // been dropped round a ring by hand: which edge is nearest the
            // centre is what the alignment goes by, and a box square to the
            // page says nothing about which edge was meant.
            box->setRotation(i * 30.0);
            box->setName(QStringLiteral("box%1").arg(i));
            scene.addItem(box);
            made.append(box);
        }
        scene.notifyShapesChanged();
        scene.selectShape(made.first());
        for (int i = 1; i < made.size(); ++i)
            scene.addToEditSelection(made.at(i));

        scene.alignSelection(CanvasScene::Align::RingOutside);

        // Every shape touches something. Asked of the nearest other shape
        // rather than of "the next one round", so the check does not depend on
        // sorting them into ring order first.
        for (int i = 0; i < made.size(); ++i) {
            const QPolygonF mine = drawn(made.at(i));
            qreal nearest = std::numeric_limits<qreal>::max();
            for (int j = 0; j < made.size(); ++j) {
                if (j == i)
                    continue;
                const QPolygonF other = drawn(made.at(j));
                for (const QPointF &p : mine)
                    for (const QPointF &q : other)
                        nearest = qMin(nearest, QLineF(p, q).length());
            }
            // Square corners close exactly. Rounded ones close to about a
            // quarter of the radius: the share each shape takes is worked out
            // from where its two corner arcs sit, which is exact only while
            // the face is long against the radius. It is an approximation, and
            // a declared one -- before it there was a gap of five and a half
            // pixels here at this radius.
            EXPECT_LT(nearest, rho > 0.0 ? rho / 4.0 + 0.1 : 0.01)
                << "corner radius " << rho << " left box " << i << " a gap of " << nearest
                << " from anything";
        }
    }
}

// Copy and Paste took shapes and nothing else, so a ray could be selected,
// Copy was greyed out, and there was no way to make a second one like it.
// They go through the same JSON the file uses, so the clipboard cannot drift
// from what a scene can hold.
TEST(Selection, ARayAndAnExplosionCopyAndPaste)
{
    if (!theClipboardAnswers())
        GTEST_SKIP() << "the system clipboard is held by something else on this machine";

    MainWindow window;
    CanvasScene *scene = window.findChild<CanvasScene *>();
    ASSERT_TRUE(scene);
    scene->setEditorMode(EditorMode::Physics);

    RayItem *ray = scene->addRay(QPointF(120, 80));
    ASSERT_TRUE(ray);
    ray->setAngleDegrees(37.0);
    ray->setLength(222.0);
    scene->notifyShapesChanged();
    scene->selectRay(ray);

    QAction *copy = window.findChild<QAction *>(QStringLiteral("actionCopy"));
    QAction *paste = window.findChild<QAction *>(QStringLiteral("actionPaste"));
    ASSERT_TRUE(copy);
    ASSERT_TRUE(paste);
    EXPECT_TRUE(copy->isEnabled()) << "a selected ray left Copy greyed out";

    copy->trigger();
    SKIP_IF_THE_CLIPBOARD_STOPPED_ANSWERING();
    EXPECT_TRUE(paste->isEnabled()) << "a copied ray left Paste greyed out";
    paste->trigger();

    ASSERT_EQ(scene->rays().size(), 2) << "pasting the ray made no second one";
    RayItem *copied = scene->rays().at(0) == ray ? scene->rays().at(1) : scene->rays().at(0);
    EXPECT_NE(copied->name(), ray->name()) << "the copy took the original's name";
    EXPECT_DOUBLE_EQ(copied->angleDegrees(), 37.0) << "the copy lost its angle";
    EXPECT_DOUBLE_EQ(copied->length(), 222.0) << "the copy lost its length";
    EXPECT_NE(copied->pos(), ray->pos()) << "the copy landed exactly on the original";

    // The same for a blast, and its settings come with it.
    ExplosionItem *blast = scene->addExplosion(QPointF(300, 200));
    ASSERT_TRUE(blast);
    blast->params()[QStringLiteral("radius")] = 123.0;
    scene->selectRay(nullptr);
    scene->selectExplosion(blast);
    EXPECT_TRUE(copy->isEnabled()) << "a selected explosion left Copy greyed out";

    copy->trigger();
    paste->trigger();
    ASSERT_EQ(scene->explosions().size(), 2);
    ExplosionItem *other = scene->explosions().at(0) == blast ? scene->explosions().at(1)
                                                             : scene->explosions().at(0);
    EXPECT_NE(other->name(), blast->name());
    EXPECT_DOUBLE_EQ(other->params().value(QStringLiteral("radius")).toDouble(), 123.0)
        << "the copy lost the blast's settings";
}

// Shift during a turn is the precision modifier: the shape takes a fifth of
// the mouse's swing, so a degree can be placed by hand. Accumulated rather
// than measured from where the drag began, so pressing or releasing Shift
// part-way through does not make the shape jump.
TEST(ShiftDrag, ShiftTurnsAShapeMoreSlowly)
{
    const auto turnBy = [](bool fine, bool halfway) {
        CanvasScene scene;
        scene.setEditorMode(EditorMode::Edit);
        auto *box = new RectangleItem;
        box->setRect(QRectF(0, 0, 100, 60));
        box->setPos(0, 0);
        box->setName(QStringLiteral("box"));
        scene.addItem(box);
        scene.notifyShapesChanged();
        scene.selectShape(box);
        scene.switchActiveToRotating();

        // The press has to land inside the shape, which is what tells the
        // scene a turn is beginning rather than a click on empty field.
        const QPointF pivot = box->mapToScene(box->origin());
        const QPointF grip = box->mapToScene(box->rect().center());
        const QPointF arm = grip - pivot;
        const auto swungBy = [&](qreal degrees) {
            const qreal r = qDegreesToRadians(degrees);
            return pivot + QPointF(arm.x() * std::cos(r) - arm.y() * std::sin(r),
                                   arm.x() * std::sin(r) + arm.y() * std::cos(r));
        };

        const Qt::KeyboardModifiers held = fine ? Qt::ShiftModifier : Qt::NoModifier;
        press(&scene, grip, halfway ? Qt::NoModifier : held);
        // A quarter turn, in two halves, so the modifier can change in between.
        moveTo(&scene, swungBy(45.0), halfway ? Qt::NoModifier : held);
        moveTo(&scene, swungBy(90.0), held);
        release(&scene, swungBy(90.0), held);
        return box->rotation();
    };

    const qreal free = turnBy(false, false);
    EXPECT_NEAR(free, 90.0, 0.001) << "a plain drag no longer follows the mouse";

    const qreal fine = turnBy(true, false);
    EXPECT_NEAR(fine, 18.0, 0.001) << "Shift should turn it a fifth as far";

    // Shift pressed half way: the first half at full speed, the second slowed.
    const qreal mixed = turnBy(true, true);
    EXPECT_NEAR(mixed, 45.0 + 9.0, 0.001)
        << "pressing Shift part-way made the shape jump instead of slowing down";
}

// With several shapes picked the property table speaks for all of them: typing
// a rotation turns the whole selection, not only the shape carrying the
// handles. One shape on its own is a selection of one and behaves as before.
TEST(Selection, TheRotationRowTurnsTheSelectionAsOnePiece)
{
    CanvasScene scene;
    scene.setEditorMode(EditorMode::Edit);

    QVector<ShapeItem *> made;
    for (int i = 0; i < 3; ++i) {
        auto *box = new RectangleItem;
        box->setRect(QRectF(0, 0, 60, 40));
        box->setPos(i * 100, 0);
        box->setRotation(i * 10.0);
        box->setName(QStringLiteral("box%1").arg(i));
        scene.addItem(box);
        made.append(box);
    }
    scene.notifyShapesChanged();
    scene.selectShape(made.at(0));
    scene.addToEditSelection(made.at(1));
    scene.addToEditSelection(made.at(2));
    ASSERT_EQ(scene.editSelection().size(), 2);

    std::unique_ptr<PropertyPane> pane(made.at(0)->makePropertyPane());
    ASSERT_TRUE(pane);
    pane->attach(made.at(0));

    const PropertyRow *rotation = nullptr;
    const std::vector<PropertyRow> rows = pane->rows(EditorMode::Edit);
    for (const PropertyRow &row : rows) {
        if (row.label == QStringLiteral("Rotation"))
            rotation = &row;
    }
    ASSERT_TRUE(rotation) << "the shape pane offers no Rotation row";

    // The selection turns as one piece: every shape gains the same angle and
    // orbits the group's pivot, so the arrangement survives.
    QVector<QPointF> before;
    for (ShapeItem *box : made)
        before << box->mapToScene(box->rect().center());
    const QPointF pivot = scene.editSelectionOrigin();

    rotation->setter(37.0);

    for (int i = 0; i < made.size(); ++i) {
        EXPECT_DOUBLE_EQ(made.at(i)->rotation(), i * 10.0 + 37.0)
            << made.at(i)->name().toStdString() << " did not gain the same angle as the rest";
        // Carried round the pivot, not spun where it stood.
        const QLineF was(pivot, before.at(i));
        const QLineF now(pivot, made.at(i)->mapToScene(made.at(i)->rect().center()));
        EXPECT_NEAR(now.length(), was.length(), 0.001)
            << made.at(i)->name().toStdString() << " changed its distance from the pivot";
        qreal swept = was.angle() - now.angle();
        while (swept < -180.0)
            swept += 360.0;
        while (swept > 180.0)
            swept -= 360.0;
        EXPECT_NEAR(swept, 37.0, 0.001)
            << made.at(i)->name().toStdString() << " did not orbit with the rest";
    }

    // Picked on its own, it is the only one that turns.
    scene.selectShape(made.at(1));
    std::unique_ptr<PropertyPane> alone(made.at(1)->makePropertyPane());
    alone->attach(made.at(1));
    for (const PropertyRow &row : alone->rows(EditorMode::Edit)) {
        if (row.label == QStringLiteral("Rotation"))
            row.setter(12.0);
    }
    EXPECT_DOUBLE_EQ(made.at(1)->rotation(), 12.0);
    EXPECT_DOUBLE_EQ(made.at(0)->rotation(), 37.0) << "a lone selection turned its neighbours";
}

// A shape dragged round and round kept the running total -- 1663 degrees,
// which is the same way up as 223 and reads like a fault. The drag now leaves
// the angle inside 0..360, and the table shows it that way.
TEST(ShiftDrag, TurningRoundAndRoundKeepsTheAngleInRange)
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
    scene.switchActiveToRotating();

    const QPointF pivot = box->mapToScene(box->origin());
    const QPointF grip = box->mapToScene(box->rect().center());
    const QPointF arm = grip - pivot;
    const auto swungBy = [&](qreal degrees) {
        const qreal r = qDegreesToRadians(degrees);
        return pivot + QPointF(arm.x() * std::cos(r) - arm.y() * std::sin(r),
                               arm.x() * std::sin(r) + arm.y() * std::cos(r));
    };

    // Four times round and a bit more, in steps small enough to follow.
    press(&scene, grip, Qt::NoModifier);
    for (int step = 1; step <= 50; ++step)
        moveTo(&scene, swungBy(step * 30.0), Qt::NoModifier);
    release(&scene, swungBy(1500.0), Qt::NoModifier);

    EXPECT_GE(box->rotation(), 0.0) << "the angle came out negative";
    EXPECT_LT(box->rotation(), 360.0)
        << "the angle kept the running total instead of coming back into range";
    EXPECT_NEAR(box->rotation(), 60.0, 0.001) << "1500 degrees is 60 degrees round";

    std::unique_ptr<PropertyPane> pane(box->makePropertyPane());
    pane->attach(box);
    for (const PropertyRow &row : pane->rows(EditorMode::Edit)) {
        if (row.label == QStringLiteral("Rotation"))
            EXPECT_NEAR(row.getter().toDouble(), 60.0, 0.001) << "the table shows it out of range";
    }
}

// Several shapes picked are not one shape with handles on it. The table used
// to show the lead shape's pane -- its Corner Radius, its Radius, its own Left
// and Top -- which said it was speaking for all of them while it spoke for
// one. What they share is the box round them and which way it faces.
TEST(Selection, SeveralShapesGetTheirOwnPropertyTable)
{
    MainWindow window;
    CanvasScene *scene = window.findChild<CanvasScene *>();
    ASSERT_TRUE(scene);
    scene->setEditorMode(EditorMode::Edit);

    QVector<ShapeItem *> made;
    for (int i = 0; i < 3; ++i) {
        auto *box = new RectangleItem;
        box->setRect(QRectF(0, 0, 60, 40));
        box->setPos(i * 100, 0);
        box->setCornerRadius(8.0);
        box->setName(QStringLiteral("box%1").arg(i));
        scene->addItem(box);
        made.append(box);
    }
    scene->notifyShapesChanged();

    PropertyPanel *panel = window.findChild<PropertyPanel *>();
    ASSERT_TRUE(panel);

    const auto labels = [&] {
        QStringList found;
        for (const PropertyRow &row : panel->rowsForTest())
            found << row.label;
        return found;
    };

    scene->selectShape(made.at(0));
    QCoreApplication::processEvents();
    EXPECT_TRUE(labels().contains(QStringLiteral("Corner Radius")))
        << "one shape on its own lost its own properties";

    scene->addToEditSelection(made.at(1));
    scene->addToEditSelection(made.at(2));
    QCoreApplication::processEvents();

    const QStringList many = labels();
    EXPECT_FALSE(many.contains(QStringLiteral("Corner Radius")))
        << "the selection's table is still showing one shape's corner radius";
    for (const char *wanted : { "Shapes", "Left", "Top", "Width", "Height", "Rotation" }) {
        EXPECT_TRUE(many.contains(QLatin1String(wanted)))
            << "the selection's table has no " << wanted << " row";
    }

    // Left moves everything, keeping the arrangement.
    const QRectF before = scene->editSelectionBounds();
    for (const PropertyRow &row : panel->rowsForTest()) {
        if (row.label == QStringLiteral("Left"))
            row.setter(before.left() + 50.0);
    }
    EXPECT_NEAR(scene->editSelectionBounds().left(), before.left() + 50.0, 0.001);
    EXPECT_NEAR(made.at(2)->pos().x() - made.at(0)->pos().x(), 200.0, 0.001)
        << "moving the selection changed the spacing inside it";

    // Width scales it, and the box comes out the size that was typed.
    for (const PropertyRow &row : panel->rowsForTest()) {
        if (row.label == QStringLiteral("Width"))
            row.setter(before.width() / 2.0);
    }
    EXPECT_NEAR(scene->editSelectionBounds().width(), before.width() / 2.0, 0.001)
        << "the box is not the width that was typed";
}

// Dragging on the canvas writes the same values the table shows, and nothing
// was telling the table. Turn a selection with the mouse and the Rotation row
// sat at whatever it read when the table was built -- 0, for a selection that
// had not been turned before.
TEST(Selection, TheTableFollowsATurnWhileTheMouseIsStillDown)
{
    MainWindow window;
    CanvasScene *scene = window.findChild<CanvasScene *>();
    ASSERT_TRUE(scene);
    scene->setEditorMode(EditorMode::Edit);

    QVector<ShapeItem *> made;
    for (int i = 0; i < 3; ++i) {
        auto *box = new RectangleItem;
        box->setRect(QRectF(0, 0, 60, 40));
        box->setPos(i * 100, 0);
        box->setName(QStringLiteral("box%1").arg(i));
        scene->addItem(box);
        made.append(box);
    }
    scene->notifyShapesChanged();
    scene->selectShape(made.at(0));
    scene->addToEditSelection(made.at(1));
    scene->addToEditSelection(made.at(2));
    scene->switchActiveToRotating();
    QCoreApplication::processEvents();

    PropertyPanel *panel = window.findChild<PropertyPanel *>();
    ASSERT_TRUE(panel);
    const auto shown = [&] {
        for (QDoubleSpinBox *box : panel->findChildren<QDoubleSpinBox *>()) {
            if (box->toolTip().contains(QLatin1String("one piece")))
                return box->value();
        }
        return -1.0;
    };
    ASSERT_NEAR(shown(), 0.0, 0.001) << "the selection's Rotation row is missing";

    // A quarter turn with the mouse, about the selection's pivot.
    const QPointF pivot = scene->editSelectionOrigin();
    const QPointF grip = made.at(0)->mapToScene(made.at(0)->rect().center());
    const QPointF arm = grip - pivot;
    const auto swungBy = [&](qreal degrees) {
        const qreal r = qDegreesToRadians(degrees);
        return pivot + QPointF(arm.x() * std::cos(r) - arm.y() * std::sin(r),
                               arm.x() * std::sin(r) + arm.y() * std::cos(r));
    };
    press(scene, grip, Qt::NoModifier);
    moveTo(scene, swungBy(45.0), Qt::NoModifier);
    QCoreApplication::processEvents();
    // Still dragging. A single shape's table keeps up while the mouse is down,
    // and the selection's has to as well -- catching up only on release is the
    // thing that looked like it was stuck at zero.
    EXPECT_NEAR(shown(), 45.0, 0.001) << "the table waits for the mouse to be let go";

    moveTo(scene, swungBy(90.0), Qt::NoModifier);
    QCoreApplication::processEvents();
    EXPECT_NEAR(shown(), 90.0, 0.001) << "the table stopped following part-way through";

    release(scene, swungBy(90.0), Qt::NoModifier);
    QCoreApplication::processEvents();
    ASSERT_NEAR(made.at(0)->rotation(), 90.0, 0.001) << "the drag did not turn the selection";
    EXPECT_NEAR(shown(), 90.0, 0.001) << "the table lost the value when the drag finished";
}

// The same for moving: Left and Top follow the drag rather than jumping when
// the mouse is let go.
TEST(Selection, TheTableFollowsAMoveWhileTheMouseIsStillDown)
{
    MainWindow window;
    CanvasScene *scene = window.findChild<CanvasScene *>();
    ASSERT_TRUE(scene);
    scene->setEditorMode(EditorMode::Edit);
    // Whatever the saved settings say: snapping would round the drag, which is
    // nothing to do with whether the table keeps up with it.
    scene->setSnapToGrid(false);

    QVector<ShapeItem *> made;
    for (int i = 0; i < 2; ++i) {
        auto *box = new RectangleItem;
        box->setRect(QRectF(0, 0, 60, 40));
        box->setPos(i * 100, 0);
        box->setName(QStringLiteral("box%1").arg(i));
        scene->addItem(box);
        made.append(box);
    }
    scene->notifyShapesChanged();
    scene->selectShape(made.at(0));
    scene->addToEditSelection(made.at(1));
    QCoreApplication::processEvents();

    PropertyPanel *panel = window.findChild<PropertyPanel *>();
    ASSERT_TRUE(panel);
    const auto left = [&] {
        for (QDoubleSpinBox *box : panel->findChildren<QDoubleSpinBox *>()) {
            if (box->toolTip().contains(QLatin1String("left edge of everything")))
                return box->value();
        }
        return -99999.0;
    };
    const qreal was = scene->editSelectionBounds().left();
    ASSERT_NEAR(left(), was, 0.001) << "the selection's Left row is missing";

    const QPointF grip = made.at(0)->mapToScene(made.at(0)->rect().center());
    press(scene, grip, Qt::NoModifier);
    moveTo(scene, grip + QPointF(70, 0), Qt::NoModifier);
    QCoreApplication::processEvents();
    EXPECT_NEAR(left(), was + 70.0, 0.001) << "Left waits for the mouse to be let go";
    release(scene, grip + QPointF(70, 0), Qt::NoModifier);
    QCoreApplication::processEvents();
    EXPECT_NEAR(left(), was + 70.0, 0.001) << "Left lost the value when the drag finished";
}

// A control is declared in the document and its value is run state, the same
// bargain a variable makes. The declaration survives the file; the value does
// not need to, because a run starts it where the scene says.
TEST(Shapes, ControlsSurviveTheFile)
{
    CanvasScene scene;
    SceneControl tick;
    tick.id = QStringLiteral("go");
    tick.kind = SceneControl::Kind::Checkbox;
    tick.initial = true;
    SceneControl pull;
    pull.id = QStringLiteral("speed");
    pull.kind = SceneControl::Kind::Slider;
    pull.minimum = -20.0;
    pull.maximum = 80.0;
    pull.step = 2.5;
    pull.decimals = 2;
    pull.initial = 15.0;
    SceneControl press;
    press.id = QStringLiteral("fire");
    press.kind = SceneControl::Kind::Button;
    scene.setControls({ tick, pull, press });

    CanvasScene read;
    QString error;
    ASSERT_TRUE(SceneSerializer::load(&read, SceneSerializer::save(&scene), &error))
        << error.toStdString();

    ASSERT_EQ(read.controls().size(), 3);
    const SceneControl &back = read.controls().at(1);
    EXPECT_EQ(back.id, QStringLiteral("speed"));
    EXPECT_EQ(back.kind, SceneControl::Kind::Slider);
    EXPECT_DOUBLE_EQ(back.minimum, -20.0);
    EXPECT_DOUBLE_EQ(back.maximum, 80.0);
    EXPECT_DOUBLE_EQ(back.step, 2.5);
    EXPECT_EQ(back.decimals, 2);
    EXPECT_DOUBLE_EQ(back.startingValue().toDouble(), 15.0);

    EXPECT_EQ(read.controls().at(0).kind, SceneControl::Kind::Checkbox);
    EXPECT_TRUE(read.controls().at(0).startingValue().toBool());
    EXPECT_EQ(read.controls().at(2).kind, SceneControl::Kind::Button);
    EXPECT_FALSE(read.controls().at(2).startingValue().toBool()) << "a button starts up";

    // And each one is readable straight away, before any run.
    EXPECT_DOUBLE_EQ(read.controlValue(QStringLiteral("speed")).toDouble(), 15.0);
    EXPECT_TRUE(read.controlEnabled(QStringLiteral("go")));

    // What a rule names of a control goes in as the two things it is -- which
    // control, which property -- so the punctuation the cards show an address
    // with stays theirs, and changing it cannot leave a saved scene naming
    // nothing. A file written while it was one flat string still reads.
    Rule rule;
    rule.conditions.resize(1);
    rule.conditions[0].subjectName = Rule::controls();
    rule.conditions[0].conditionKey = pull.valueKey();
    rule.conditions[0].compare = Rule::Compare::Greater;
    rule.conditions[0].conditionValue = 10.0;
    rule.actions.resize(1);
    rule.actions[0].targetName = Rule::controls();
    rule.actions[0].propertyKey = tick.enabledKey();
    rule.actions[0].sourceObject = Rule::controls();
    rule.actions[0].sourceProperty = pull.valueKey();
    scene.setRules({ rule });

    const QJsonObject document = SceneSerializer::save(&scene);
    const QJsonObject written = document.value("rules").toArray().first().toObject();
    for (const char *field : { "watch", "property", "sourceProperty" }) {
        const QJsonObject address = written.value(field).toObject();
        EXPECT_FALSE(address.isEmpty()) << field << " was not written as an address";
        EXPECT_FALSE(address.value("control").toString().isEmpty());
        EXPECT_FALSE(address.value("property").toString().contains(
            SceneControl::separator()))
            << "the file carries no punctuation of the cards'";
    }
    EXPECT_EQ(written.value("watch").toObject().value("control").toString(),
              QStringLiteral("speed"));
    EXPECT_EQ(written.value("watch").toObject().value("property").toString(),
              SceneControl::valueProperty());

    // And the same scene as it was written when an address was one string with
    // a dot in it.
    QJsonObject flat = written;
    flat.insert("watch", QStringLiteral("speed.value"));
    flat.insert("property", QStringLiteral("go.enabled"));
    flat.insert("sourceProperty", QStringLiteral("speed.value"));
    QJsonObject legacy = document;
    legacy.insert("rules", QJsonArray { flat });

    CanvasScene before;
    ASSERT_TRUE(SceneSerializer::load(&before, legacy, &error)) << error.toStdString();
    ASSERT_EQ(before.rules().size(), 1);
    EXPECT_EQ(before.rules().first().conditions.first().conditionKey, pull.valueKey())
        << "a rule written with a dot no longer names anything";
    EXPECT_EQ(before.rules().first().actions.first().propertyKey, tick.enabledKey());
    EXPECT_EQ(before.rules().first().actions.first().sourceProperty, pull.valueKey());

    // A caption is only what it says: the id is what a rule holds on to, so
    // changing one leaves every rule alone.
    EXPECT_EQ(read.controlCaption(QStringLiteral("fire")), QStringLiteral("fire"))
        << "a control with no caption says its id";
    press.caption = QStringLiteral("Fire!");
    scene.setControls({ tick, pull, press });
    EXPECT_EQ(scene.controlCaption(QStringLiteral("fire")), QStringLiteral("Fire!"));

    CanvasScene captioned;
    ASSERT_TRUE(SceneSerializer::load(&captioned, SceneSerializer::save(&scene), &error))
        << error.toStdString();
    EXPECT_EQ(captioned.controls().at(2).caption, QStringLiteral("Fire!"));
    EXPECT_EQ(captioned.rules().first().conditions.first().conditionKey, pull.valueKey());
}
