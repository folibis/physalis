#include "CanvasScene.h"
#include "PhysicsBody.h"
#include "ShapeItem.h"
#include "RectangleItem.h"
#include "Joint.h"
#include "SimulationController.h"

#include <QVariantMap>

#include <QApplication>
#include <gtest/gtest.h>
#include <QImage>
#include <QPainter>
#include <cstdio>

static const char *kScratch = "";

static ShapeItem *box(CanvasScene *scene, qreal x, qreal y, const char *name)
{
    auto *r = new RectangleItem;
    r->setRect(QRectF(0, 0, 60, 40));
    r->setPos(x, y);
    r->setName(QString::fromLatin1(name));
    scene->addItem(r);
    return r;
}

static QImage shoot(CanvasScene *scene, const QRectF &area)
{
    QImage image(int(area.width() * 2), int(area.height() * 2), QImage::Format_ARGB32);
    image.fill(Qt::white);
    QPainter p(&image);
    p.setRenderHint(QPainter::Antialiasing);
    scene->render(&p, QRectF(0, 0, area.width() * 2, area.height() * 2), area);
    p.end();
    return image;
}

// How many pixels carry a joint's own colour -- the thing that must survive
// Debug View being switched off.
static int jointPixels(const QImage &image, const QColor &colour)
{
    int count = 0;
    for (int y = 0; y < image.height(); ++y)
        for (int x = 0; x < image.width(); ++x) {
            const QColor c = image.pixelColor(x, y);
            if (qAbs(c.red() - colour.red()) < 24 && qAbs(c.green() - colour.green()) < 24
                && qAbs(c.blue() - colour.blue()) < 24)
                ++count;
        }
    return count;
}

TEST(JointsVisible, Behaves)
{

    for (const QString &type : {QStringLiteral("revolute"), QStringLiteral("distance")}) {
        CanvasScene scene;
        ShapeItem *a = box(&scene, 100, 100, "a");
        ShapeItem *b = box(&scene, 260, 100, "b");
        scene.notifyShapesChanged();
        scene.setEditorMode(EditorMode::Physics);

        scene.selectForPhysics(a, true);
        PhysicsBody *bodyA = scene.createBodyFromSelection();
        scene.clearPhysicsSelection();
        scene.selectForPhysics(b, true);
        PhysicsBody *bodyB = scene.createBodyFromSelection();
        scene.clearPhysicsSelection();

        const int anchors = type == QLatin1String("revolute") ? 1 : 2;
        Joint *joint = scene.createJoint(type, bodyA, bodyB, anchors, QVariantMap());
        ASSERT_TRUE(joint != nullptr) << "joint created: " << type.toStdString();
        joint->setAnchorScenePos(Joint::End::A, QPointF(160, 120));
        if (anchors > 1)
            joint->setAnchorScenePos(Joint::End::B, QPointF(290, 120));

        const QRectF area(60, 60, 320, 140);
        const QColor colour = scene.jointTypeColor(type);

        // Outside a run the joint is always drawn -- that is when it is being
        // placed, and hiding it then would make it uneditable.
        scene.setDebugView(false);
        const int idle = jointPixels(shoot(&scene, area), colour);

        SimulationController sim(&scene, nullptr);
        sim.setEngineName(QStringLiteral("Box2D"));
        sim.start();
        for (int i = 0; i < 10; ++i)
            sim.stepFrame();

        scene.setDebugView(true);
        const int on = jointPixels(shoot(&scene, area), colour);
        scene.setDebugView(false);
        const QImage offImage = shoot(&scene, area);
        const int off = jointPixels(offImage, colour);
        sim.stop();

        const bool ok = idle > 0 && on > 0 && off == 0;
        EXPECT_TRUE(ok) << "joint shows with debug view on and hides without";

        offImage.save(QString::fromLatin1(kScratch) + QStringLiteral("joints_%1_off.png").arg(type));
    }
}
