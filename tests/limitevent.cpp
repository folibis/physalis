#include "CanvasScene.h"
#include "EngineRegistry.h"
#include "Joint.h"
#include "JointTypes.h"
#include "PhysicsBody.h"
#include "RectangleItem.h"
#include "Rule.h"
#include "SimulationController.h"

#include <QApplication>
#include <QVariantMap>
#include <gtest/gtest.h>

using namespace physics;

namespace {

// A motorised arm on a hinge with somewhere to stop. The motor drives it into
// the upper limit and holds it there, which is the arrival a rule watches for.
struct Arm
{
    CanvasScene scene;
    Joint *hinge = nullptr;

    Arm()
    {
        scene.setSimulationEngineName(QStringLiteral("Box2D"));

        auto *postShape = new RectangleItem;
        postShape->setRect(QRectF(0, 0, 40, 40));
        postShape->setPos(0, 0);
        auto *armShape = new RectangleItem;
        armShape->setRect(QRectF(0, 0, 200, 20));
        armShape->setPos(20, 10);
        scene.addItem(postShape);
        scene.addItem(armShape);
        scene.notifyShapesChanged();
        scene.setEditorMode(EditorMode::Physics);

        scene.selectForPhysics(postShape, true);
        PhysicsBody *post = scene.createBodyFromSelection();
        post->props().type = BodyType::Static;
        scene.clearPhysicsSelection();

        scene.selectForPhysics(armShape, true);
        PhysicsBody *arm = scene.createBodyFromSelection();
        // Gravity would swing the arm down past the limit before the motor had
        // a say; this is about the limit, not about the fall.
        arm->props().gravityScale = 0.0;
        scene.clearPhysicsSelection();

        QVariantMap params;
        int anchors = 0;
        if (auto engine = EngineRegistry::create(QStringLiteral("Box2D"))) {
            for (const JointType &t : engine->jointTypes()) {
                if (t.id != QLatin1String("revolute"))
                    continue;
                anchors = t.anchorCount;
                for (const JointParam &p : t.params)
                    params.insert(p.key, p.defaultValue);
            }
        }
        params.insert(QStringLiteral("enableLimit"), true);
        params.insert(QStringLiteral("lowerAngle"), -10.0);
        params.insert(QStringLiteral("upperAngle"), 45.0);
        params.insert(QStringLiteral("enableMotor"), true);
        params.insert(QStringLiteral("motorSpeed"), 120.0);
        // Newton-metres on an arm weighing a few grams: enough to turn it,
        // not enough to argue with the limit when it gets there.
        params.insert(QStringLiteral("maxMotorTorque"), 0.001);

        hinge = scene.createJoint(QStringLiteral("revolute"), post, arm, anchors, params);
    }
};

} // namespace

// Joint limit events. They were queued with a positional initialiser, and
// EngineEvent carries two shape names between the handles and the id -- so the
// id went into subjectShape and every one of these events arrived nameless.
// Nothing had ever watched for one, so nothing had noticed.
TEST(LimitEvent, ReachesARuleOnArrival)
{
    Arm arm;

    // The motor switches itself off when the arm arrives at its stop, which is
    // the thing limit events exist for.
    Rule stop;
    stop.subjectName = arm.hinge->name();
    stop.eventId = QStringLiteral("limitUpper");
    stop.targetName = arm.hinge->name();
    stop.propertyKey = QStringLiteral("enableMotor");
    stop.op = Rule::Op::Set;
    stop.value = false;
    arm.scene.setRules({ stop });

    SimulationController sim(&arm.scene, nullptr);
    sim.setEngineName(QStringLiteral("Box2D"));
    sim.start();

    ASSERT_TRUE(sim.readValue(arm.hinge->name(), QStringLiteral("motorEnabled")).toBool())
        << "the motor is running to begin with";

    for (int i = 0; i < 90; ++i)
        sim.stepFrame();

    EXPECT_GT(sim.readValue(arm.hinge->name(), QStringLiteral("angle")).toDouble(), 40.0)
        << "the arm reached its upper limit";
    EXPECT_FALSE(sim.readValue(arm.hinge->name(), QStringLiteral("motorEnabled")).toBool())
        << "and the rule watching for that arrival switched the motor off";

    sim.stop();
}
