#include "CanvasScene.h"
#include "Joint.h"
#include "PhysicsBody.h"
#include "Rule.h"
#include "SceneExporter.h"
#include "SceneFixtures.h"

#include <QDir>
#include <QFile>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTextStream>
#include <gtest/gtest.h>

// The QML / qml-box2d converter: a Qt Quick project whose Scene.qml holds the
// scene as qml-box2d objects and its rules as JavaScript after each step.

namespace {

QString shippedConverters()
{
    QDir dir(QStringLiteral(__FILE__));
    dir.cdUp();     // tests/
    dir.cdUp();     // the project root
    return dir.absoluteFilePath(QStringLiteral("exporters"));
}

SceneExporter::Converter qmlConverter()
{
    for (const SceneExporter::Converter &converter : SceneExporter::discover(shippedConverters())) {
        if (converter.id == QLatin1String("qml-box2d"))
            return converter;
    }
    return {};
}

QString contentsOf(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    QTextStream in(&file);
    return in.readAll();
}

// The cart, with a rule of each kind the converter writes differently.
void buildScene(CanvasScene *scene)
{
    Fixtures::buildCart(scene);
    QVector<Rule> rules = scene->rules();

    Rule every;
    every.name = QStringLiteral("kick every second");
    every.subjectName = Rule::world();
    every.conditionKey = QStringLiteral("frame");
    every.compare = Rule::Compare::Multiple;
    every.conditionValue = 60.0;
    every.targetName = QStringLiteral("body_3");
    every.propertyKey = QStringLiteral("impulseY");
    every.op = Rule::Op::Set;
    every.value = -2.0;
    rules << every;

    Rule start;
    start.name = QStringLiteral("off it goes");
    start.subjectName = Rule::world();
    start.eventId = Rule::runStartedEvent();
    start.targetName = QStringLiteral("body_3");
    start.propertyKey = QStringLiteral("velocityX");
    start.op = Rule::Op::Set;
    start.value = 120.0;
    rules << start;

    Rule back;
    back.name = QStringLiteral("back to the start");
    back.subjectName = QStringLiteral("frontWheel");
    back.eventId = QStringLiteral("contactBegin");
    back.conditionValue = QStringLiteral("wall");
    back.targetName = QStringLiteral("body_3");
    back.actionId = Rule::initStateAction();
    rules << back;

    scene->setRules(rules);
}

bool exportTo(CanvasScene *scene, const QString &folder, QString *error, QStringList *log = nullptr)
{
    const SceneExporter::Converter converter = qmlConverter();
    if (converter.id.isEmpty()) {
        *error = QStringLiteral("no qml-box2d converter under ") + shippedConverters();
        return false;
    }
    QStringList written;
    return SceneExporter::run(converter, scene, folder, QJsonObject(), error, &written, log);
}

} // namespace

