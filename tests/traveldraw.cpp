#include "CanvasScene.h"
#include "Joint.h"
#include "PhysicsBody.h"
#include "RectangleItem.h"

#include <QApplication>
#include <QImage>
#include <QPainter>
#include <QVariantMap>
#include <gtest/gtest.h>

// Where a sliding joint's travel is drawn. It is measured from end B -- zero
// is where that anchor stands -- so a range of -300 to 0 covers the ground
// between the anchors, not the same distance again off the far side of A.

namespace {

// How much of the joint's own axis colour lands in this patch of the scene.
// The band is the only thing drawn in it -- the grid and the bodies are their
// own colours, and nowhere near it.
int railInk(CanvasScene *scene, const QRectF &area, const QColor &rail)
{
    QImage image(int(area.width()), int(area.height()), QImage::Format_ARGB32);
    image.fill(Qt::white);
    QPainter painter(&image);
    scene->render(&painter, QRectF(0, 0, area.width(), area.height()), area);
    painter.end();

    int marked = 0;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            const QColor pixel = image.pixelColor(x, y);
            if (qAbs(pixel.red() - rail.red()) < 30 && qAbs(pixel.green() - rail.green()) < 30
                && qAbs(pixel.blue() - rail.blue()) < 30)
                ++marked;
        }
    }
    return marked;
}

} // namespace

TEST(TravelDraw, TheBandRunsBackFromTheSecondAnchor)
{
    CanvasScene scene;
    scene.setSimulationEngineName(QStringLiteral("Box2D"));

    // Two blocks 300 apart, and a joint anchored on each of them -- the case
    // that goes wrong: anchors on the same point cannot show the difference.
    auto *left = new RectangleItem;
    left->setRect(QRectF(0, 0, 40, 40));
    left->setPos(80, 80);
    auto *right = new RectangleItem;
    right->setRect(QRectF(0, 0, 40, 40));
    right->setPos(380, 80);
    scene.addItem(left);
    scene.addItem(right);
    scene.notifyShapesChanged();
    scene.setEditorMode(EditorMode::Physics);

    scene.selectForPhysics(left, true);
    PhysicsBody *bodyA = scene.createBodyFromSelection();
    scene.clearPhysicsSelection();
    scene.selectForPhysics(right, true);
    PhysicsBody *bodyB = scene.createBodyFromSelection();
    scene.clearPhysicsSelection();

    QVariantMap params { { QStringLiteral("enableLimit"), true },
                         { QStringLiteral("lowerTranslation"), -300.0 },
                         { QStringLiteral("upperTranslation"), 0.0 } };
    Joint *joint = scene.createJoint(QStringLiteral("prismatic"), bodyA, bodyB, 2, params);
    ASSERT_TRUE(joint);
    joint->params() = params;
    joint->setAnchorScenePos(Joint::End::A, QPointF(100, 100));
    joint->setAnchorScenePos(Joint::End::B, QPointF(400, 100));

    const QColor rail = scene.jointKindColor(physics::JointVisual::Axis).darker(160);

    // Empty ground on both sides, well clear of the blocks and the anchors.
    // Three hundred units of travel measured from the wrong anchor lands here,
    // which is how the mistake showed: a lift whose rail ran up and away had
    // its limits drawn running down and away, off the picture entirely.
    EXPECT_EQ(railInk(&scene, QRectF(-260, 60, 300, 80), rail), 0)
        << "nothing is drawn 300 further back than the joint can reach";
    EXPECT_EQ(railInk(&scene, QRectF(460, 60, 300, 80), rail), 0)
        << "and nothing beyond the far anchor either, with no travel above zero";
    EXPECT_GT(railInk(&scene, QRectF(90, 60, 340, 80), rail), 0)
        << "the travel is drawn where it happens -- across the ground between"
           " the two anchors, where the joint can actually go";
}
