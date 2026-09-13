#include "CanvasScene.h"
#include "EngineRegistry.h"
#include "PhysicsBody.h"
#include "RectangleItem.h"
#include "SceneSerializer.h"
#include "ShapeItem.h"

#include <QJsonArray>
#include <QSet>
#include <QJsonObject>
#include <gtest/gtest.h>

// A body, a shape and the world hold whatever the engine says they have, keyed
// by the names the engine publishes -- the same bargain joints have always
// had. The editor stores and shows them without knowing one of them.

namespace {

ShapeItem *shapeWithBody(CanvasScene *scene)
{
    ShapeItem *shape = scene->addRectangle(QPointF(0, 0));
    scene->setEditorMode(EditorMode::Physics);
    scene->selectForPhysics(shape, true);
    scene->createBodyFromSelection();
    scene->clearPhysicsSelection();
    return shape;
}

} // namespace

TEST(EngineOwned, NothingIsStoredUntilItIsChanged)
{
    CanvasScene scene;
    ShapeItem *shape = shapeWithBody(&scene);
    ASSERT_TRUE(shape->body());

    EXPECT_TRUE(scene.world().params.isEmpty()) << "an untouched world holds nothing of its own";
    EXPECT_TRUE(shape->body()->props().params.isEmpty()) << "nor does an untouched body";
    EXPECT_TRUE(shape->part().params.isEmpty()) << "nor an untouched shape";

    // What they are, then, is what the engine said they start as.
    auto engine = physics::EngineRegistry::create(scene.simulationEngineName());
    ASSERT_TRUE(engine);
    const QVariantMap shapeDefaults = physics::storedDefaults(engine->shapeProperties());
    EXPECT_TRUE(shapeDefaults.contains(QStringLiteral("density")))
        << "and the engine does say what a shape starts as";
    EXPECT_DOUBLE_EQ(shapeDefaults.value(QStringLiteral("density")).toDouble(), 1.0);
}

TEST(EngineOwned, ValuesSurviveSavingAndOpening)
{
    CanvasScene source;
    ShapeItem *shape = shapeWithBody(&source);
    shape->part().params["density"] = 2.5;
    shape->part().params["friction"] = 0.12;
    shape->body()->props().params["gravityScale"] = 0.25;
    source.world().params["gravityY"] = 4.0;

    CanvasScene opened;
    QString error;
    ASSERT_TRUE(SceneSerializer::load(&opened, SceneSerializer::save(&source), &error))
        << error.toStdString();

    ASSERT_EQ(opened.shapes().size(), 1);
    ASSERT_EQ(opened.bodies().size(), 1);
    EXPECT_DOUBLE_EQ(opened.shapes().first()->part().params["density"].toDouble(), 2.5);
    EXPECT_DOUBLE_EQ(opened.shapes().first()->part().params["friction"].toDouble(), 0.12);
    EXPECT_DOUBLE_EQ(opened.bodies().first()->props().params["gravityScale"].toDouble(), 0.25);
    EXPECT_DOUBLE_EQ(opened.world().params["gravityY"].toDouble(), 4.0);
}

// Scenes written before the engine described its own bodies kept those values
// loose in the file, under Box2D's names -- which are the names its catalogue
// publishes, so they carry straight over.
TEST(EngineOwned, AnOlderFileStillCarriesItsSettings)
{
    CanvasScene source;
    shapeWithBody(&source);
    QJsonObject document = SceneSerializer::save(&source);

    QJsonObject world = document.value("world").toObject();
    world.remove("physics");
    world.insert("gravity", QJsonObject { {"x", 0.0}, {"y", 4.0} });
    world.insert("contactHertz", 45.0);
    world.insert("enableContinuous", false);
    document.insert("world", world);

    QJsonArray bodies = document.value("bodies").toArray();
    QJsonObject body = bodies.first().toObject();
    body.remove("physics");
    body.insert("linearVelocity", QJsonObject { {"x", 30.0}, {"y", -5.0} });
    body.insert("gravityScale", 0.5);
    body.insert("isBullet", true);
    bodies.replace(0, body);
    document.insert("bodies", bodies);

    // A shape's settings were already a block of their own, but its collision
    // bits were hex text rather than numbers.
    QJsonArray shapes = document.value("shapes").toArray();
    QJsonObject shape = shapes.first().toObject();
    shape.insert("physics", QJsonObject { {"density", 3.0},
                                          {"maskBits", QStringLiteral("0x00000000000000ff")} });
    shapes.replace(0, shape);
    document.insert("shapes", shapes);

    CanvasScene opened;
    QString error;
    ASSERT_TRUE(SceneSerializer::load(&opened, document, &error)) << error.toStdString();

    const QVariantMap &worldValues = opened.world().params;
    EXPECT_DOUBLE_EQ(worldValues["gravityY"].toDouble(), 4.0) << "gravity came across";
    EXPECT_DOUBLE_EQ(worldValues["contactHertz"].toDouble(), 45.0);
    EXPECT_FALSE(worldValues["enableContinuous"].toBool());

    ASSERT_EQ(opened.bodies().size(), 1);
    const QVariantMap &bodyValues = opened.bodies().first()->props().params;
    EXPECT_DOUBLE_EQ(bodyValues["velocityX"].toDouble(), 30.0) << "as did the starting velocity";
    EXPECT_DOUBLE_EQ(bodyValues["velocityY"].toDouble(), -5.0);
    EXPECT_DOUBLE_EQ(bodyValues["gravityScale"].toDouble(), 0.5);
    EXPECT_TRUE(bodyValues["isBullet"].toBool());

    ASSERT_EQ(opened.shapes().size(), 1);
    const QVariantMap &shapeValues = opened.shapes().first()->part().params;
    EXPECT_DOUBLE_EQ(shapeValues["density"].toDouble(), 3.0);
    EXPECT_DOUBLE_EQ(shapeValues["maskBits"].toDouble(), 255.0)
        << "and the bits are a number now, not text";
}

