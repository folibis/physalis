#include "CanvasScene.h"
#include "OptionsDialog.h"
#include "PhysicsBody.h"
#include "RectangleItem.h"
#include "SimulationController.h"

#include <QApplication>
#include <QGraphicsSceneMouseEvent>
#include <gtest/gtest.h>

// Shapes lying one over another -- balls on a table drawn after them. A click
// takes the one on top; Ctrl+click steps down to the one beneath, and round to
// the top again. A plain press on the shape already selected keeps it.

namespace {

const QPointF kBallCentre(120, 120);

void send(CanvasScene &scene, QEvent::Type type, const QPointF &at, Qt::KeyboardModifiers modifiers = {})
{
    QGraphicsSceneMouseEvent event(type);
    event.setScenePos(at);
    event.setScreenPos(at.toPoint());
    event.setButton(Qt::LeftButton);
    event.setButtons(type == QEvent::GraphicsSceneMouseRelease ? Qt::NoButton : Qt::LeftButton);
    event.setModifiers(modifiers);
    QCoreApplication::sendEvent(&scene, &event);
}

void click(CanvasScene &scene, const QPointF &at, Qt::KeyboardModifiers modifiers = {})
{
    send(scene, QEvent::GraphicsSceneMousePress, at, modifiers);
    send(scene, QEvent::GraphicsSceneMouseRelease, at, modifiers);
}

RectangleItem *rectangle(CanvasScene &scene, const QString &name, const QRectF &rect)
{
    auto *shape = new RectangleItem;
    shape->setRect(QRectF(0, 0, rect.width(), rect.height()));
    shape->setPos(rect.topLeft());
    shape->setName(name);
    scene.addItem(shape);
    scene.notifyShapesChanged();
    return shape;
}

// The ball first and the table after it, so the table is on top.
void layTable(CanvasScene &scene)
{
    rectangle(scene, QStringLiteral("ball"), QRectF(100, 100, 40, 40));
    rectangle(scene, QStringLiteral("table"), QRectF(0, 0, 400, 300));
}

QString selectedForPhysics(const CanvasScene &scene)
{
    return scene.physicsSelection().size() == 1 ? scene.physicsSelection().first()->name() : QString();
}

} // namespace

TEST(OverlapSelect, CtrlClickStepsDownInPhysicsMode)
{
    CanvasScene scene;
    scene.setSimulationEngineName(QStringLiteral("Box2D"));
    layTable(scene);
    scene.setEditorMode(EditorMode::Physics);

    click(scene, kBallCentre);
    EXPECT_EQ(selectedForPhysics(scene), QStringLiteral("table")) << "a click takes the shape on top";
    click(scene, kBallCentre, Qt::ControlModifier);
    EXPECT_EQ(selectedForPhysics(scene), QStringLiteral("ball")) << "Ctrl+click reaches the one beneath";
    click(scene, kBallCentre, Qt::ControlModifier);
    EXPECT_EQ(selectedForPhysics(scene), QStringLiteral("table")) << "and again goes round to the top";

    click(scene, kBallCentre, Qt::ControlModifier);
    ASSERT_EQ(selectedForPhysics(scene), QStringLiteral("ball"));
    click(scene, kBallCentre);
    EXPECT_EQ(selectedForPhysics(scene), QStringLiteral("ball"))
        << "a plain click on the selected ball keeps it, table or no table";
}

TEST(OverlapSelect, ADoubleClickMakesABodyOfWhatWasReached)
{
    CanvasScene scene;
    scene.setSimulationEngineName(QStringLiteral("Box2D"));
    layTable(scene);
    scene.setEditorMode(EditorMode::Physics);

    click(scene, kBallCentre, Qt::ControlModifier);
    ASSERT_EQ(selectedForPhysics(scene), QStringLiteral("table"));
    click(scene, kBallCentre, Qt::ControlModifier);
    ASSERT_EQ(selectedForPhysics(scene), QStringLiteral("ball"));

    QString requestedFor;
    QObject::connect(&scene, &CanvasScene::createBodyRequested, [&] { requestedFor = selectedForPhysics(scene); });
    send(scene, QEvent::GraphicsSceneMousePress, kBallCentre);
    send(scene, QEvent::GraphicsSceneMouseRelease, kBallCentre);
    send(scene, QEvent::GraphicsSceneMouseDoubleClick, kBallCentre);
    send(scene, QEvent::GraphicsSceneMouseRelease, kBallCentre);
    EXPECT_EQ(requestedFor, QStringLiteral("ball")) << "the body is for the ball, not the table over it";
}

