#include "CanvasScene.h"
#include "Joint.h"
#include "PhysicsBody.h"
#include "RectangleItem.h"
#include "ShapeItem.h"

#include <QImage>
#include <QPainter>
#include <QVariantMap>
#include <gtest/gtest.h>

namespace {

const QRectF kArea(60, 60, 320, 140);

ShapeItem *box(CanvasScene *scene, qreal x, qreal y, const char *name)
{
    auto *rectangle = new RectangleItem;
    rectangle->setRect(QRectF(0, 0, 60, 40));
    rectangle->setPos(x, y);
    rectangle->setName(QString::fromLatin1(name));
    scene->addItem(rectangle);
    return rectangle;
}

// A scene with one joint of the given type between two bodies.
Joint *twoBodyScene(CanvasScene *scene, const QString &type, int anchors)
{
    ShapeItem *a = box(scene, 100, 100, "a");
    ShapeItem *b = box(scene, 260, 100, "b");
    scene->notifyShapesChanged();
    scene->setEditorMode(EditorMode::Physics);

    scene->selectForPhysics(a, true);
    PhysicsBody *bodyA = scene->createBodyFromSelection();
    scene->clearPhysicsSelection();
    scene->selectForPhysics(b, true);
    PhysicsBody *bodyB = scene->createBodyFromSelection();
    scene->clearPhysicsSelection();

    Joint *joint = scene->createJoint(type, bodyA, bodyB, anchors, QVariantMap());
    if (joint) {
        joint->setAnchorScenePos(Joint::End::A, QPointF(160, 120));
        joint->setAnchorScenePos(Joint::End::B, QPointF(290, 120));
    }
    return joint;
}

int pixelsOf(CanvasScene *scene, const QColor &colour)
{
    QImage image(int(kArea.width() * 2), int(kArea.height() * 2), QImage::Format_ARGB32);
    image.fill(Qt::white);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    scene->render(&painter, QRectF(0, 0, kArea.width() * 2, kArea.height() * 2), kArea);
    painter.end();

    int count = 0;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            const QColor c = image.pixelColor(x, y);
            if (qAbs(c.red() - colour.red()) < 24 && qAbs(c.green() - colour.green()) < 24
                && qAbs(c.blue() - colour.blue()) < 24)
                ++count;
        }
    }
    return count;
}

} // namespace

// Joints are coloured by kind, not by the engine's name for the type: every
// engine tags its types with one of five kinds, so one short list of settings
// covers all of them and a scene keeps its look under either engine.
TEST(JointColours, BelongToTheKindNotTheEngine)
{
    CanvasScene scene;
    const QColor magenta(Qt::magenta);

    scene.setSimulationEngineName(QStringLiteral("Box2D"));
    EXPECT_EQ(scene.jointVisual(QStringLiteral("distance")), physics::JointVisual::Segment);
    EXPECT_EQ(scene.jointTypeColor(QStringLiteral("distance")),
              CanvasScene::defaultJointKindColor(physics::JointVisual::Segment))
        << "a joint wears its kind's colour";

    scene.setJointKindColor(physics::JointVisual::Segment, magenta);
    EXPECT_EQ(scene.jointTypeColor(QStringLiteral("distance")), magenta);
    EXPECT_NE(scene.jointTypeColor(QStringLiteral("revolute")), magenta)
        << "and only that kind's";

    // Chipmunk's pin holds a length just as Box2D's distance does, so the same
    // setting reaches it -- with no row of its own, and no name in common.
    scene.setSimulationEngineName(QStringLiteral("Chipmunk2D"));
    EXPECT_EQ(scene.jointVisual(QStringLiteral("pin")), physics::JointVisual::Segment);
    EXPECT_EQ(scene.jointTypeColor(QStringLiteral("pin")), magenta)
        << "the colour crosses engines because the kind does";
}

// Each kind also carries how its line is drawn: the rod joints have always
// been, or a plain stroke.
TEST(JointColours, TheStyleChangesWhatIsDrawn)
{
    CanvasScene rod;
    ASSERT_TRUE(twoBodyScene(&rod, QStringLiteral("distance"), 2));
    EXPECT_EQ(rod.jointKindStyle(physics::JointVisual::Segment), JointStyle::Rod)
        << "a joint holding a length is a rod until told otherwise";
    // An anchorless joint has nothing to be a rod between, so it starts dashed.
    EXPECT_EQ(rod.jointKindStyle(physics::JointVisual::Link), JointStyle::Dashed);

    // Both of these are strokes at full strength, unlike the rod, whose shaft
    // is a fill at whatever opacity the joint settings ask for -- so these two
    // are what can be counted against each other.
    CanvasScene solid;
    ASSERT_TRUE(twoBodyScene(&solid, QStringLiteral("distance"), 2));
    solid.setJointKindStyle(physics::JointVisual::Segment, JointStyle::Solid);
    const int solidPixels = pixelsOf(&solid, solid.jointTypeColor(QStringLiteral("distance")));

    CanvasScene dotted;
    ASSERT_TRUE(twoBodyScene(&dotted, QStringLiteral("distance"), 2));
    dotted.setJointKindStyle(physics::JointVisual::Segment, JointStyle::Dotted);
    EXPECT_EQ(dotted.jointKindStyle(physics::JointVisual::Segment), JointStyle::Dotted);
    const int dottedPixels = pixelsOf(&dotted, dotted.jointTypeColor(QStringLiteral("distance")));

    EXPECT_GT(dottedPixels, 0) << "the dotted joint is still drawn";
    EXPECT_LT(dottedPixels, solidPixels)
        << "with gaps in it -- " << dottedPixels << " against the solid line's " << solidPixels;
}
