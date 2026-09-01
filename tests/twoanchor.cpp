#include "CanvasScene.h"
#include "PhysicsBody.h"
#include "RectangleItem.h"
#include "Joint.h"
#include "EngineRegistry.h"
#include "JointTypes.h"

#include <QApplication>
#include <gtest/gtest.h>
#include <QImage>
#include <QPainter>
#include <QVariantMap>
#include <cstdio>

static const char *kScratch = "";


TEST(TwoAnchor, Behaves)
{
    CanvasScene scene;
    scene.setSimulationEngineName(QStringLiteral("Box2D"));

    auto *ra = new RectangleItem; ra->setRect(QRectF(0, 0, 80, 50)); ra->setPos(80, 100);
    auto *rb = new RectangleItem; rb->setRect(QRectF(0, 0, 80, 50)); rb->setPos(250, 130);
    scene.addItem(ra); scene.addItem(rb);
    scene.notifyShapesChanged();
    scene.setEditorMode(EditorMode::Physics);

    scene.selectForPhysics(ra, true);
    PhysicsBody *bodyA = scene.createBodyFromSelection();
    scene.clearPhysicsSelection();
    scene.selectForPhysics(rb, true);
    PhysicsBody *bodyB = scene.createBodyFromSelection();
    scene.clearPhysicsSelection();

    // The toolbar passes the type's own anchor count, so ask the catalogue
    // the same way it does.
    int anchorCount = 0;
    QVariantMap defaults;
    if (auto engine = physics::EngineRegistry::create(QStringLiteral("Box2D"))) {
        for (const physics::JointType &t : engine->jointTypes()) {
            if (t.id == QLatin1String("prismatic")) {
                anchorCount = t.anchorCount;
                for (const physics::JointParam &prm : t.params)
                    defaults.insert(prm.key, prm.defaultValue);
            }
        }
    }
    Joint *j = scene.createJoint(QStringLiteral("prismatic"), bodyA, bodyB,
                                 anchorCount, defaults);
    const QPointF a = j->anchorScenePos(Joint::End::A);
    const QPointF b = j->anchorScenePos(Joint::End::B);
    const QPointF comA = bodyA->centerOfMassScenePos();
    const QPointF comB = bodyB->centerOfMassScenePos();
    EXPECT_TRUE(j->anchorCount() == 2) << "anchor count is 2" << " -- " << (QStringLiteral("%1").arg(j->anchorCount())).toStdString();
    EXPECT_TRUE(QLineF(a, comA).length() < 0.5) << "anchor A sits on body A's centre of mass";
    EXPECT_TRUE(QLineF(b, comB).length() < 0.5) << "anchor B sits on body B's centre of mass";

    const QRectF area(40, 60, 300, 140);
    QImage im(int(area.width()) * 2, int(area.height()) * 2, QImage::Format_ARGB32);
    im.fill(Qt::white);
    QPainter p(&im);
    p.setRenderHint(QPainter::Antialiasing);
    scene.render(&p, QRectF(0, 0, im.width(), im.height()), area);
    p.end();
    im.save(QString::fromLatin1(kScratch) + QStringLiteral("twoanchor.png"));
}
