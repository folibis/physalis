#include "CanvasScene.h"
#include "PhysicsBody.h"
#include "PolygonItem.h"
#include "RectangleItem.h"
#include "ShapeItem.h"
#include "SimulationController.h"

#include <QApplication>
#include <gtest/gtest.h>

namespace {

PolygonItem *polyline(CanvasScene *scene, int points, const char *name)
{
    QPolygonF pts;
    for (int i = 0; i < points; ++i)
        pts << QPointF(i * 60, (i % 2) * 10);
    auto *item = new PolygonItem(pts, false);
    item->setName(QString::fromLatin1(name));
    scene->addItem(item);
    scene->notifyShapesChanged();
    return item;
}

} // namespace

// Only a filled shape has area, and only area gives mass. A body made of
// outlines cannot be dynamic -- it is dropped at the start of every run as
// having none -- so grouping one must not hand back a body that is already
// doomed. Kinematic is the closest thing that still works: driven by a
// velocity, simply not pushed around.
TEST(OutlineBody, Behaves)
{
    CanvasScene scene;
    scene.setEditorMode(EditorMode::Physics);

    PolygonItem *rail = polyline(&scene, 5, "rail");
    EXPECT_FALSE(rail->hasInterior()) << "an open polyline encloses nothing";
    EXPECT_FALSE(rail->drawsFilled()) << "so it is not drawn filled either";

    scene.selectForPhysics(rail, true);
    PhysicsBody *body = scene.createBodyFromSelection();
    scene.clearPhysicsSelection();
    ASSERT_TRUE(body != nullptr);
    EXPECT_EQ(body->props().type, physics::BodyType::Kinematic)
        << "a body of outlines starts kinematic, not dynamic";

    // And the engine keeps it, rather than dropping it for having no mass.
    SimulationController sim(&scene, nullptr);
    sim.setEngineName(QStringLiteral("Box2D"));
    sim.start();
    EXPECT_TRUE(sim.skippedBodies().isEmpty())
        << "kept by the run" << " -- " << sim.skippedBodies().join(", ").toStdString();
    sim.stop();

    // A closed convex shape still encloses something, so nothing changes there.
    auto *box = new RectangleItem;
    box->setRect(QRectF(0, 0, 40, 40));
    box->setName(QStringLiteral("box"));
    scene.addItem(box);
    scene.notifyShapesChanged();
    scene.selectForPhysics(box, true);
    PhysicsBody *solid = scene.createBodyFromSelection();
    scene.clearPhysicsSelection();
    ASSERT_TRUE(solid != nullptr);
    EXPECT_TRUE(box->hasInterior());
    EXPECT_EQ(solid->props().type, physics::BodyType::Dynamic)
        << "a filled shape still makes an ordinary dynamic body";
}
