#include "CanvasScene.h"
#include "Joint.h"
#include "PhysicsBody.h"
#include "RectangleItem.h"
#include "Rule.h"
#include "SceneSerializer.h"
#include "ShapeItem.h"
#include "SimulationController.h"

#include <QApplication>
#include <QLineF>
#include <QJsonArray>
#include <QJsonObject>
#include <QVariantMap>
#include <gtest/gtest.h>

namespace {

PhysicsBody *bodyFrom(CanvasScene *scene, const QPointF &at, const char *name)
{
    auto *shape = new RectangleItem;
    shape->setRect(QRectF(0, 0, 40, 40));
    shape->setPos(at);
    shape->setName(QString::fromLatin1(name));
    scene->addItem(shape);
    scene->notifyShapesChanged();
    scene->selectForPhysics(shape, true);
    PhysicsBody *body = scene->createBodyFromSelection();
    scene->clearPhysicsSelection();
    return body;
}

ShapeItem *shapeNamed(CanvasScene *scene, const char *name)
{
    for (ShapeItem *s : scene->shapes())
        if (s->name() == QLatin1String(name))
            return s;
    return nullptr;
}

} // namespace

// A joint that holds a body to a point in the world has one body, not two.
// Box2D's def has a second slot and asserts if what is in it can move, so the
// engine keeps a static body of its own for it -- but that is the backend's
// business. Asking the user to find or invent a static body to satisfy it put
// Box2D's struct layout in the way of drawing a scene.
TEST(MouseJoint, Behaves)
{
    CanvasScene scene;
    scene.setEditorMode(EditorMode::Physics);

    PhysicsBody *cargo = bodyFrom(&scene, QPointF(0, 0), "cargo");
    ASSERT_TRUE(cargo != nullptr);

    QVariantMap params;
    params.insert(QStringLiteral("hertz"), 4.0);
    params.insert(QStringLiteral("dampingRatio"), 1.0);
    params.insert(QStringLiteral("maxForce"), 0.05);
    Joint *joint = scene.createJoint(QStringLiteral("mouse"), cargo, nullptr, 2, params);
    ASSERT_TRUE(joint != nullptr) << "one body is enough";
    joint->params() = params;
    EXPECT_EQ(joint->bodyA(), cargo);
    EXPECT_TRUE(joint->bodyB() == nullptr) << "and there is no second one";

    // Both ends start on the body, so nothing is yanked the moment a run
    // begins: the grip is on it and the target is not yet anywhere else.
    EXPECT_LT(QLineF(joint->anchorScenePos(Joint::End::A),
                     cargo->centerOfMassScenePos()).length(), 1.0)
        << "the held point starts on the body";
    EXPECT_LT(QLineF(joint->anchorScenePos(Joint::End::B),
                     cargo->centerOfMassScenePos()).length(), 1.0)
        << "and so does the point it is pulled to, so it does not move";

    // It survives the file with one body named and no empty second one.
    const QJsonObject saved = SceneSerializer::save(&scene);
    const QJsonArray savedJoints = saved.value(QStringLiteral("joints")).toArray();
    ASSERT_EQ(savedJoints.size(), 1) << "a one-body joint is still written out";
    EXPECT_EQ(savedJoints.at(0).toObject().value(QStringLiteral("bodyA")).toString(),
              cargo->name());
    EXPECT_FALSE(savedJoints.at(0).toObject().contains(QStringLiteral("bodyB")))
        << "with no second body named, rather than an empty one";

    CanvasScene reloaded;
    QString error;
    ASSERT_TRUE(SceneSerializer::load(&reloaded, saved, &error)) << error.toStdString();
    ASSERT_EQ(reloaded.joints().size(), 1) << "and read back rather than dropped";
    EXPECT_TRUE(reloaded.joints().first()->bodyB() == nullptr);

    // And the engine takes it: a rule moves the target, the body follows.
    Rule lead;
    lead.subjectName = Rule::world();
    lead.conditionKey = QStringLiteral("frame");
    lead.compare = Rule::Compare::Greater;
    lead.conditionValue = 2.0;
    lead.targetName = reloaded.joints().first()->name();
    lead.propertyKey = QStringLiteral("targetX");
    lead.op = Rule::Op::Set;
    lead.value = 260.0;
    reloaded.setRules({lead});

    ShapeItem *moved = shapeNamed(&reloaded, "cargo");
    ASSERT_TRUE(moved != nullptr);
    const qreal startX = moved->mapToScene(moved->rect().center()).x();

    SimulationController sim(&reloaded, nullptr);
    sim.setEngineName(QStringLiteral("Box2D"));
    sim.start();
    EXPECT_TRUE(sim.skippedJoints().isEmpty())
        << "the engine supplies the world side itself"
        << " -- " << sim.skippedJoints().join(", ").toStdString();
    for (int i = 0; i < 240; ++i)
        sim.stepFrame();
    const qreal endX = moved->mapToScene(moved->rect().center()).x();
    sim.stop();

    EXPECT_GT(endX - startX, 100.0)
        << "the body is led towards the target rather than staying put"
        << " -- moved " << (endX - startX);
}