TEST(QmlExport, WritesAQtQuickProject)
{
    CanvasScene scene;
    buildScene(&scene);

    QTemporaryDir output;
    ASSERT_TRUE(output.isValid());
    QString error;
    ASSERT_TRUE(exportTo(&scene, output.path(), &error)) << error.toStdString();

    for (const char *file : {"CMakeLists.txt", "scale-box2d.cmake", "main.cpp", "Main.qml", "Scene.qml",
                             "JointView.qml", "SensorHatch.qml"}) {
        const QString path = output.filePath(QString::fromLatin1(file));
        EXPECT_TRUE(QFile::exists(path)) << "no " << file;
        // Every object is a template; one left half filled would not build.
        EXPECT_FALSE(contentsOf(path).contains(QStringLiteral("{{"))) << file;
        EXPECT_FALSE(contentsOf(path).contains(QStringLiteral("//!"))) << file << " carries a template's note";
    }

    const QString qml = contentsOf(output.filePath(QStringLiteral("Scene.qml")));
    EXPECT_TRUE(qml.contains(QStringLiteral("import Box2D 2.0")));
    EXPECT_TRUE(qml.contains(QStringLiteral("pixelsPerMeter: scene.ppm")));
    EXPECT_TRUE(qml.contains(QStringLiteral("bodyType: Body.Dynamic")));
    EXPECT_TRUE(qml.contains(QStringLiteral("WheelJoint {"))) << qml.toStdString();
    EXPECT_TRUE(qml.contains(QStringLiteral("Circle {")));
    // The contact rule: the watched wheel reports itself, and the rule acts on it.
    EXPECT_TRUE(qml.contains(QStringLiteral("onBeginContact: (other) => scene.contactsBegun.push([frontWheel, other])")))
        << qml.toStdString();
    EXPECT_TRUE(qml.contains(QStringLiteral("initState(body_3);")));
    EXPECT_TRUE(qml.contains(QStringLiteral("% 60 === 0"))) << "the multiple-of rule";
    EXPECT_TRUE(qml.contains(QStringLiteral("body_3.applyLinearImpulse(")));
    // Axes are the scene's own, at the editor's centre of mass: qml-box2d's sit at
    // the origin of every static body.
    EXPECT_TRUE(qml.contains(QStringLiteral("flags: DebugDraw.Shape\n")));
    // Joints are drawn by the scene too: qml-box2d draws nothing where an
    // anchor sits on its body's origin.
    EXPECT_TRUE(qml.contains(QStringLiteral("JointView {")));
    EXPECT_TRUE(qml.contains(QStringLiteral("[wheel_1, Qt.point(")) && qml.contains(QStringLiteral("[wheel_2, Qt.point(")))
        << "every joint in the view's list";
    EXPECT_TRUE(qml.contains(QStringLiteral("visible: scene.debug")));
    // Built two pixels inside the drawn outline, so shapes drawn flush slide past.
    EXPECT_TRUE(qml.contains(QStringLiteral("width: 216"))) << "the chassis, 220 wide";
    // ...and denser by the area it lost, so it weighs, and balances, as drawn.
    EXPECT_TRUE(qml.contains(QStringLiteral("density: 1.107085"))) << "220 x 50 built as 216 x 46";

    EXPECT_TRUE(contentsOf(output.filePath(QStringLiteral("main.cpp"))).contains(QStringLiteral("setSamples(4)")))
        << "multisampled, so turned edges are smooth";

    const QString cmake = contentsOf(output.filePath(QStringLiteral("CMakeLists.txt")));
    EXPECT_TRUE(cmake.contains(QStringLiteral("qml-box2d")));
    EXPECT_TRUE(cmake.contains(QStringLiteral("qmlbox2d")));
    EXPECT_TRUE(cmake.contains(QStringLiteral("QML_FILES Main.qml Scene.qml JointView.qml SensorHatch.qml")));
    const QString scale = contentsOf(output.filePath(QStringLiteral("scale-box2d.cmake")));
    EXPECT_TRUE(scale.contains(QStringLiteral("b2_linearSlop;0.0005"))) << scale.toStdString();
}

// qml-box2d hands angles to Box2D with their sign flipped, which turns a
// revolute joint's range over; the export has to put it in upside down.
TEST(QmlExport, RevoluteLimitsGoInTurnedOver)
{
    CanvasScene scene;
    scene.setSimulationEngineName(QStringLiteral("Box2D"));
    auto *a = scene.addCircle(QPointF(0, 0));
    auto *b = scene.addCircle(QPointF(100, 0));
    scene.notifyShapesChanged();
    scene.setEditorMode(EditorMode::Physics);
    scene.selectForPhysics(a, true);
    PhysicsBody *bodyA = scene.createBodyFromSelection();
    scene.clearPhysicsSelection();
    scene.selectForPhysics(b, true);
    PhysicsBody *bodyB = scene.createBodyFromSelection();
    ASSERT_TRUE(bodyA && bodyB);
    QVariantMap params;
    params.insert(QStringLiteral("enableLimit"), true);
    params.insert(QStringLiteral("lowerAngle"), -30.0);
    params.insert(QStringLiteral("upperAngle"), 60.0);
    ASSERT_TRUE(scene.createJoint(QStringLiteral("revolute"), bodyA, bodyB, 1, params));

    QTemporaryDir output;
    QString error;
    ASSERT_TRUE(exportTo(&scene, output.path(), &error)) << error.toStdString();
    const QString qml = contentsOf(output.filePath(QStringLiteral("Scene.qml")));
    EXPECT_TRUE(qml.contains(QStringLiteral("lowerAngle: 60"))) << qml.toStdString();
    EXPECT_TRUE(qml.contains(QStringLiteral("upperAngle: -30")));
}
