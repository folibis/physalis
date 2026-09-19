// The Qt/Box2D export is one plain Box2D program. The scene's rules are not
// carried into it as data for something to interpret: each comes out as an if
// statement after b2World_Step, calling Box2D on variables named after the
// objects -- the code someone who knows only C++ and Box2D would write.

#include "CanvasScene.h"
#include "ExplosionItem.h"
#include "CircleItem.h"
#include "Joint.h"
#include "PhysicsBody.h"
#include "RectangleItem.h"
#include "Rule.h"
#include "SceneExporter.h"
#include "SceneFixtures.h"

#include <QDir>
#include <QFile>
#include <QJsonObject>
#include <QTemporaryDir>
#include <gtest/gtest.h>

namespace {

QString shippedConverters()
{
    QDir dir(QStringLiteral(__FILE__));
    dir.cdUp();     // tests/
    dir.cdUp();     // the project root
    return dir.absoluteFilePath(QStringLiteral("exporters"));
}

SceneExporter::Converter qtProjectConverter()
{
    const QVector<SceneExporter::Converter> found =
        SceneExporter::discover(shippedConverters());
    for (const SceneExporter::Converter &converter : found) {
        if (converter.name.contains(QStringLiteral("Qt project")))
            return converter;
    }
    return {};
}

QString contentsOf(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    return QString::fromUtf8(file.readAll());
}

// A ball over a ground plane, hung off a revolute joint with a limit and a
// motor.
struct Rig {
    ShapeItem *ground = nullptr;
    ShapeItem *ball = nullptr;
    PhysicsBody *groundBody = nullptr;
    PhysicsBody *ballBody = nullptr;
    Joint *arm = nullptr;
};

Rig buildRig(CanvasScene *scene)
{
    scene->setSimulationEngineName(QStringLiteral("Box2D"));

    Rig rig;
    auto *ground = new RectangleItem;
    ground->setRect(QRectF(0, 0, 600, 40));
    ground->setPos(-300, 200);
    ground->setName(QStringLiteral("ground"));
    scene->addItem(ground);
    rig.ground = ground;

    auto *ball = new CircleItem;
    ball->setRect(QRectF(0, 0, 60, 60));
    ball->setPos(-30, 0);
    ball->setName(QStringLiteral("ball"));
    scene->addItem(ball);
    rig.ball = ball;
    scene->notifyShapesChanged();

    const EditorMode was = scene->editorMode();
    scene->setEditorMode(EditorMode::Physics);

    const auto makeBody = [scene](ShapeItem *shape, physics::BodyType type) {
        scene->clearPhysicsSelection();
        scene->selectForPhysics(shape, true);
        PhysicsBody *body = scene->createBodyFromSelection();
        body->props().type = type;
        shape->part().params["enableContactEvents"] = true;
        scene->clearPhysicsSelection();
        return body;
    };
    rig.groundBody = makeBody(ground, physics::BodyType::Static);
    rig.ballBody = makeBody(ball, physics::BodyType::Dynamic);

    QVariantMap params;
    params.insert(QStringLiteral("enableLimit"), true);
    params.insert(QStringLiteral("lowerAngle"), -30.0);
    params.insert(QStringLiteral("upperAngle"), 30.0);
    params.insert(QStringLiteral("enableMotor"), true);
    params.insert(QStringLiteral("motorSpeed"), 90.0);
    params.insert(QStringLiteral("maxMotorTorque"), 500.0);
    rig.arm = scene->createJoint(QStringLiteral("revolute"), rig.groundBody, rig.ballBody,
                                 1, params);

    scene->setEditorMode(was);
    return rig;
}

// Exports a scene; hands back main.cpp and, when asked, the files written.
QString mainFor(CanvasScene *scene, QStringList *written = nullptr)
{
    const SceneExporter::Converter converter = qtProjectConverter();
    if (converter.name.isEmpty())
        return {};

    QTemporaryDir output;
    if (!output.isValid())
        return {};
    QString error;
    if (!SceneExporter::run(converter, scene, output.path(), QJsonObject(), &error, written))
        return error;
    return contentsOf(output.filePath(QStringLiteral("main.cpp")));
}

} // namespace

