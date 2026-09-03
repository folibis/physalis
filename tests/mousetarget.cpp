#include "CanvasScene.h"
#include "EngineRegistry.h"
#include "Joint.h"
#include "JointTypes.h"
#include "PhysicsBody.h"
#include "RectangleItem.h"
#include "Rule.h"
#include "ShapeItem.h"
#include "SimulationController.h"

#include <QApplication>
#include <QVariantMap>
#include <gtest/gtest.h>

using namespace physics;

// b2MouseJoint_SetTarget. A soft-target joint whose target cannot move is an
// ornament: it holds the body where it was and never does anything else. The
// catalogue called its spring live-settable and setJointParam had no case for
// the type at all, so none of it did anything.
TEST(MouseTarget, LedAcrossTheSceneByARule)
{
    CanvasScene scene;
    scene.setSimulationEngineName(QStringLiteral("Box2D"));

    auto *anchorShape = new RectangleItem;
    anchorShape->setRect(QRectF(0, 0, 40, 40));
    anchorShape->setPos(0, 0);
    auto *boxShape = new RectangleItem;
    boxShape->setRect(QRectF(0, 0, 40, 40));
    boxShape->setPos(0, 200);
    scene.addItem(anchorShape);
    scene.addItem(boxShape);
    scene.notifyShapesChanged();
    scene.setEditorMode(EditorMode::Physics);

    scene.selectForPhysics(anchorShape, true);
    PhysicsBody *anchor = scene.createBodyFromSelection();
    // Box2D refuses a mouse joint whose first body can move.
    anchor->props().type = BodyType::Static;
    scene.clearPhysicsSelection();

    scene.selectForPhysics(boxShape, true);
    PhysicsBody *box = scene.createBodyFromSelection();
    scene.clearPhysicsSelection();

    QVariantMap params;
    int anchors = 0;
    if (auto engine = EngineRegistry::create(QStringLiteral("Box2D"))) {
        for (const JointType &t : engine->jointTypes()) {
            if (t.id != QLatin1String("mouse"))
                continue;
            anchors = t.anchorCount;
            for (const JointParam &p : t.params)
                params.insert(p.key, p.defaultValue);
        }
    }
    Joint *leash = scene.createJoint(QStringLiteral("mouse"), anchor, box, anchors, params);
    ASSERT_NE(leash, nullptr);
    // createJoint puts a single anchor midway between the two bodies. For this
    // joint the anchor is the target, and Box2D takes hold of the box at
    // whatever point is under it -- so put it on the box, or the pull is off
    // centre and mostly spins it.
    leash->setAnchorScenePos(Joint::End::A, box->centerOfMassScenePos());

    Rule lead;
    lead.subjectName = Rule::world();
    lead.conditionKey = QStringLiteral("time");
    lead.compare = Rule::Compare::Greater;
    lead.conditionValue = 0.05;
    lead.targetName = leash->name();
    lead.propertyKey = QStringLiteral("targetX");
    lead.op = Rule::Op::Set;
    lead.value = 500.0;
    scene.setRules({ lead });

    SimulationController sim(&scene, nullptr);
    sim.setEngineName(QStringLiteral("Box2D"));
    sim.start();

    const qreal startX = boxShape->pos().x();
    for (int i = 0; i < 120; ++i)
        sim.stepFrame();

    EXPECT_NEAR(sim.readValue(leash->name(), QStringLiteral("targetX")).toDouble(),
                500.0, 1.0)
        << "the joint reads back the target the rule gave it";
    EXPECT_GT(boxShape->pos().x(), startX + 100.0)
        << "and the box has been drawn towards it";

    sim.stop();
}
