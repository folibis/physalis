#include "CanvasScene.h"
#include "PhysicsBody.h"
#include "PolygonItem.h"
#include "RectangleItem.h"
#include "SceneSerializer.h"
#include "ShapeItem.h"
#include "SimulationController.h"

#include <QApplication>
#include <gtest/gtest.h>
#include <cstdio>

// Slides a square block along a drawn zig-zag line and reports how far it got
// before the joins between edges stopped it.
static qreal slideAlong(bool smooth, int pointCount, bool *built)
{
    CanvasScene scene;
    scene.setSimulationEngineName(QStringLiteral("Box2D"));

    QPolygonF line;
    for (int i = 0; i < pointCount; ++i)
        line << QPointF(-400 + i * 200.0, 200 + (i % 2 ? 6 : 0));   // a slight kink at each join
    auto *ground = new PolygonItem(line, false);   // open: a chain
    ground->setName(QStringLiteral("ground"));
    scene.addItem(ground);
    ground->setSmoothChain(smooth);

    auto *block = new RectangleItem;
    block->setRect(QRectF(0, 0, 50, 50));
    block->setPos(-380, 130);
    block->setName(QStringLiteral("block"));
    scene.addItem(block);
    scene.notifyShapesChanged();
    scene.setEditorMode(EditorMode::Physics);

    scene.selectForPhysics(ground, true);
    scene.createBodyFromSelection()->props().type = physics::BodyType::Static;
    scene.clearPhysicsSelection();
    scene.selectForPhysics(block, true);
    PhysicsBody *moving = scene.createBodyFromSelection();
    moving->props().linearVelocity = QPointF(3.0, 0.0);
    scene.clearPhysicsSelection();

    SimulationController sim(&scene, nullptr);
    sim.setEngineName(QStringLiteral("Box2D"));
    sim.start();
    *built = sim.skippedJoints().isEmpty();
    const qreal startX = block->sceneBoundingRect().center().x();
    for (int f = 0; f < 300; ++f)
        sim.stepFrame();
    const qreal gone = block->sceneBoundingRect().center().x() - startX;
    sim.stop();
    return gone;
}

TEST(SmoothChain, Behaves)
{
    bool a = false, b = false;
    const qreal segments = slideAlong(false, 6, &a);
    const qreal chain = slideAlong(true, 6, &b);
    EXPECT_TRUE(a && b) << "both worlds built";
    EXPECT_TRUE(chain > segments + 20.0) << "the chain lets it slide further" << " -- " << (QStringLiteral("%1 vs %2").arg(chain, 0, 'f', 0).arg(segments, 0, 'f', 0)).toStdString();

    // b2CreateChain asserts below four points, which would abort the process.
    bool built = false;
    const qreal shortLine = slideAlong(true, 3, &built);
    EXPECT_TRUE(built) << "a three-point line still simulates" << " -- " << (QStringLiteral("travelled %1").arg(shortLine, 0, 'f', 0)).toStdString();

    CanvasScene scene;
    QPolygonF line;
    line << QPointF(0, 0) << QPointF(100, 0) << QPointF(200, 20) << QPointF(300, 0);
    auto *poly = new PolygonItem(line, false);
    poly->setName(QStringLiteral("ramp"));
    poly->setSmoothChain(true);
    scene.addItem(poly);
    scene.notifyShapesChanged();
    QString error;
    SceneSerializer::saveToFile(&scene, QStringLiteral("smoothchain.phys"), &error);
    CanvasScene back;
    SceneSerializer::loadFromFile(&back, QStringLiteral("smoothchain.phys"), &error);
    EXPECT_TRUE(!back.shapes().isEmpty() && back.shapes().first()->smoothChain()) << "it comes back on";
}