// One source file and the CMake file that builds it; nothing of the editor
// comes along.
TEST(RulesAsCode, OneFileOfPlainBox2D)
{
    CanvasScene scene;
    Fixtures::buildCart(&scene);

    QStringList written;
    const QString code = mainFor(&scene, &written);
    ASSERT_FALSE(code.isEmpty());

    written.sort();
    EXPECT_EQ(written, (QStringList { QStringLiteral("CMakeLists.txt"), QStringLiteral("main.cpp") }));

    EXPECT_TRUE(code.contains(QStringLiteral("world = b2CreateWorld(&worldDef);")));
    EXPECT_TRUE(code.contains(QStringLiteral("b2World_Step(world, dt, ")));
    EXPECT_TRUE(code.contains(QStringLiteral("chassis = b2CreatePolygonShape(")))
        << "a shape a rule names is kept in a variable of its own name";
    for (const char *leftover : { "Rule", "Scene scene", "shapesByName", "readValue", "{{" })
        EXPECT_FALSE(code.contains(QLatin1String(leftover))) << leftover;
}

// A def field is written only when it is not already Box2D's default.
TEST(RulesAsCode, DefaultsAreNotWrittenOut)
{
    CanvasScene scene;
    buildRig(&scene);

    const QString code = mainFor(&scene);
    ASSERT_FALSE(code.isEmpty());

    EXPECT_FALSE(code.contains(QStringLiteral("bodyDef.type = b2_staticBody")))
        << "static is what b2DefaultBodyDef already is";
    EXPECT_TRUE(code.contains(QStringLiteral("bodyDef.type = b2_dynamicBody;")));
    EXPECT_FALSE(code.contains(QStringLiteral("bodyDef.gravityScale")));
    EXPECT_FALSE(code.contains(QStringLiteral("bodyDef.isBullet")));
    EXPECT_TRUE(code.contains(QStringLiteral("jointDef.enableLimit = true;")));
    EXPECT_FALSE(code.contains(QStringLiteral("jointDef.enableSpring")));
}

// A condition on a value is checked after every step and acts on the step it
// becomes true.
TEST(RulesAsCode, AValueRuleIsAnIfAfterTheStep)
{
    CanvasScene scene;
    Rig rig = buildRig(&scene);
    ASSERT_NE(rig.arm, nullptr);
    const QString arm = rig.arm->name();

    Rule stop;
    stop.name = QStringLiteral("stop at the top");
    stop.subjectName = arm;
    stop.conditionKey = QStringLiteral("angle");
    stop.compare = Rule::Compare::Greater;
    stop.conditionValue = 25.0;
    stop.targetName = arm;
    stop.propertyKey = QStringLiteral("motorSpeed");
    stop.value = 0.0;
    scene.setRules({ stop });

    const QString code = mainFor(&scene);
    ASSERT_FALSE(code.isEmpty());

    EXPECT_TRUE(code.contains(QStringLiteral("// stop at the top")));
    EXPECT_TRUE(code.contains(
        QStringLiteral("bool stopAtTheTop = b2RevoluteJoint_GetAngle(%1) > rad(25);").arg(arm)))
        << code.toStdString();
    EXPECT_TRUE(code.contains(QStringLiteral("if (stopAtTheTop && !stopAtTheTopBefore) {")));
    EXPECT_TRUE(code.contains(QStringLiteral("b2RevoluteJoint_SetMotorSpeed(%1, 0.0f);").arg(arm)));
    EXPECT_TRUE(code.contains(QStringLiteral("stopAtTheTopBefore = false;")))
        << "createWorld starts it over";
}

// "Is a multiple of" is asked in the editor's units, as whole numbers.
TEST(RulesAsCode, AMultipleIsAskedInTheEditorsUnits)
{
    CanvasScene scene;
    Rig rig = buildRig(&scene);
    ASSERT_NE(rig.arm, nullptr);
    const QString arm = rig.arm->name();

    Rule every;
    every.name = QStringLiteral("every ten degrees");
    every.subjectName = arm;
    every.conditionKey = QStringLiteral("angle");
    every.compare = Rule::Compare::Multiple;
    every.conditionValue = 10.0;
    every.targetName = arm;
    every.propertyKey = QStringLiteral("motorSpeed");
    every.value = 0.0;
    scene.setRules({ every });

    const QString code = mainFor(&scene);
    ASSERT_FALSE(code.isEmpty());
    const QString whole = QStringLiteral("std::llround((b2RevoluteJoint_GetAngle(%1)) * 180.0f / B2_PI)").arg(arm);
    EXPECT_TRUE(code.contains(QStringLiteral("(%1 != 0 && %1 % 10 == 0)").arg(whole)))
        << code.toStdString();
}

