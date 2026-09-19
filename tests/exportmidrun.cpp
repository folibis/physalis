#include "CanvasScene.h"
#include "PhysicsBody.h"
#include "RectangleItem.h"
#include "SceneExporter.h"
#include "SimulationController.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTextStream>
#include <gtest/gtest.h>

// An export made while the scene is playing is an export of the scene, as it
// stood when the run started -- not of wherever the run has got to.

namespace {

void writeFile(const QString &path, const QString &contents)
{
    QFile file(path);
    ASSERT_TRUE(file.open(QIODevice::WriteOnly | QIODevice::Text));
    QTextStream out(&file);
    out.setEncoding(QStringConverter::Utf8);
    out << contents;
}

} // namespace

TEST(ExportMidRun, ExportsTheSceneNotTheRun)
{
    CanvasScene scene;
    scene.setSimulationEngineName(QStringLiteral("Box2D"));
    ShapeItem *box = scene.addRectangle(QPointF(0, 0));
    scene.notifyShapesChanged();
    scene.setEditorMode(EditorMode::Physics);
    scene.selectForPhysics(box, true);
    PhysicsBody *body = scene.createBodyFromSelection();
    ASSERT_TRUE(body);
    body->props().type = physics::BodyType::Dynamic;
    scene.clearPhysicsSelection();
    const QPointF drawnAt = box->pos();
    const QPointF bodyDrawnAt = body->originScenePos();

    // A converter that writes down where it was told the body is.
    QTemporaryDir root;
    ASSERT_TRUE(root.isValid());
    const QString folder = root.filePath(QStringLiteral("where"));
    QDir().mkpath(folder);
    writeFile(QDir(folder).filePath(QStringLiteral("manifest.json")),
              QStringLiteral(R"({"name": "Where", "description": "where the body is"})"));
    writeFile(QDir(folder).filePath(QStringLiteral("export.js")),
              QStringLiteral("function exportScene(scene, io) {\n"
                             "    var b = scene.simulation.bodies[0];\n"
                             "    io.write('where.json', JSON.stringify({ x: b.position.x, y: b.position.y,\n"
                             "        bodies: scene.simulation.bodies.length }));\n"
                             "}\n"));
    const QVector<SceneExporter::Converter> found = SceneExporter::discover(root.path());
    ASSERT_EQ(found.size(), 1);

    SimulationController sim(&scene, nullptr);
    sim.setEngineName(QStringLiteral("Box2D"));
    sim.start();
    for (int i = 0; i < 60; ++i)
        sim.stepFrame();
    const QPointF fallenTo = box->pos();
    ASSERT_GT(fallenTo.y(), drawnAt.y() + 1.0) << "the box did not fall, so this tests nothing";

    QTemporaryDir output;
    ASSERT_TRUE(output.isValid());
    bool converted = false;
    QString error;
    QPointF exportedFrom;
    sim.withSceneAsStarted([&] {
        exportedFrom = body->originScenePos();
        converted = SceneExporter::run(found.first(), &scene, output.path(), QJsonObject(), &error);
    });
    ASSERT_TRUE(converted) << error.toStdString();

    QFile written(output.filePath(QStringLiteral("where.json")));
    ASSERT_TRUE(written.open(QIODevice::ReadOnly));
    const QJsonObject where = QJsonDocument::fromJson(written.readAll()).object();
    EXPECT_NEAR(where.value("y").toDouble(), exportedFrom.y(), 0.01);
    EXPECT_NEAR(exportedFrom.x(), bodyDrawnAt.x(), 0.01);
    EXPECT_NEAR(exportedFrom.y(), bodyDrawnAt.y(), 0.01) << "the export saw the box where the run had it";
    EXPECT_EQ(where.value("bodies").toInt(), 1);

    // And the run carries on from where it was.
    EXPECT_EQ(box->pos(), fallenTo);
    EXPECT_NE(body->originScenePos(), exportedFrom);
    EXPECT_TRUE(sim.isActive());

    sim.stop();
    EXPECT_EQ(box->pos(), drawnAt) << "stop still puts the scene back";
}
