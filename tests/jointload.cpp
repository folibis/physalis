#include "CanvasScene.h"
#include "Joint.h"
#include "PhysicsBody.h"
#include "RectangleItem.h"
#include "Rule.h"
#include "ShapeItem.h"
#include "SimulationController.h"
#include "EngineRegistry.h"
#include "PhysicsTypes.h"

#include <QApplication>
#include <gtest/gtest.h>
#include <QVariantMap>
#include <cstdio>


TEST(JointLoad, Behaves)
{
    auto engine = physics::EngineRegistry::create(QStringLiteral("Box2D"));
    for (const physics::JointType &type : engine->jointTypes()) {
        QStringList keys;
        for (const physics::JointParam &p : engine->jointReadables(type.id))
            keys << p.key;
        EXPECT_TRUE(keys.contains(QStringLiteral("constraintForce"))
                  && keys.contains(QStringLiteral("constraintTorque"))) << type.id.toUtf8().constData();
    }

    // A heavy block hung from a static anchor by a distance joint. The joint
    // has to hold up its whole weight, so the force is predictable.
    CanvasScene scene;
    scene.setSimulationEngineName(QStringLiteral("Box2D"));

    auto *anchor = new RectangleItem;
    anchor->setRect(QRectF(0, 0, 40, 20));
    anchor->setPos(-20, -200);
    anchor->setName(QStringLiteral("anchor"));
    auto *weight = new RectangleItem;
    weight->setRect(QRectF(0, 0, 100, 100));
    weight->setPos(-50, 0);
    weight->setName(QStringLiteral("weight"));
    scene.addItem(anchor);
    scene.addItem(weight);
    scene.notifyShapesChanged();
    scene.setEditorMode(EditorMode::Physics);

    scene.selectForPhysics(anchor, true);
    PhysicsBody *anchorBody = scene.createBodyFromSelection();
    anchorBody->props().type = physics::BodyType::Static;
    scene.clearPhysicsSelection();
    scene.selectForPhysics(weight, true);
    PhysicsBody *weightBody = scene.createBodyFromSelection();
    scene.clearPhysicsSelection();

    QVariantMap params;
    Joint *rope = scene.createJoint(QStringLiteral("distance"), anchorBody, weightBody,
                                    2, params);

    SimulationController sim(&scene, nullptr);
    sim.setEngineName(QStringLiteral("Box2D"));
    sim.start();
    for (int f = 0; f < 240; ++f)
        sim.stepFrame();

    const QVariant force = sim.readValue(rope->name(), QStringLiteral("constraintForce"));
    const QVariant torque = sim.readValue(rope->name(), QStringLiteral("constraintTorque"));

    // Weight of the block: density 1 over its area in metres, times gravity.
    const qreal ppm = scene.world().pixelsPerMeter;
    const qreal areaM2 = (100.0 / ppm) * (100.0 / ppm);
    // The solver runs at kReferencePixelsPerMeter, so gravity -- and therefore
    // every force it reports -- is scaled by that ratio.
    const qreal motionScale = physics::kReferencePixelsPerMeter / ppm;
    const qreal expected = areaM2 * 1.0 * scene.world().gravity.y() * motionScale;
    sim.stop();

    EXPECT_TRUE(force.isValid()) << "the force is readable" << " -- " << (force.isValid() ? QString::number(force.toDouble(), 'g', 4)
                          : QStringLiteral("unreadable")).toStdString();
    EXPECT_TRUE(torque.isValid()) << "the torque is readable";
    EXPECT_TRUE(force.isValid() && qAbs(force.toDouble() - expected) < expected * 0.25) << "and it matches the weight it is holding";
}