// An event is read straight from Box2D's contact events.
TEST(RulesAsCode, AnEventRuleReadsBox2DsContactEvents)
{
    CanvasScene scene;
    Rig rig = buildRig(&scene);

    Rule bounce;
    bounce.subjectName = rig.ball->name();
    bounce.eventId = QStringLiteral("contactBegin");
    bounce.conditionValue = rig.ground->name();
    bounce.targetName = rig.ballBody->name();
    bounce.propertyKey = QStringLiteral("velocityY");
    bounce.value = -400.0;
    scene.setRules({ bounce });

    const QString code = mainFor(&scene);
    ASSERT_FALSE(code.isEmpty());

    EXPECT_TRUE(code.contains(QStringLiteral("b2ContactEvents contacts = b2World_GetContactEvents(world);")));
    EXPECT_TRUE(code.contains(QStringLiteral("for (int i = 0; i < contacts.beginCount; ++i) {")));
    EXPECT_TRUE(code.contains(QStringLiteral("B2_ID_EQUALS(a, ball) && B2_ID_EQUALS(b, ground)")))
        << code.toStdString();
    EXPECT_TRUE(code.contains(QStringLiteral("b2Body_SetLinearVelocity(%1, b2Vec2{ b2Body_GetLinearVelocity(%1).x, m(-400) });")
                                  .arg(rig.ballBody->name())));
}

// Arriving at a joint limit is a condition like any other; starting against the
// stop does not count as arriving at it.
TEST(RulesAsCode, ALimitRuleIsTheJointsOwnLimitCheck)
{
    CanvasScene scene;
    Rig rig = buildRig(&scene);
    const QString arm = rig.arm->name();

    Rule reverse;
    reverse.name = QStringLiteral("reverse");
    reverse.subjectName = arm;
    reverse.eventId = QStringLiteral("limitUpper");
    reverse.targetName = arm;
    reverse.propertyKey = QStringLiteral("motorSpeed");
    reverse.op = Rule::Op::Negate;
    scene.setRules({ reverse });

    const QString code = mainFor(&scene);
    ASSERT_FALSE(code.isEmpty());

    EXPECT_TRUE(code.contains(QStringLiteral("b2RevoluteJoint_IsLimitEnabled(%1)").arg(arm)));
    EXPECT_TRUE(code.contains(QStringLiteral("b2RevoluteJoint_GetUpperLimit(%1)").arg(arm)));
    EXPECT_TRUE(code.contains(
        QStringLiteral("b2RevoluteJoint_SetMotorSpeed(%1, -b2RevoluteJoint_GetMotorSpeed(%1));").arg(arm)))
        << code.toStdString();
    EXPECT_TRUE(code.contains(QStringLiteral("reverseBefore = true;")));
}

// "Whatever touched it" is the other shape of the contact Box2D reported.
TEST(RulesAsCode, TheOtherShapeIsTheOtherSideOfTheContact)
{
    CanvasScene scene;
    Rig rig = buildRig(&scene);

    Rule shove;
    shove.name = QStringLiteral("float");
    shove.subjectName = rig.ground->name();
    shove.eventId = QStringLiteral("contactBegin");
    shove.targetName = Rule::otherObjectBody();
    shove.propertyKey = QStringLiteral("gravityScale");
    shove.value = 0.0;
    shove.once = true;
    scene.setRules({ shove });

    const QString code = mainFor(&scene);
    ASSERT_FALSE(code.isEmpty());

    EXPECT_TRUE(code.contains(QStringLiteral("b2ShapeId other = B2_ID_EQUALS(a, ground) ? b : a;")))
        << code.toStdString();
    EXPECT_TRUE(code.contains(QStringLiteral("b2Body_SetGravityScale(b2Shape_GetBody(other), 0.0f);")));
    EXPECT_TRUE(code.contains(QStringLiteral("floatRuleDone = true;")))
        << "a rule named after a C++ keyword still gets a readable variable";
}

// A rule switched off in the editor is not in the program.
TEST(RulesAsCode, ARuleSwitchedOffIsLeftOut)
{
    CanvasScene scene;
    Rig rig = buildRig(&scene);

    Rule off;
    off.name = QStringLiteral("parked");
    off.subjectName = rig.arm->name();
    off.conditionKey = QStringLiteral("angle");
    off.conditionValue = 1.0;
    off.targetName = rig.arm->name();
    off.propertyKey = QStringLiteral("motorSpeed");
    off.value = 5.0;
    off.enabled = false;
    scene.setRules({ off });

    const QString code = mainFor(&scene);
    ASSERT_FALSE(code.isEmpty());
    EXPECT_TRUE(code.contains(QStringLiteral("switched off in the editor")));
    EXPECT_FALSE(code.contains(QStringLiteral("b2RevoluteJoint_SetMotorSpeed")));
}