TEST(OverlapSelect, TheShapeReachedCanBeDragged)
{
    CanvasScene scene;
    scene.setSimulationEngineName(QStringLiteral("Box2D"));
    layTable(scene);
    scene.setEditorMode(EditorMode::Physics);

    click(scene, kBallCentre, Qt::ControlModifier);
    click(scene, kBallCentre, Qt::ControlModifier);
    ASSERT_EQ(selectedForPhysics(scene), QStringLiteral("ball"));

    ShapeItem *ball = scene.physicsSelection().first();
    const QPointF before = ball->pos();
    send(scene, QEvent::GraphicsSceneMousePress, kBallCentre);
    send(scene, QEvent::GraphicsSceneMouseMove, kBallCentre + QPointF(40, 0));
    send(scene, QEvent::GraphicsSceneMouseRelease, kBallCentre + QPointF(40, 0));
    EXPECT_GT(ball->pos().x(), before.x() + 20) << "pressing on the ball under the table drags the ball";
}

TEST(OverlapSelect, CtrlClickStepsDownInEditMode)
{
    CanvasScene scene;
    layTable(scene);
    scene.setEditorMode(EditorMode::Edit);

    click(scene, kBallCentre);
    ASSERT_TRUE(scene.activeItem());
    EXPECT_EQ(scene.activeItem()->name(), QStringLiteral("table")) << "a click takes the shape on top";
    click(scene, kBallCentre, Qt::ControlModifier);
    ASSERT_TRUE(scene.activeItem());
    EXPECT_EQ(scene.activeItem()->name(), QStringLiteral("ball")) << "Ctrl+click reaches the one beneath";
    click(scene, kBallCentre, Qt::ControlModifier);
    ASSERT_TRUE(scene.activeItem());
    EXPECT_EQ(scene.activeItem()->name(), QStringLiteral("table")) << "and round to the top again";
}

TEST(OverlapSelect, ABallUnderTheTableCanBeShot)
{
    CanvasScene scene;
    scene.setSimulationEngineName(QStringLiteral("Box2D"));
    RectangleItem *ballShape = rectangle(scene, QStringLiteral("ball"), QRectF(100, 100, 40, 40));
    scene.setEditorMode(EditorMode::Physics);
    scene.selectForPhysics(ballShape, true);
    PhysicsBody *ball = scene.createBodyFromSelection();
    scene.clearPhysicsSelection();
    ASSERT_TRUE(ball);
    ball->shot().enabled = true;

    RectangleItem *tableShape = rectangle(scene, QStringLiteral("table"), QRectF(0, 0, 400, 300));
    scene.selectForPhysics(tableShape, true);
    PhysicsBody *table = scene.createBodyFromSelection();
    scene.clearPhysicsSelection();
    ASSERT_TRUE(table);
    table->props().type = physics::BodyType::Static;

    SimulationController sim(&scene, nullptr);
    sim.setEngineName(QStringLiteral("Box2D"));
    PhysicsBody *shotAt = nullptr;
    QObject::connect(&scene, &CanvasScene::shotReleased, [&](PhysicsBody *body, const QPointF &) { shotAt = body; });
    sim.start();
    sim.stepFrame();

    const QPointF centre = ball->centerOfMassScenePos();
    EXPECT_TRUE(scene.beginShot(centre)) << "the table on top does not stop the ball being aimed";
    scene.aimShot(centre + QPointF(-100, 0));
    scene.releaseShot();
    EXPECT_EQ(shotAt, ball) << "and it is the ball that is shot";
    sim.stop();
}

// Anchors are drawn see-through, how much so a setting.
TEST(OverlapSelect, AnchorOpacityIsASetting)
{
    CanvasScene scene;
    EXPECT_LT(scene.jointAnchorOpacity(), 100) << "anchors start out see-through";
    scene.setJointAnchorOpacity(35);
    EXPECT_EQ(scene.jointAnchorOpacity(), 35);

    OptionsDialog::Settings given;
    given.jointAnchorOpacity = 45;
    OptionsDialog dialog(given);
    EXPECT_EQ(dialog.settings().jointAnchorOpacity, 45) << "Options shows it and hands it back";
}
