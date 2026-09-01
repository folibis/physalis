#include "CanvasScene.h"
#include "MainWindow.h"
#include "PhysicsBody.h"
#include "RayItem.h"
#include "RectangleItem.h"
#include "ShapeItem.h"
#include "SimulationController.h"

#include <QApplication>
#include <gtest/gtest.h>

TEST(RayShot, Renders)
{
    MainWindow window;
    window.resize(1000, 560);
    window.show();
    for (int i = 0; i < 25; ++i) QCoreApplication::processEvents();
    auto *scene = window.findChild<CanvasScene *>();
    auto *sim = window.findChild<SimulationController *>();
    scene->setEditorMode(EditorMode::Physics);
    scene->world().gravity = QPointF(0.0, 0.0);

    auto *wall = new RectangleItem;
    wall->setRect(QRectF(0, 0, 50, 300));
    wall->setPos(180, -150);
    wall->setName(QStringLiteral("wall"));
    scene->addItem(wall);
    scene->notifyShapesChanged();
    scene->selectForPhysics(wall, true);
    scene->createBodyFromSelection()->props().type = physics::BodyType::Static;
    scene->clearPhysicsSelection();

    RayItem *seeing = scene->addRay(QPointF(-260, -40));
    seeing->setLength(600.0);
    RayItem *missing = scene->addRay(QPointF(-260, 90));
    missing->setAngleDegrees(-25.0);
    missing->setLength(380.0);
    scene->selectRay(seeing);

    // Stopped first, so the selection outline is visible; it hides while a
    // run owns the scene.
    for (int i = 0; i < 20; ++i) QCoreApplication::processEvents();
    window.grab().save(QStringLiteral("rays_selected.png"));

    sim->start();
    for (int f = 0; f < 5; ++f) sim->stepFrame();
    for (int i = 0; i < 20; ++i) QCoreApplication::processEvents();
    window.grab().save(QStringLiteral("rays.png"));
    sim->stop();
    window.close();
}