// A rule that sets off an explosion names the explosion, not a body; it was
// once not exported at all, and a chain of events waiting on it never started.
TEST(RulesAsCode, AnExplosionIsBox2DsOwn)
{
    CanvasScene scene;
    Rig rig = buildRig(&scene);
    ASSERT_NE(rig.ball, nullptr);
    ExplosionItem *blast = scene.addExplosion(QPointF(10, 50));
    ASSERT_NE(blast, nullptr);
    blast->params().insert(QStringLiteral("impulse"), 5.0);

    Rule boom;
    boom.subjectName = Rule::world();
    boom.conditionKey = QStringLiteral("time");
    boom.compare = Rule::Compare::Greater;
    boom.conditionValue = 1.0;
    boom.targetName = blast->name();
    boom.actionId = QStringLiteral("explode");
    boom.actionParams.insert(QStringLiteral("radius"), 200.0);
    boom.actionParams.insert(QStringLiteral("impulse"), 3.0);
    scene.setRules({ boom });

    const QString code = mainFor(&scene);
    EXPECT_TRUE(code.contains(QStringLiteral("explosion.position = m(10, 50);"))) << code.toStdString();
    EXPECT_TRUE(code.contains(QStringLiteral("explosion.impulsePerLength = m(5);")))
        << "the explosion's own impulse wins over the rule's";
    EXPECT_TRUE(code.contains(QStringLiteral("b2World_Explode(world, &explosion);")));
}

// Box2D measures a prismatic joint between its anchors; the editor counts
// travel from where the joint starts, and its engine adds the gap to the limits.
TEST(RulesAsCode, ASlidersTravelCountsFromWhereItStarts)
{
    CanvasScene scene;
    scene.setSimulationEngineName(QStringLiteral("Box2D"));
    auto *base = scene.addCircle(QPointF(0, 0));
    auto *lift = scene.addCircle(QPointF(0, 300));
    scene.notifyShapesChanged();
    scene.setEditorMode(EditorMode::Physics);
    scene.selectForPhysics(base, true);
    PhysicsBody *baseBody = scene.createBodyFromSelection();
    scene.clearPhysicsSelection();
    scene.selectForPhysics(lift, true);
    PhysicsBody *liftBody = scene.createBodyFromSelection();
    ASSERT_TRUE(baseBody && liftBody);

    QVariantMap params;
    params.insert(QStringLiteral("enableLimit"), true);
    params.insert(QStringLiteral("lowerTranslation"), 0.0);
    params.insert(QStringLiteral("upperTranslation"), 100.0);
    Joint *slide = scene.createJoint(QStringLiteral("prismatic"), liftBody, baseBody, 2, params);
    ASSERT_NE(slide, nullptr);
    slide->setAnchorScenePos(Joint::End::A, QPointF(30, 330));
    slide->setAnchorScenePos(Joint::End::B, QPointF(30, 30));
    slide->setAxisScene(QPointF(0, -1));

    const QString code = mainFor(&scene);
    EXPECT_TRUE(code.contains(QStringLiteral("jointDef.lowerTranslation = m(300);"))) << code.toStdString();
    EXPECT_TRUE(code.contains(QStringLiteral("jointDef.upperTranslation = m(400);")));
}

// A scene keeps a 64-bit collision filter as a hex string. Read as a JavaScript
// number, all-ones came out as 18446744073709552000, which C++ wraps round to
// 384: every shape collided with nothing.
TEST(RulesAsCode, CollisionBitsAreExact)
{
    CanvasScene scene;
    Rig rig = buildRig(&scene);
    ASSERT_NE(rig.ball, nullptr);
    rig.ball->part().params[QStringLiteral("maskBits")] = QStringLiteral("0x0000000000000005");
    rig.ball->part().params[QStringLiteral("categoryBits")] = QStringLiteral("0x0000000000000002");
    rig.ground->part().params[QStringLiteral("maskBits")] = QStringLiteral("0xffffffffffffffff");

    const QString code = mainFor(&scene);
    EXPECT_TRUE(code.contains(QStringLiteral("shapeDef.filter.maskBits = 0x5ull;"))) << code.toStdString();
    EXPECT_TRUE(code.contains(QStringLiteral("shapeDef.filter.categoryBits = 0x2ull;")));
    EXPECT_FALSE(code.contains(QStringLiteral("18446744073709552000")));
}
