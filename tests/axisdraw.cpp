#include "CanvasScene.h"
#include "PhysicsBody.h"
#include "ShapeItem.h"
#include "RectangleItem.h"
#include "Joint.h"

#include <QApplication>
#include <gtest/gtest.h>
#include <QImage>
#include <QPainter>
#include <QVariantMap>
#include <cstdio>

static const char *kScratch = "";

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

static int pixelsNear(const QImage &im, const QColor &c, int tol = 40)
{
    int n = 0;
    for (int y = 0; y < im.height(); ++y)
        for (int x = 0; x < im.width(); ++x) {
            const QColor p = im.pixelColor(x, y);
            if (qAbs(p.red() - c.red()) < tol && qAbs(p.green() - c.green()) < tol
                && qAbs(p.blue() - c.blue()) < tol) ++n;
        }
    return n;
}


TEST(AxisDraw, Behaves)
{
    const QRectF area(40, 60, 320, 140);

    struct Case { const char *type; int anchors; bool limit; };
    const Case cases[] = { {"revolute", 1, false}, {"prismatic", 1, true}, {"prismatic", 1, false} };
    QImage shots[3];

    int i = 0;
    for (const Case &c : cases) {
        CanvasScene scene;
        scene.setSimulationEngineName(QStringLiteral("Box2D"));
        auto *a = new RectangleItem; a->setRect(QRectF(0, 0, 70, 44)); a->setPos(90, 100);
        auto *b = new RectangleItem; b->setRect(QRectF(0, 0, 70, 44)); b->setPos(240, 100);
        scene.addItem(a); scene.addItem(b);
        scene.notifyShapesChanged();
        scene.setEditorMode(EditorMode::Physics);

        scene.selectForPhysics(a, true);
        PhysicsBody *bodyA = scene.createBodyFromSelection();
        scene.clearPhysicsSelection();
        scene.selectForPhysics(b, true);
        PhysicsBody *bodyB = scene.createBodyFromSelection();
        scene.clearPhysicsSelection();

        QVariantMap params;
        if (c.limit) {
            params.insert(QStringLiteral("enableLimit"), true);
            params.insert(QStringLiteral("lowerTranslation"), -55.0);
            params.insert(QStringLiteral("upperTranslation"), 55.0);
        }
        Joint *j = scene.createJoint(QString::fromLatin1(c.type), bodyA, bodyB, c.anchors, params);
        j->params() = params;
        j->setAnchorScenePos(Joint::End::A, QPointF(200, 122));

        shots[i] = shoot(&scene, area);
        const QColor colour = scene.jointTypeColor(QString::fromLatin1(c.type));
        ++i;
    }

    CanvasScene probe;
    probe.setSimulationEngineName(QStringLiteral("Box2D"));
    EXPECT_TRUE(probe.jointVisual(QStringLiteral("prismatic")) == physics::JointVisual::Axis) << "the engine says prismatic slides";
    EXPECT_TRUE(probe.jointVisual(QStringLiteral("wheel")) == physics::JointVisual::Axis) << "the engine says wheel slides";
    EXPECT_TRUE(probe.jointVisual(QStringLiteral("revolute")) == physics::JointVisual::Pivot) << "revolute is still a pivot";
    EXPECT_TRUE(shots[0] != shots[1]) << "a prismatic no longer looks like a revolute";
    EXPECT_TRUE(shots[1] != shots[2]) << "limits change the rail length";

    QImage sheet(shots[0].width(), shots[0].height() * 3 + 60, QImage::Format_ARGB32);
    sheet.fill(QColor(0xF6, 0xF6, 0xF6));
    QPainter p(&sheet);
    const char *labels[] = {"revolute (pivot)", "prismatic, limits -55..55", "prismatic, free travel"};
    for (int k = 0; k < 3; ++k) {
        p.drawText(10, k * (shots[k].height() + 20) + 14, QString::fromLatin1(labels[k]));
        p.drawImage(0, k * (shots[k].height() + 20) + 20, shots[k]);
    }
    p.end();
    sheet.save(QString::fromLatin1(kScratch) + QStringLiteral("axisjoint.png"));
}
