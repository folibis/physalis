#include "CanvasScene.h"
#include "PhysicsBody.h"
#include "RectangleItem.h"
#include "SceneSerializer.h"
#include "ShapeItem.h"
#include "SimulationController.h"

#include <QApplication>
#include <gtest/gtest.h>
#include <QImage>
#include <QPainter>
#include <cstdio>

static const char *kScratch = "";

// Rolls a block along a floor made of two abutting slabs and reports how far
// it got. A square corner catches on the join; a rounded one rides over it.
static qreal distanceTravelled(qreal cornerRadius)
{
    CanvasScene scene;
    scene.setSimulationEngineName(QStringLiteral("Box2D"));

    // The second slab sits slightly proud, so there is a lip to catch on.
    for (int i = 0; i < 2; ++i) {
        auto *slab = new RectangleItem;
        slab->setRect(QRectF(0, 0, 400, 60));
        slab->setPos(-400 + i * 400, 200 - i * 6);
        slab->setName(QStringLiteral("floor%1").arg(i));
        scene.addItem(slab);
    }
    auto *block = new RectangleItem;
    block->setRect(QRectF(0, 0, 60, 60));
    block->setPos(-360, 130);
    block->setName(QStringLiteral("block"));
    block->setCornerRadius(cornerRadius);
    scene.addItem(block);
    scene.notifyShapesChanged();
    scene.setEditorMode(EditorMode::Physics);

    for (ShapeItem *s : scene.shapes()) {
        scene.selectForPhysics(s, true);
        PhysicsBody *body = scene.createBodyFromSelection();
        if (s->name() != QLatin1String("block"))
            body->props().type = physics::BodyType::Static;
        else
            body->props().linearVelocity = QPointF(3.0, 0.0);   // m/s, not pixels
        scene.clearPhysicsSelection();
    }

    SimulationController sim(&scene, nullptr);
    sim.setEngineName(QStringLiteral("Box2D"));
    sim.start();
    const qreal startX = block->sceneBoundingRect().center().x();
    for (int f = 0; f < 240; ++f)
        sim.stepFrame();
    const qreal travelled = block->sceneBoundingRect().center().x() - startX;
    sim.stop();
    return travelled;
}

TEST(CornerRadius, Behaves)
{
    RectangleItem probe;
    probe.setRect(QRectF(0, 0, 200, 100));
    probe.setCornerRadius(1000.0);
    EXPECT_TRUE(qFuzzyCompare(probe.cornerRadius(), 50.0)) << "a huge radius caps at half the shorter side" << " -- " << (QStringLiteral("%1").arg(probe.cornerRadius())).toStdString();
    EXPECT_TRUE(qFuzzyCompare(probe.cornerRadius(), qMin(200.0, 100.0) / 2.0)) << "at that cap the shape is a capsule";
    probe.setCornerRadius(-5.0);
    EXPECT_TRUE(qFuzzyIsNull(probe.cornerRadius())) << "a negative radius becomes zero";

    probe.setCornerRadius(50.0);
    const QPainterPath rounded = probe.shape();
    probe.setCornerRadius(0.0);
    const QPainterPath square = probe.shape();
    EXPECT_TRUE(rounded != square) << "a rounded outline differs from a square one";
    EXPECT_TRUE(rounded.boundingRect().width() <= square.boundingRect().width() + 0.01) << "and does not grow the shape" << " -- " << (QStringLiteral("%1 vs %2").arg(rounded.boundingRect().width())
              .arg(square.boundingRect().width())).toStdString();

    const qreal squareRun = distanceTravelled(0.0);
    const qreal roundRun = distanceTravelled(30.0);
    EXPECT_TRUE(roundRun > squareRun + 20.0) << "rounding lets it ride over the lip";

    CanvasScene scene;
    auto *saved = new RectangleItem;
    saved->setRect(QRectF(0, 0, 200, 100));
    saved->setName(QStringLiteral("pill"));
    saved->setCornerRadius(37.5);
    scene.addItem(saved);
    scene.notifyShapesChanged();
    QString error;
    const QString path = QStringLiteral("cornerradius.phys");
    SceneSerializer::saveToFile(&scene, path, &error);
    CanvasScene back;
    SceneSerializer::loadFromFile(&back, path, &error);
    EXPECT_TRUE(!back.shapes().isEmpty()
              && qFuzzyCompare(back.shapes().first()->cornerRadius(), 37.5)) << "the radius comes back" << " -- " << (back.shapes().isEmpty() ? QStringLiteral("no shapes")
                                  : QString::number(back.shapes().first()->cornerRadius())).toStdString();
}