// The two things the editor itself has to recognise about a shape are the
// engine's to name: it hatches what things pass through, and works out where a
// body balances from what its shapes are made of.
TEST(EngineOwned, TheEngineNamesWhatTheEditorMustRecognise)
{
    CanvasScene scene;
    ShapeItem *shape = shapeWithBody(&scene);

    EXPECT_TRUE(scene.hasSensorProperty()) << "Box2D has something things pass through";
    EXPECT_FALSE(scene.isSensorShape(shape)) << "and a new shape is not one";
    scene.setSensorShape(shape, true);
    EXPECT_TRUE(scene.isSensorShape(shape)) << "which the editor sets through that same property";
    EXPECT_TRUE(shape->part().params[scene.sensorPropertyKey()].toBool())
        << "and stores under the name the engine gave it";

    EXPECT_DOUBLE_EQ(scene.shapeDensity(shape), 1.0) << "density falls back to the engine default";
    shape->part().params["density"] = 4.0;
    EXPECT_DOUBLE_EQ(scene.shapeDensity(shape), 4.0);
}

// Everything the engine's API can answer about a body, a shape and the world
// is offered: these are the ones that used to be missing.
TEST(EngineOwned, TheCatalogueCoversTheEngineApi)
{
    for (const char *engineName : { "Box2D", "Chipmunk2D" }) {
        auto engine = physics::EngineRegistry::create(QString::fromLatin1(engineName));
        ASSERT_TRUE(engine) << engineName;

        QSet<QString> bodyKeys;
        for (const physics::JointParam &p : engine->bodyProperties())
            bodyKeys.insert(p.key);
        QSet<QString> shapeKeys;
        for (const physics::JointParam &p : engine->shapeProperties())
            shapeKeys.insert(p.key);
        QSet<QString> worldKeys;
        for (const physics::JointParam &p : engine->worldProperties())
            worldKeys.insert(p.key);
        QSet<QString> actions;
        for (const physics::ActionType &a : engine->bodyActions())
            actions.insert(a.id);

        for (const char *key : { "contactCount", "jointCount", "boundsMinX", "boundsMaxY",
                                 "localCenterOfMassX", "enableContactEvents", "enableHitEvents" }) {
            EXPECT_TRUE(bodyKeys.contains(QString::fromLatin1(key)))
                << engineName << " offers a body's " << key;
        }
        for (const char *key : { "contactCount", "boundsMinX", "boundsMaxY", "rotationalInertia",
                                 "centerOfMassX" }) {
            EXPECT_TRUE(shapeKeys.contains(QString::fromLatin1(key)))
                << engineName << " offers a shape's " << key;
        }
        EXPECT_TRUE(worldKeys.contains(QStringLiteral("shapeCount")))
            << engineName << " counts its shapes";
        for (const char *id : { "pushForceAt", "resetMass" }) {
            EXPECT_TRUE(actions.contains(QString::fromLatin1(id)))
                << engineName << " can " << id;
        }
    }
}

// And what they publish, they answer.
TEST(EngineOwned, TheNewReadingsAnswerWhileRunning)
{
    for (const char *engineName : { "Box2D", "Chipmunk2D" }) {
        auto engine = physics::EngineRegistry::create(QString::fromLatin1(engineName));
        ASSERT_TRUE(engine) << engineName;
        physics::WorldDesc world;
        engine->createWorld(world);

        physics::ShapePart part;
        part.name = QStringLiteral("crate");
        part.geometry.kind = physics::GeometryKind::Box;
        part.geometry.halfExtents = QPointF(20, 20);

        physics::BodyDesc desc;
        desc.name = QStringLiteral("crate_body");
        desc.parts.append(part);
        const physics::BodyHandle body = engine->addBody(desc);
        ASSERT_NE(body, physics::kInvalidBody) << engineName;
        engine->step(1.0 / 60.0);

        const QVariant width = engine->bodyValue(body, QStringLiteral("boundsMaxX"));
        ASSERT_TRUE(width.isValid()) << engineName << " reports its bounds";
        EXPECT_NEAR(width.toDouble(), 20.0, 2.0)
            << engineName << " -- a 40-unit box reaches 20 either side";
        EXPECT_EQ(engine->bodyValue(body, QStringLiteral("contactCount")).toInt(), 0)
            << engineName << " -- nothing is touching it";
        EXPECT_EQ(engine->bodyValue(body, QStringLiteral("jointCount")).toInt(), 0)
            << engineName;
        EXPECT_GT(engine->shapeValue(QStringLiteral("crate"), QStringLiteral("rotationalInertia"))
                      .toDouble(), 0.0)
            << engineName << " -- the shape has a moment of its own";
        EXPECT_EQ(engine->worldValue(QStringLiteral("shapeCount")).toInt(), 1) << engineName;

        // A mass set by hand, then thrown away again.
        engine->setBodyParam(body, QStringLiteral("mass"), 5.0);
        EXPECT_NEAR(engine->bodyValue(body, QStringLiteral("mass")).toDouble(), 5.0, 1e-6)
            << engineName;
        engine->performAction(QStringLiteral("resetMass"), body, {});
        EXPECT_LT(engine->bodyValue(body, QStringLiteral("mass")).toDouble(), 5.0)
            << engineName << " -- recalculated from the shapes";

        engine->destroyWorld();
    }
}
