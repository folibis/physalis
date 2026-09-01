#include "CanvasScene.h"
#include "PhysicsBody.h"
#include "RectangleItem.h"
#include "Joint.h"
#include "SimulationController.h"
#include "EngineRegistry.h"
#include "JointTypes.h"

#include <QApplication>
#include <gtest/gtest.h>
#include <QVariantMap>
#include <cstdio>

using namespace physics;


TEST(MotorOffset, Behaves)
{
    CanvasScene scene;
    scene.setSimulationEngineName(QStringLiteral("Box2D"));

    auto *postShape = new RectangleItem;
    postShape->setRect(QRectF(0, 0, 30, 30));
    postShape->setPos(-200, -50);
    auto *plateShape = new RectangleItem;
    plateShape->setRect(QRectF(0, 0, 120, 20));
    plateShape->setPos(140, 90);
    scene.addItem(postShape);
    scene.addItem(plateShape);
    scene.notifyShapesChanged();
    scene.setEditorMode(EditorMode::Physics);

    scene.selectForPhysics(postShape, true);
    PhysicsBody *post = scene.createBodyFromSelection();
    post->props().type = BodyType::Static;
    scene.clearPhysicsSelection();
    scene.selectForPhysics(plateShape, true);
    PhysicsBody *plate = scene.createBodyFromSelection();
    scene.clearPhysicsSelection();

    QVariantMap defaults;
    int anchors = 0;
    if (auto engine = EngineRegistry::create(QStringLiteral("Box2D"))) {
        for (const JointType &t : engine->jointTypes())
            if (t.id == QLatin1String("motor")) {
                anchors = t.anchorCount;
                for (const JointParam &p : t.params)
                    defaults.insert(p.key, p.defaultValue);
            }
    }
    Joint *motor = scene.createJoint(QStringLiteral("motor"), post, plate, anchors, defaults);

    const QPointF expected = plate->originScenePos() - post->originScenePos();
    const qreal offsetX = motor->params().value(QStringLiteral("linearOffsetX")).toDouble();
    const qreal offsetY = motor->params().value(QStringLiteral("linearOffsetY")).toDouble();
    EXPECT_TRUE(qAbs(offsetX - expected.x()) < 0.5 && qAbs(offsetY - expected.y()) < 0.5) << "offset starts at the bodies' real separation";

    // With maxForce at its default the motor cannot act; give it some so the
    // test measures the offset rather than an inert joint.
    motor->params().insert(QStringLiteral("maxForce"), 5000.0);
    motor->params().insert(QStringLiteral("maxTorque"), 5000.0);

    const QPointF before = plate->centerOfMassScenePos();
    SimulationController sim(&scene, nullptr);
    sim.setEngineName(QStringLiteral("Box2D"));
    sim.start();
    for (int i = 0; i < 30; ++i)
        sim.stepFrame();
    const QPointF after = plate->centerOfMassScenePos();
    sim.stop();

    EXPECT_TRUE(QLineF(before, after).length() < 20.0) << "the plate is not yanked onto the post" << " -- " << (QStringLiteral("moved %1 px").arg(QLineF(before, after).length(), 0, 'f', 1)).toStdString();
}
