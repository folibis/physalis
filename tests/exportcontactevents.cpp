// Box2D 3.1's b2DefaultShapeDef leaves contact events off, and the editor turns
// them on for every shape an event rule watches when a run starts. An export
// that only copied the shape's own flag produced a program in which the
// contact a rule waits for was never reported, so the rule never fired.

#include "CanvasScene.h"
#include "CircleItem.h"
#include "PhysicsBody.h"
#include "RectangleItem.h"
#include "Rule.h"
#include "SceneExporter.h"

#include <QDir>
#include <QFile>
#include <QJsonObject>
#include <QTemporaryDir>
#include <gtest/gtest.h>

namespace {

QString exported(CanvasScene *scene)
{
    QDir dir(QStringLiteral(__FILE__));
    dir.cdUp();
    dir.cdUp();
    const QVector<SceneExporter::Converter> found =
        SceneExporter::discover(dir.absoluteFilePath(QStringLiteral("exporters")));
    for (const SceneExporter::Converter &converter : found) {
        if (!converter.name.contains(QStringLiteral("Qt project")))
            continue;
        QTemporaryDir output;
        QString error;
        if (!output.isValid()
            || !SceneExporter::run(converter, scene, output.path(), QJsonObject(), &error))
            return error;
        QFile file(output.filePath(QStringLiteral("main.cpp")));
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
            return {};
        return QString::fromUtf8(file.readAll());
    }
    return {};
}

} // namespace

TEST(ExportContactEvents, AWatchedShapeReportsItsContacts)
{
    CanvasScene scene;
    scene.setSimulationEngineName(QStringLiteral("Box2D"));

    auto *wall = new RectangleItem;
    wall->setRect(QRectF(0, 0, 40, 200));
    wall->setPos(200, 0);
    wall->setName(QStringLiteral("wall"));
    scene.addItem(wall);

    auto *ball = new CircleItem;
    ball->setRect(QRectF(0, 0, 30, 30));
    ball->setPos(0, 50);
    ball->setName(QStringLiteral("ball"));
    scene.addItem(ball);
    scene.notifyShapesChanged();
    scene.setEditorMode(EditorMode::Physics);

    for (ShapeItem *shape : { static_cast<ShapeItem *>(wall), static_cast<ShapeItem *>(ball) }) {
        scene.clearPhysicsSelection();
        scene.selectForPhysics(shape, true);
        PhysicsBody *body = scene.createBodyFromSelection();
        body->props().type = shape == wall ? physics::BodyType::Static : physics::BodyType::Dynamic;
        // Neither shape asks for contact events itself.
        shape->part().params["enableContactEvents"] = false;
        scene.clearPhysicsSelection();
    }

    Rule stop;
    stop.name = QStringLiteral("stop");
    stop.subjectName = QStringLiteral("ball");
    stop.eventId = QStringLiteral("contactBegin");
    stop.conditionValue = QStringLiteral("wall");
    stop.targetName = QStringLiteral("ball");
    stop.propertyKey = QStringLiteral("restitution");
    stop.value = 0.0;
    scene.setRules({ stop });

    const QString code = exported(&scene);
    ASSERT_FALSE(code.isEmpty());

    const qsizetype created = code.indexOf(QStringLiteral("ball = b2CreateCircleShape("));
    ASSERT_GT(created, 0) << code.toStdString();
    const qsizetype defStart = code.lastIndexOf(QStringLiteral("b2ShapeDef shapeDef"), created);
    ASSERT_GE(defStart, 0);
    const QString ballDef = code.mid(defStart, created - defStart);
    EXPECT_TRUE(ballDef.contains(QStringLiteral("shapeDef.enableContactEvents = true;")))
        << "the shape the rule watches reports its contacts -- " << ballDef.toStdString();
    EXPECT_FALSE(code.contains(QStringLiteral("enableContactEvents = false")))
        << "off is already Box2D's default, so it is never written";
}
