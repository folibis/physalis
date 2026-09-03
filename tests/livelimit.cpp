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

// A slider driven by its own motor: a static post on the left, a plate to the
// right of it, and a prismatic joint between them along the line they make.
// The plate travels until something stops it, which is what the limit is for.
struct Slider
{
    CanvasScene scene;
    Joint *joint = nullptr;
    PhysicsBody *plate = nullptr;

    Slider()
    {
        scene.setSimulationEngineName(QStringLiteral("Box2D"));

        auto *postShape = new RectangleItem;
        postShape->setRect(QRectF(0, 0, 40, 40));
        postShape->setPos(0, 0);
        auto *plateShape = new RectangleItem;
        plateShape->setRect(QRectF(0, 0, 60, 40));
        plateShape->setPos(300, 0);
        scene.addItem(postShape);
        scene.addItem(plateShape);
        scene.notifyShapesChanged();
        scene.setEditorMode(EditorMode::Physics);

        scene.selectForPhysics(postShape, true);
        PhysicsBody *post = scene.createBodyFromSelection();
        post->props().type = BodyType::Static;
        scene.clearPhysicsSelection();

        scene.selectForPhysics(plateShape, true);
        plate = scene.createBodyFromSelection();
        scene.clearPhysicsSelection();

        QVariantMap params;
        int anchors = 0;
        if (auto engine = EngineRegistry::create(QStringLiteral("Box2D"))) {
            for (const JointType &t : engine->jointTypes()) {
                if (t.id != QLatin1String("prismatic"))
                    continue;
                anchors = t.anchorCount;
                for (const JointParam &p : t.params)
                    params.insert(p.key, p.defaultValue);
            }
        }
        // Driven outwards, with limits wide enough that they are not what
        // stops it -- the test narrows one of them while it runs.
        params.insert(QStringLiteral("enableMotor"), true);
        params.insert(QStringLiteral("motorSpeed"), 400.0);
        // Newtons, and the plate weighs a couple of grams at this scale -- a
        // larger number here would simply overpower the limit.
        params.insert(QStringLiteral("maxMotorForce"), 0.1);
        params.insert(QStringLiteral("enableLimit"), true);
        params.insert(QStringLiteral("lowerTranslation"), -5000.0);
        params.insert(QStringLiteral("upperTranslation"), 5000.0);

        joint = scene.createJoint(QStringLiteral("prismatic"), post, plate, anchors, params);
    }

    // Runs `frames` steps and answers how far the slider has travelled.
    qreal travelAfter(int frames)
    {
        SimulationController sim(&scene, nullptr);
        sim.setEngineName(QStringLiteral("Box2D"));
        sim.start();
        for (int i = 0; i < frames; ++i)
            sim.stepFrame();
        const qreal translation =
            sim.readValue(joint->name(), QStringLiteral("translation")).toDouble();
        sim.stop();
        return translation;
    }
};

} // namespace

// b2PrismaticJoint_SetLimits, reached through the joint's own upperTranslation
// property. The catalogue has always called it live-settable; for a while
// nothing was listening, and the slider ran on as though no rule had fired.
TEST(LiveLimit, TightenedWhileRunning)
{
    const qreal unrestricted = Slider().travelAfter(60);
    ASSERT_GT(unrestricted, 200.0) << "the motor drives the slider outwards at all";

    Slider restricted;
    // Ahead of where the slider has got to when the rule fires, so the limit
    // is what stops it rather than having to drag it back.
    const qreal cap = 400.0;

    Rule tighten;
    tighten.subjectName = Rule::world();
    tighten.conditionKey = QStringLiteral("time");
    tighten.compare = Rule::Compare::Greater;
    tighten.conditionValue = 0.05;
    tighten.targetName = restricted.joint->name();
    tighten.propertyKey = QStringLiteral("upperTranslation");
    tighten.op = Rule::Op::Set;
    tighten.value = cap;
    restricted.scene.setRules({ tighten });

    const qreal stopped = restricted.travelAfter(60);

    EXPECT_LT(stopped, unrestricted - 100.0)
        << "narrowing the limit mid-run stops the slider short of where it would"
           " otherwise have reached";
    EXPECT_NEAR(stopped, cap, 30.0) << "and it stops at about the new limit";
}

// b2PrismaticJoint_SetTargetTranslation. The place the documentation points
// people at for moving something without teleporting it, and the one that was
// least excusable to have left unwired.
TEST(LiveLimit, SpringDrivenToATarget)
{
    Slider slider;
    // The spring drives towards the target; the motor would fight it.
    slider.joint->params().insert(QStringLiteral("enableMotor"), false);
    slider.joint->params().insert(QStringLiteral("enableSpring"), true);
    slider.joint->params().insert(QStringLiteral("hertz"), 4.0);
    slider.joint->params().insert(QStringLiteral("dampingRatio"), 1.0);

    const qreal resting = slider.travelAfter(30);

    Rule drive;
    drive.subjectName = Rule::world();
    drive.conditionKey = QStringLiteral("time");
    drive.compare = Rule::Compare::Greater;
    drive.conditionValue = 0.05;
    drive.targetName = slider.joint->name();
    drive.propertyKey = QStringLiteral("targetTranslation");
    drive.op = Rule::Op::Set;
    drive.value = resting + 250.0;
    slider.scene.setRules({ drive });

    const qreal driven = slider.travelAfter(90);

    EXPECT_GT(driven, resting + 100.0)
        << "the spring pulls the slider towards the target a rule gave it";
}
