// The Planck.js export is one web page of plain Planck code. The scene's rules
// come out as if statements after world.step(), calling Planck on variables
// named after the objects. Planck is duck-typed, so knowing each joint's type
// while exporting also means calling the method that exists: a wheel joint's
// spring is setSpringFrequencyHz and answers to nothing else.

#include "CanvasScene.h"
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

SceneExporter::Converter planckConverter()
{
    const QVector<SceneExporter::Converter> found =
        SceneExporter::discover(shippedConverters());
    for (const SceneExporter::Converter &converter : found) {
        if (converter.name.contains(QStringLiteral("Planck")))
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

QString pageFor(CanvasScene *scene, QStringList *written = nullptr, QStringList *said = nullptr)
{
    const SceneExporter::Converter converter = planckConverter();
    if (converter.name.isEmpty())
        return {};
    QTemporaryDir output;
    if (!output.isValid())
        return {};
    QString error;
    if (!SceneExporter::run(converter, scene, output.path(), QJsonObject(), &error, written, said))
        return error;
    return contentsOf(output.filePath(QStringLiteral("index.html")));
}

// A wheel on a ground plane, driven by a sprung wheel joint.
Joint *buildDriven(CanvasScene *scene, PhysicsBody **wheelBody = nullptr)
{
    scene->setSimulationEngineName(QStringLiteral("Box2D"));

    auto *ground = new RectangleItem;
    ground->setRect(QRectF(0, 0, 600, 40));
    ground->setPos(-300, 200);
    ground->setName(QStringLiteral("ground"));
    scene->addItem(ground);

    auto *wheel = new CircleItem;
    wheel->setRect(QRectF(0, 0, 70, 70));
    wheel->setPos(-35, 100);
    wheel->setName(QStringLiteral("wheel"));
    scene->addItem(wheel);
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
    PhysicsBody *groundBody = makeBody(ground, physics::BodyType::Static);
    PhysicsBody *made = makeBody(wheel, physics::BodyType::Dynamic);
    if (wheelBody)
        *wheelBody = made;

    QVariantMap params;
    params.insert(QStringLiteral("enableSpring"), true);
    params.insert(QStringLiteral("hertz"), 4.0);
    params.insert(QStringLiteral("dampingRatio"), 0.7);
    params.insert(QStringLiteral("enableMotor"), true);
    params.insert(QStringLiteral("motorSpeed"), 120.0);
    params.insert(QStringLiteral("maxMotorTorque"), 500.0);
    Joint *drive = scene->createJoint(QStringLiteral("wheel"), groundBody, made, 1, params);
    scene->setEditorMode(was);
    return drive;
}

} // namespace

TEST(PlanckRules, OnePageOfPlainPlanck)
{
    CanvasScene scene;
    Fixtures::buildCart(&scene);

    QStringList written;
    const QString page = pageFor(&scene, &written);
    ASSERT_FALSE(page.isEmpty());

    EXPECT_EQ(written, QStringList { QStringLiteral("index.html") });
    EXPECT_TRUE(page.contains(QStringLiteral("world = pl.World(")));
    EXPECT_TRUE(page.contains(QStringLiteral("world.step(dt, 8, 3);")));
    EXPECT_TRUE(page.contains(QStringLiteral("chassis = ")))
        << "a fixture a rule names is kept in a variable of its own name";
    for (const char *leftover : { "RuleRunner", "scene.rules", "fixturesByName", "{{" })
        EXPECT_FALSE(page.contains(QLatin1String(leftover))) << leftover;
}

TEST(PlanckRules, AValueRuleIsAnIfAfterTheStep)
{
    CanvasScene scene;
    PhysicsBody *wheelBody = nullptr;
    Joint *drive = buildDriven(&scene, &wheelBody);
    ASSERT_NE(drive, nullptr);

    Rule slowDown;
    slowDown.name = QStringLiteral("ease off");
    slowDown.subjectName = wheelBody->name();
    slowDown.conditionKey = QStringLiteral("speed");
    slowDown.compare = Rule::Compare::Greater;
    slowDown.conditionValue = 300.0;
    slowDown.targetName = drive->name();
    slowDown.propertyKey = QStringLiteral("motorSpeed");
    slowDown.value = 30.0;
    scene.setRules({ slowDown });

    const QString page = pageFor(&scene);
    ASSERT_FALSE(page.isEmpty());

    EXPECT_TRUE(page.contains(
        QStringLiteral("var easeOff = %1.getLinearVelocity().length() > len(300);").arg(wheelBody->name())))
        << page.toStdString();
    EXPECT_TRUE(page.contains(QStringLiteral("if (easeOff && !easeOffBefore) {")));
    EXPECT_TRUE(page.contains(QStringLiteral("%1.setMotorSpeed(rad(30));").arg(drive->name())));
    EXPECT_TRUE(page.contains(QStringLiteral("%1.getBodyA().setAwake(true);").arg(drive->name())));
}

// Every joint used to be asked for setFrequency, which a wheel joint does not
// have.
TEST(PlanckRules, AWheelJointsSpringHasItsOwnName)
{
    CanvasScene scene;
    Joint *drive = buildDriven(&scene);
    ASSERT_NE(drive, nullptr);

    Rule stiffen;
    stiffen.subjectName = Rule::world();
    stiffen.conditionKey = QStringLiteral("time");
    stiffen.compare = Rule::Compare::Greater;
    stiffen.conditionValue = 2.0;
    stiffen.targetName = drive->name();
    stiffen.propertyKey = QStringLiteral("hertz");
    stiffen.value = 8.0;
    scene.setRules({ stiffen });

    const QString page = pageFor(&scene);
    ASSERT_FALSE(page.isEmpty());

    EXPECT_TRUE(page.contains(QStringLiteral("%1.setSpringFrequencyHz(8);").arg(drive->name())))
        << page.toStdString();
    EXPECT_FALSE(page.contains(QStringLiteral(".setFrequency(")));
}

// Planck reports a contact while it steps, when the world cannot be changed, so
// the callback only notes it and step() acts on it afterwards.
TEST(PlanckRules, AContactIsNotedThenActedOn)
{
    CanvasScene scene;
    PhysicsBody *wheelBody = nullptr;
    buildDriven(&scene, &wheelBody);

    Rule land;
    land.subjectName = QStringLiteral("wheel");
    land.eventId = QStringLiteral("contactBegin");
    land.conditionValue = QStringLiteral("ground");
    land.targetName = wheelBody->name();
    land.propertyKey = QStringLiteral("angularVelocity");
    land.value = 0.0;
    scene.setRules({ land });

    const QString page = pageFor(&scene);
    ASSERT_FALSE(page.isEmpty());

    EXPECT_TRUE(page.contains(QStringLiteral("world.on(\"begin-contact\", function (contact) {")));
    EXPECT_TRUE(page.contains(QStringLiteral("contactsBegun.push([contact.getFixtureA(), contact.getFixtureB()]);")));
    EXPECT_TRUE(page.contains(QStringLiteral("(a === wheel && b === ground)")))
        << page.toStdString();
    EXPECT_TRUE(page.contains(QStringLiteral("%1.setAngularVelocity(0);").arg(wheelBody->name())));
    EXPECT_TRUE(page.contains(QStringLiteral("contactsBegun = [];")));
}

// What Planck has no equivalent for stays in the page as the comment saying
// why, and the converter says so on the way out.
TEST(PlanckRules, WhatPlanckCannotDoIsSaidOutLoud)
{
    CanvasScene scene;
    buildDriven(&scene);

    Rule tune;
    tune.name = QStringLiteral("stiffen contacts");
    tune.subjectName = Rule::world();
    tune.conditionKey = QStringLiteral("time");
    tune.conditionValue = 1.0;
    tune.targetName = Rule::world();
    tune.propertyKey = QStringLiteral("contactHertz");
    tune.value = 45.0;
    scene.setRules({ tune });

    QStringList said;
    const QString page = pageFor(&scene, nullptr, &said);
    ASSERT_FALSE(page.isEmpty());
    EXPECT_TRUE(page.contains(QStringLiteral("Not exported"))) << page.toStdString();
    EXPECT_TRUE(said.join(QLatin1Char('\n')).contains(QStringLiteral("stiffen contacts")))
        << said.join(QLatin1Char('\n')).toStdString();
}
