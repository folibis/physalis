// The editor draws a sensor open and hatched, and both exports have to draw it
// the same way -- in the colour and pattern the settings chose.

#include "CanvasScene.h"
#include "PhysicsBody.h"
#include "RectangleItem.h"
#include "SceneExporter.h"

#include <QDir>
#include <QFile>
#include <QJsonObject>
#include <QTemporaryDir>
#include <gtest/gtest.h>

namespace {

QString exported(CanvasScene *scene, const QString &converterName, const QString &file,
                 const QJsonObject &settings)
{
    QDir dir(QStringLiteral(__FILE__));
    dir.cdUp();
    dir.cdUp();
    const QVector<SceneExporter::Converter> found =
        SceneExporter::discover(dir.absoluteFilePath(QStringLiteral("exporters")));
    for (const SceneExporter::Converter &converter : found) {
        if (!converter.name.contains(converterName))
            continue;
        QTemporaryDir output;
        QString error;
        if (!output.isValid()
            || !SceneExporter::run(converter, scene, output.path(), settings, &error))
            return error;
        QFile generated(output.filePath(file));
        if (!generated.open(QIODevice::ReadOnly | QIODevice::Text))
            return {};
        return QString::fromUtf8(generated.readAll());
    }
    return {};
}

void buildSensor(CanvasScene *scene)
{
    scene->setSimulationEngineName(QStringLiteral("Box2D"));
    auto *gate = new RectangleItem;
    gate->setRect(QRectF(0, 0, 40, 200));
    gate->setName(QStringLiteral("gate"));
    scene->addItem(gate);
    scene->notifyShapesChanged();
    scene->setEditorMode(EditorMode::Physics);
    scene->selectForPhysics(gate, true);
    PhysicsBody *body = scene->createBodyFromSelection();
    body->props().type = physics::BodyType::Static;
    gate->part().params["isSensor"] = true;
    scene->clearPhysicsSelection();
}

QJsonObject hatchedGreen()
{
    QJsonObject physics;
    physics.insert(QStringLiteral("sensorColor"), QStringLiteral("#ff05c936"));
    physics.insert(QStringLiteral("sensorPattern"), 12);   // Qt::BDiagPattern
    physics.insert(QStringLiteral("sensorFillsBody"), false);
    QJsonObject settings;
    settings.insert(QStringLiteral("Physics"), physics);
    return settings;
}

} // namespace

TEST(ExportSensors, TheQtProjectHatchesThem)
{
    CanvasScene scene;
    buildSensor(&scene);
    const QString code = exported(&scene, QStringLiteral("Qt project"), QStringLiteral("main.cpp"),
                                  hatchedGreen());
    ASSERT_FALSE(code.isEmpty());

    EXPECT_TRUE(code.contains(QStringLiteral("customColor = COLOR_STATIC | SENSOR;")))
        << "the sensor's shape is marked for the draw callbacks -- " << code.toStdString();
    EXPECT_TRUE(code.contains(QStringLiteral("const QColor SENSOR_COLOR(5, 201, 54, 255);")));
    EXPECT_TRUE(code.contains(QStringLiteral("SENSOR_PATTERN = Qt::BDiagPattern;")));
    EXPECT_TRUE(code.contains(QStringLiteral("const bool SENSOR_FILLED = false;")));
    EXPECT_TRUE(code.contains(QStringLiteral("hatch(painter, path);")));
}

TEST(ExportSensors, ThePlanckPageHatchesThem)
{
    CanvasScene scene;
    buildSensor(&scene);
    const QString page = exported(&scene, QStringLiteral("Planck"), QStringLiteral("index.html"),
                                  hatchedGreen());
    ASSERT_FALSE(page.isEmpty());

    EXPECT_TRUE(page.contains(QStringLiteral("var style = 12;")));
    EXPECT_TRUE(page.contains(QStringLiteral("pen.fillStyle = \"#05c936\";")));
    EXPECT_TRUE(page.contains(QStringLiteral("var SENSOR_FILLED = false;")));
    EXPECT_TRUE(page.contains(QStringLiteral("var sensor = fixture.isSensor();")));
    EXPECT_TRUE(page.contains(QStringLiteral("ctx.fillStyle = SENSOR_HATCH;")));
}