// Whatever a joint draws is what you can click on. A two-anchor joint is
// picked by its shaft; a joint holding one body to a point draws a leader from
// the point back to the body, and that has to answer to a click the same way
// -- a line you can see and cannot select is the odd one out.
TEST(MouseJointPicking, Behaves)
{
    CanvasScene scene;
    scene.setEditorMode(EditorMode::Physics);

    PhysicsBody *cargo = bodyFrom(&scene, QPointF(0, 0), "cargo");
    ASSERT_TRUE(cargo != nullptr);
    Joint *joint = scene.createJoint(QStringLiteral("mouse"), cargo, nullptr, 2, QVariantMap());
    ASSERT_TRUE(joint != nullptr);

    const QPointF target(300, -200);
    joint->setAnchorScenePos(Joint::End::B, target);   // the end that is moved
    const QPointF centre = joint->anchorScenePos(Joint::End::A);

    // The target itself, as any anchor is.
    int end = -1;
    EXPECT_EQ(scene.jointAt(target, &end), joint) << "the target picks it";

    // And anywhere along the leader between the two.
    for (double t : { 0.25, 0.5, 0.75 }) {
        const QPointF on = centre + (target - centre) * t;
        EXPECT_EQ(scene.jointAt(on, &end), joint)
            << "the leader picks it too, at " << t << " along";
    }

    // Well away from it, nothing.
    const QPointF across = centre + QPointF(-400, 400);
    EXPECT_TRUE(scene.jointAt(across, &end) == nullptr) << "and empty space picks nothing";
}

// The target may be put anywhere, including well away from the body -- that
// is the ordinary way to use this joint, and the body has to be drawn towards
// it. Box2D's def field is the *initial* target and is what it derives its
// grip from, so handing it the destination made it take hold of the empty
// space there and pin the body where it already stood: a joint that reported
// itself created, showed a target, and did nothing at all.
TEST(MouseJointPulls, Behaves)
{
    CanvasScene scene;
    scene.setEditorMode(EditorMode::Physics);

    PhysicsBody *cargo = bodyFrom(&scene, QPointF(0, 0), "cargo");
    ASSERT_TRUE(cargo != nullptr);

    QVariantMap params;
    params.insert(QStringLiteral("hertz"), 10.0);
    params.insert(QStringLiteral("dampingRatio"), 10.0);
    params.insert(QStringLiteral("maxForce"), 10.0);
    Joint *joint = scene.createJoint(QStringLiteral("mouse"), cargo, nullptr, 2, params);
    ASSERT_TRUE(joint != nullptr);
    joint->params() = params;

    // The second end dragged away from the body, which is how it is aimed.
    const QPointF target(300, -200);
    joint->setAnchorScenePos(Joint::End::B, target);
    EXPECT_LT(QLineF(joint->anchorScenePos(Joint::End::A),
                     cargo->centerOfMassScenePos()).length(), 1.0)
        << "and the held point stays on the body";

    ShapeItem *ball = shapeNamed(&scene, "cargo");
    ASSERT_TRUE(ball != nullptr);
    const QPointF started = ball->mapToScene(ball->rect().center());
    ASSERT_GT(QLineF(started, target).length(), 100.0) << "it starts well away from the target";

    SimulationController sim(&scene, nullptr);
    sim.setEngineName(QStringLiteral("Box2D"));
    sim.start();
    ASSERT_TRUE(sim.skippedJoints().isEmpty());
    for (int i = 0; i < 300; ++i)
        sim.stepFrame();
    const QPointF ended = ball->mapToScene(ball->rect().center());
    sim.stop();

    EXPECT_LT(QLineF(ended, target).length(), 10.0)
        << "the body arrives at the target it was pulled towards"
        << " -- ended at (" << ended.x() << ", " << ended.y() << ")";
}
