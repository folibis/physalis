#include "CanvasScene.h"
#include "PhysicsBody.h"
#include "ShapeItem.h"
#include "RectangleItem.h"
#include "Joint.h"
#include "SimulationController.h"

#include <QApplication>
#include <gtest/gtest.h>
#include <QImage>
#include <QPainter>
#include <QVariantMap>
#include <cstdio>

static const char *kScratch = "";

static QImage shoot(CanvasScene *scene, const QRectF &area)
{
    QImage image(int(area.width()), int(area.height()), QImage::Format_ARGB32);
    image.fill(Qt::white);
    QPainter p(&image);
    p.setRenderHint(QPainter::Antialiasing);
    scene->render(&p, QRectF(QPointF(0, 0), area.size()), area);
    p.end();
    return image;
}

// Pixels close to a given colour -- the selection rings and handles are each
// drawn in a colour nothing else on the canvas uses.
static int pixelsNear(const QImage &image, const QColor &c, int tolerance = 30)
{
    int n = 0;
    for (int y = 0; y < image.height(); ++y)
        for (int x = 0; x < image.width(); ++x) {
            const QColor p = image.pixelColor(x, y);
            if (qAbs(p.red() - c.red()) < tolerance && qAbs(p.green() - c.green()) < tolerance
                && qAbs(p.blue() - c.blue()) < tolerance)
                ++n;
        }
    return n;
}

TEST(SelHidden, Behaves)
{
    const QRectF area(40, 40, 380, 180);

    // --- Edit mode: ring, origin bulb, scale handles ----------------------
    {
        CanvasScene scene;
        auto *r = new RectangleItem;
        r->setRect(QRectF(0, 0, 90, 60));
        r->setPos(120, 90);
        scene.addItem(r);
        scene.notifyShapesChanged();

        // A body first: with nothing for the engine to simulate, start() bails
        // out and the run never begins -- which would make this test pass for
        // the wrong reason.
        scene.setEditorMode(EditorMode::Physics);
        scene.selectForPhysics(r, true);
        scene.createBodyFromSelection();
        scene.clearPhysicsSelection();

        scene.setEditorMode(EditorMode::Edit);
        scene.selectShape(r);

        const int ringIdle = pixelsNear(shoot(&scene, area), scene.selectionColor());
        const int handleIdle = pixelsNear(shoot(&scene, area), scene.handleColor());
        EXPECT_TRUE(ringIdle > 0) << "selection ring drawn when not running";
        EXPECT_TRUE(handleIdle > 0) << "scale handles drawn when not running";

        SimulationController sim(&scene, nullptr);
        sim.setEngineName(QStringLiteral("Box2D"));
        sim.start();
        sim.stepFrame();
        const QImage running = shoot(&scene, area);
        EXPECT_TRUE(pixelsNear(running, scene.selectionColor()) == 0) << "selection ring gone while running";
        EXPECT_TRUE(pixelsNear(running, scene.handleColor()) == 0) << "scale handles gone while running";
        running.save(QString::fromLatin1(kScratch) + QStringLiteral("sel_edit_running.png"));
        sim.stop();
        EXPECT_TRUE(pixelsNear(shoot(&scene, area), scene.selectionColor()) > 0) << "both come back after the run stops";
    }

    // --- Physics mode: the picked-shape ring and a selected joint ---------
    {
        CanvasScene scene;
        auto *a = new RectangleItem;
        a->setRect(QRectF(0, 0, 80, 50));
        a->setPos(90, 90);
        scene.addItem(a);
        auto *b = new RectangleItem;
        b->setRect(QRectF(0, 0, 80, 50));
        b->setPos(250, 90);
        scene.addItem(b);
        scene.notifyShapesChanged();
        scene.setEditorMode(EditorMode::Physics);

        scene.selectForPhysics(a, true);
        PhysicsBody *bodyA = scene.createBodyFromSelection();
        scene.clearPhysicsSelection();
        scene.selectForPhysics(b, true);
        PhysicsBody *bodyB = scene.createBodyFromSelection();
        scene.clearPhysicsSelection();
        Joint *joint = scene.createJoint(QStringLiteral("distance"), bodyA, bodyB, 2,
                                         QVariantMap());

        scene.selectForPhysics(a, true);
        EXPECT_TRUE(pixelsNear(shoot(&scene, area), scene.physicsSelectionColor()) > 0) << "physics ring drawn when not running";

        scene.clearPhysicsSelection();
        scene.selectJoint(joint);
        const int jointMark = pixelsNear(shoot(&scene, area), scene.jointSelectionColor());
        EXPECT_TRUE(jointMark > 0) << "joint selection outline drawn when not running";

        SimulationController sim(&scene, nullptr);
        sim.setEngineName(QStringLiteral("Box2D"));
        sim.start();
        sim.stepFrame();
        const QImage running = shoot(&scene, area);
        EXPECT_TRUE(pixelsNear(running, scene.jointSelectionColor()) == 0) << "joint selection outline gone while running";

        scene.selectForPhysics(a, true);
        const QImage running2 = shoot(&scene, area);
        EXPECT_TRUE(pixelsNear(running2, scene.physicsSelectionColor()) == 0) << "physics ring gone while running";
        running2.save(QString::fromLatin1(kScratch) + QStringLiteral("sel_physics_running.png"));
        sim.stop();
    }
}
