#include "CanvasScene.h"
#include "MainWindow.h"
#include "RayItem.h"
#include "RectangleItem.h"
#include "SimulationController.h"

#include <QApplication>
#include <QDir>
#include <QImage>
#include <QPainter>
#include <gtest/gtest.h>

namespace {

bool near(const QColor &a, const QColor &b, int tolerance = 50)
{
    return qAbs(a.red() - b.red()) <= tolerance && qAbs(a.green() - b.green()) <= tolerance
           && qAbs(a.blue() - b.blue()) <= tolerance && a.alpha() > 80;
}

}

// Selecting a ray draws a contour around it. Drawing along the ray instead
// only made the line look thicker, which is not an outline.
TEST(RayOutline, SitsBesideTheLine)
{
    CanvasScene scene;
    scene.setEditorMode(EditorMode::Physics);

    RayItem *ray = scene.addRay(QPointF(-200, 0));
    ray->setAngleDegrees(0);
    ray->setLength(300);
    scene.selectRay(ray);

    // 1:1, so a scene unit is a pixel: the ray runs along row 20.
    const QRectF source(-220, -20, 340, 40);
    QImage image(340, 40, QImage::Format_ARGB32);
    image.fill(Qt::white);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, true);
    scene.render(&painter, QRectF(0, 0, 340, 40), source);
    painter.end();
    image.save(QDir(QDir::tempPath()).filePath(QStringLiteral("ray_outline.png")));

    const QColor selection = scene.physicsSelectionColor();

    // Along the middle of the ray, well clear of the origin and the arrowhead.
    int onTheLine = 0, above = 0, below = 0;
    for (int x = 80; x < 260; ++x) {
        if (near(image.pixelColor(x, 20), selection))
            ++onTheLine;
        for (int y = 12; y <= 18; ++y)
            if (near(image.pixelColor(x, y), selection)) {
                ++above;
                break;
            }
        for (int y = 22; y <= 28; ++y)
            if (near(image.pixelColor(x, y), selection)) {
                ++below;
                break;
            }
    }

    // The pen is dotted, so neither side is solid -- two thirds of the span
    // carrying a mark is a contour, and the axis staying clear is the point.
    EXPECT_GT(above, 100) << "the contour runs along one side";
    EXPECT_GT(below, 100) << "and along the other";
    EXPECT_LT(onTheLine, 20) << "and not over the ray itself";
}

namespace {

// The ray's own blue, which the field background never uses.
int rayPixels(CanvasScene *scene, const QRectF &source)
{
    QImage image(int(source.width()), int(source.height()), QImage::Format_ARGB32);
    image.fill(Qt::white);
    QPainter painter(&image);
    scene->render(&painter, QRectF(QPointF(), source.size()), source);
    painter.end();

    int marked = 0;
    for (int y = 0; y < image.height(); ++y)
        for (int x = 0; x < image.width(); ++x)
            if (near(image.pixelColor(x, y), QColor(0x3A, 0x7B, 0xD5), 30))
                ++marked;
    return marked;
}

}

// A ray measures the scene, it is not part of it -- so a run without Debug
// View leaves it out, the same as the joints.
TEST(RayOutline, HidesDuringARunWithoutDebugView)
{
    MainWindow window;
    window.resize(1000, 700);
    window.show();
    for (int i = 0; i < 20; ++i)
        QCoreApplication::processEvents();

    auto *scene = window.findChild<CanvasScene *>();
    auto *sim = window.findChild<SimulationController *>();
    ASSERT_NE(sim, nullptr);
    scene->setEditorMode(EditorMode::Physics);

    // Something for the run to actually simulate, well away from the strip
    // the ray is measured in.
    auto *block = new RectangleItem;
    block->setRect(QRectF(0, 0, 60, 60));
    block->setPos(0, 400);
    block->setName(QStringLiteral("block"));
    scene->addItem(block);
    scene->notifyShapesChanged();
    scene->selectForPhysics(block, true);
    scene->createBodyFromSelection();
    scene->clearPhysicsSelection();

    RayItem *ray = scene->addRay(QPointF(-200, 0));
    ray->setAngleDegrees(0);
    ray->setLength(300);
    scene->selectRay(nullptr);

    const QRectF area(-220, -20, 340, 40);
    const int drawn = rayPixels(scene, area);
    EXPECT_GT(drawn, 100) << "the ray is there while editing";

    scene->setDebugView(false);
    sim->start();
    ASSERT_TRUE(scene->simulationRunning()) << "the run really started";
    for (int i = 0; i < 5; ++i)
        sim->stepFrame();
    EXPECT_EQ(rayPixels(scene, area), 0) << "and gone once the run starts";

    scene->setDebugView(true);
    EXPECT_GT(rayPixels(scene, area), 100) << "unless Debug View is on";

    sim->stop();
    window.close();
}
