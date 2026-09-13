#include "CanvasScene.h"
#include "PhysicsBody.h"
#include "PolygonItem.h"
#include "RectangleItem.h"
#include "ShapeItem.h"
#include "SimulationController.h"
#include <QApplication>
#include <gtest/gtest.h>

// Every shape carries the index of its name in Box2D's user data, and that is
// how a rule, a log row or an event finds it again. A smooth chain is built by
// b2CreateChain rather than shape by shape, and its def has a user data field
// of its own -- left unset, every segment came back with none, which reads as
// index zero. So the chain answered to whichever shape happened to be first,
// that shape stopped answering to itself, and the chain could not be named at
// all.
TEST(ChainName, Behaves)
{
    CanvasScene scene;
    scene.setEditorMode(EditorMode::Physics);

    // Shape one: an ordinary box, so it takes name index 0.
    auto *box = new RectangleItem;
    box->setRect(QRectF(0, 0, 40, 40));
    box->setPos(-300, -200);
    box->setName(QStringLiteral("box"));
    box->part().params["friction"] = 0.11;
    scene.addItem(box);

    // Shape two: a smooth chain of five points, which takes the b2CreateChain
    // path rather than one segment per edge.
    QPolygonF pts;
    pts << QPointF(0, 0) << QPointF(100, 10) << QPointF(200, 0)
        << QPointF(300, 10) << QPointF(400, 0);
    auto *rail = new PolygonItem(pts, false);
    rail->setPos(0, 100);
    rail->setName(QStringLiteral("rail"));
    rail->setSmoothChain(true);
    rail->part().params["friction"] = 0.77;
    scene.addItem(rail);
    scene.notifyShapesChanged();

    scene.selectForPhysics(box, true);
    PhysicsBody *b1 = scene.createBodyFromSelection();
    scene.clearPhysicsSelection();
    scene.selectForPhysics(rail, true);
    PhysicsBody *b2 = scene.createBodyFromSelection();
    b2->props().type = physics::BodyType::Static;
    scene.clearPhysicsSelection();

    SimulationController sim(&scene, nullptr);
    sim.setEngineName(QStringLiteral("Box2D"));
    sim.start();
    sim.stepFrame();
    const QVariant boxF  = sim.readValue(QStringLiteral("box"),  QStringLiteral("friction"));
    const QVariant railF = sim.readValue(QStringLiteral("rail"), QStringLiteral("friction"));

    EXPECT_TRUE(railF.isValid()) << "the chain answers to its own name";
    EXPECT_NEAR(railF.toDouble(), 0.77, 0.01) << "with its own material";
    EXPECT_TRUE(boxF.isValid()) << "and the other shape still answers to its";
    EXPECT_NEAR(boxF.toDouble(), 0.11, 0.01)
        << "with its own, rather than the chain's -- both landing on index zero"
        << " is how one shape came to report another's";

    sim.stop();
}
