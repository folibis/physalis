#include "CanvasScene.h"
#include "PhysicsBody.h"
#include "PolygonItem.h"
#include "RectangleItem.h"
#include "SceneExporter.h"
#include "SceneFixtures.h"

#include <QApplication>
#include <QDir>
#include <QJsonObject>
#include <QFile>
#include <QTemporaryDir>
#include <QTextStream>
#include <gtest/gtest.h>

namespace {

// A converter written into a temporary folder, so a test never depends on one
// being installed anywhere.
void writeConverter(const QString &folder, const QString &manifest, const QString &script)
{
    QDir().mkpath(folder);
    const auto put = [&folder](const QString &name, const QString &contents) {
        QFile file(QDir(folder).absoluteFilePath(name));
        ASSERT_TRUE(file.open(QIODevice::WriteOnly | QIODevice::Text));
        QTextStream out(&file);
        out.setEncoding(QStringConverter::Utf8);
        out << contents;
    };
    put(QStringLiteral("manifest.json"), manifest);
    put(QStringLiteral("export.js"), script);
}

QString contentsOf(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    return QString::fromUtf8(file.readAll());
}

// Where the converters that ship with the project live, found from this
// file's own location rather than from wherever the tests happen to run.
QString shippedConverters()
{
    QDir dir(QStringLiteral(__FILE__));
    dir.cdUp();             // tests/
    dir.cdUp();             // the project root
    return dir.absoluteFilePath(QStringLiteral("exporters"));
}

// Most of these do not care what the preferences say; a converter has to cope
// with settings it was not given, since the file may not exist yet.
const QJsonObject kNoSettings;

} // namespace

// Discovery reads manifests and nothing else. Opening the File menu must not
// run anybody's code, so a folder is described by a file that is parsed, not
// by a script that is executed.
TEST(ExportScene, FindsConvertersWithoutRunningThem)
{
    QTemporaryDir root;
    ASSERT_TRUE(root.isValid());

    writeConverter(root.filePath(QStringLiteral("good")),
                   QStringLiteral(R"({"name": "Good One", "description": "does things"})"),
                   QStringLiteral("throw new Error('discovery must not run me');"));

    // No manifest: not a converter, and not an error either.
    QDir().mkpath(root.filePath(QStringLiteral("notes")));
    QFile readme(root.filePath(QStringLiteral("notes/README.txt")));
    ASSERT_TRUE(readme.open(QIODevice::WriteOnly));
    readme.write("just a folder");
    readme.close();

    const QVector<SceneExporter::Converter> found = SceneExporter::discover(root.path());
    ASSERT_EQ(found.size(), 1);
    EXPECT_EQ(found.first().name, QStringLiteral("Good One"));
    EXPECT_EQ(found.first().description, QStringLiteral("does things"));
}

// The whole scene arrives in one argument, and what the script writes is what
// lands on disk.
TEST(ExportScene, HandsOverTheSceneAndWritesWhatComesBack)
{
    QTemporaryDir root;
    QTemporaryDir output;
    ASSERT_TRUE(root.isValid() && output.isValid());

    writeConverter(
        root.filePath(QStringLiteral("counter")),
        QStringLiteral(R"({"name": "Counter"})"),
        QStringLiteral(
            "function exportScene(scene, io) {\n"
            "    io.write('summary.txt',\n"
            "        'bodies=' + scene.simulation.bodies.length +\n"
            "        ' joints=' + scene.simulation.joints.length +\n"
            "        ' rules=' + scene.rules.length +\n"
            "        ' engine=' + scene.engine.name +\n"
            "        ' first=' + scene.simulation.bodies[0].name);\n"
            "}\n"));

    CanvasScene scene;
    Fixtures::buildCart(&scene);

    const QVector<SceneExporter::Converter> found = SceneExporter::discover(root.path());
    ASSERT_EQ(found.size(), 1);

    QString error;
    QStringList written;
    ASSERT_TRUE(SceneExporter::run(found.first(), &scene, output.path(), kNoSettings, &error, &written))
        << error.toStdString();
    EXPECT_EQ(written, QStringList { QStringLiteral("summary.txt") });

    const QString summary = contentsOf(output.filePath(QStringLiteral("summary.txt")));
    EXPECT_TRUE(summary.contains(QStringLiteral("bodies=5"))) << summary.toStdString();
    EXPECT_TRUE(summary.contains(QStringLiteral("joints=2"))) << summary.toStdString();
    EXPECT_TRUE(summary.contains(QStringLiteral("rules=2"))) << summary.toStdString();
    EXPECT_TRUE(summary.contains(QStringLiteral("engine=Box2D"))) << summary.toStdString();
}

// A converter can read what it shipped beside itself, and can reach nothing
// else. Templates are the whole reason a converter is a folder.
TEST(ExportScene, ReadsItsOwnFilesAndNothingElse)
{
    QTemporaryDir root;
    QTemporaryDir output;
    ASSERT_TRUE(root.isValid() && output.isValid());

    const QString folder = root.filePath(QStringLiteral("templated"));
    writeConverter(folder, QStringLiteral(R"({"name": "Templated"})"),
                   QStringLiteral(
                       "function exportScene(scene, io) {\n"
                       "    io.write('out.txt', io.read('templates/greeting.tmpl')\n"
                       "        .replace('{{WHAT}}', scene.format));\n"
                       "}\n"));
    QDir().mkpath(folder + QStringLiteral("/templates"));
    QFile tmpl(folder + QStringLiteral("/templates/greeting.tmpl"));
    ASSERT_TRUE(tmpl.open(QIODevice::WriteOnly));
    tmpl.write("this is {{WHAT}}");
    tmpl.close();

    CanvasScene scene;
    Fixtures::buildCart(&scene);

    const QVector<SceneExporter::Converter> found = SceneExporter::discover(root.path());
    ASSERT_EQ(found.size(), 1);

    QString error;
    ASSERT_TRUE(SceneExporter::run(found.first(), &scene, output.path(), kNoSettings, &error))
        << error.toStdString();
    EXPECT_EQ(contentsOf(output.filePath(QStringLiteral("out.txt"))),
              QStringLiteral("this is shape-editor-scene"));
}

// Reading and writing are each jailed to one folder. A converter is somebody
// else's code running on the user's machine, and the two doors it is given are
// the only two it has.
TEST(ExportScene, CannotEscapeItsFolders)
{
    QTemporaryDir root;
    QTemporaryDir output;
    ASSERT_TRUE(root.isValid() && output.isValid());

    writeConverter(root.filePath(QStringLiteral("nosy")),
                   QStringLiteral(R"({"name": "Nosy"})"),
                   QStringLiteral(
                       "function exportScene(scene, io) {\n"
                       "    io.write('../escaped.txt', 'should not exist');\n"
                       "}\n"));

    CanvasScene scene;
    Fixtures::buildCart(&scene);

    const QVector<SceneExporter::Converter> found = SceneExporter::discover(root.path());
    ASSERT_EQ(found.size(), 1);

    QString error;
    EXPECT_FALSE(SceneExporter::run(found.first(), &scene, output.path(), kNoSettings, &error));
    EXPECT_TRUE(error.contains(QStringLiteral("outside"))) << error.toStdString();

    QDir beside(output.path());
    beside.cdUp();
    EXPECT_FALSE(beside.exists(QStringLiteral("escaped.txt")));
}

// A script that throws is reported, with the line it threw on, rather than
// failing quietly or taking the application down.
TEST(ExportScene, ReportsAScriptThatThrows)
{
    QTemporaryDir root;
    QTemporaryDir output;
    ASSERT_TRUE(root.isValid() && output.isValid());

    writeConverter(root.filePath(QStringLiteral("broken")),
                   QStringLiteral(R"({"name": "Broken"})"),
                   QStringLiteral("function exportScene(scene, io) {\n"
                                  "    throw new Error('no thanks');\n"
                                  "}\n"));

    CanvasScene scene;
    Fixtures::buildCart(&scene);

    const QVector<SceneExporter::Converter> found = SceneExporter::discover(root.path());
    ASSERT_EQ(found.size(), 1);

    QString error;
    EXPECT_FALSE(SceneExporter::run(found.first(), &scene, output.path(), kNoSettings, &error));
    EXPECT_TRUE(error.contains(QStringLiteral("no thanks"))) << error.toStdString();
}

// An outline's points are mapped into the body's frame by the editor, and its
// `center` is left as wherever the shape's pivot landed -- the engine ignores
// it and builds the shape from the points alone. A converter that draws the
// points *and* offsets them by the centre puts every polygon in the wrong
// place, by exactly that centre, which is what happened.
TEST(ExportScene, OutlinesAreNotOffsetTwice)
{
    CanvasScene scene;
    scene.setSimulationEngineName(QStringLiteral("Box2D"));

    // A plate and a triangle in one body, the triangle well away from the
    // plate so that its centre in body coordinates cannot be zero.
    auto *plate = new RectangleItem;
    plate->setRect(QRectF(0, 0, 200, 20));
    plate->setPos(0, 0);
    plate->setName(QStringLiteral("plate"));

    QPolygonF triangle;
    triangle << QPointF(300, -200) << QPointF(400, -200) << QPointF(300, -120);
    auto *wedge = new PolygonItem(triangle, true);
    wedge->setName(QStringLiteral("wedge"));

    scene.addItem(plate);
    scene.addItem(wedge);
    scene.notifyShapesChanged();
    scene.setEditorMode(EditorMode::Physics);
    scene.selectForPhysics(plate, true);
    scene.selectForPhysics(wedge, true);
    PhysicsBody *body = scene.createBodyFromSelection();
    ASSERT_NE(body, nullptr);
    scene.clearPhysicsSelection();

    // What the engine is given: points in body coordinates, and a centre that
    // is not the origin.
    const physics::BodyDesc desc = body->toBodyDesc();
    const physics::ShapePart *outline = nullptr;
    for (const physics::ShapePart &part : desc.parts) {
        if (part.geometry.kind == physics::GeometryKind::Polygon)
            outline = &part;
    }
    ASSERT_NE(outline, nullptr);
    ASSERT_FALSE(qFuzzyIsNull(outline->geometry.center.x())
                 && qFuzzyIsNull(outline->geometry.center.y()))
        << "the fixture is only meaningful while the outline's centre is offset";

    const QVector<SceneExporter::Converter> found =
        SceneExporter::discover(shippedConverters());
    ASSERT_FALSE(found.isEmpty());

    QTemporaryDir output;
    ASSERT_TRUE(output.isValid());
    QString error;
    ASSERT_TRUE(SceneExporter::run(found.first(), &scene, output.path(), kNoSettings, &error))
        << error.toStdString();

    const QString generated = contentsOf(output.filePath(QStringLiteral("Scene.cpp")));
    const int at = generated.indexOf(QStringLiteral("SceneShape::Polygon"));
    ASSERT_GT(at, 0);
    const QString drawable = generated.mid(at, 400);

    EXPECT_TRUE(drawable.contains(QStringLiteral("drawn.center = QPointF(0.0, 0.0)")))
        << "the outline is drawn where its points say, and nowhere else";
    // The first point, as the engine has it, has to appear untouched.
    const QPointF first = outline->geometry.points.first();
    EXPECT_TRUE(drawable.contains(QStringLiteral("QPointF(%1, %2)")
                                      .arg(first.x(), 0, 'f', 1)
                                      .arg(first.y(), 0, 'f', 1)))
        << drawable.toStdString();
}

// What a converter says through io.log() has to come back out, or the message
// shown when it finishes can only count files. It comes back on the way out of
// a failure too: a converter that logs its way to an error has already said
// the most useful part.
TEST(ExportScene, ReportsWhatTheConverterSaid)
{
    QTemporaryDir root;
    QTemporaryDir output;
    ASSERT_TRUE(root.isValid() && output.isValid());

    writeConverter(root.filePath(QStringLiteral("chatty")),
                   QStringLiteral(R"({"name": "Chatty"})"),
                   QStringLiteral(
                       "function exportScene(scene, io) {\n"
                       "    io.log('looked at ' + scene.simulation.bodies.length\n"
                       "        + ' bodies');\n"
                       "    io.write('out.txt', 'done');\n"
                       "    io.log('and wrote one file');\n"
                       "}\n"));

    CanvasScene scene;
    Fixtures::buildCart(&scene);

    const QVector<SceneExporter::Converter> found = SceneExporter::discover(root.path());
    ASSERT_EQ(found.size(), 1);

    QString error;
    QStringList written;
    QStringList log;
    ASSERT_TRUE(SceneExporter::run(found.first(), &scene, output.path(), kNoSettings,
                                   &error, &written, &log))
        << error.toStdString();
    EXPECT_EQ(log, QStringList({ QStringLiteral("looked at 5 bodies"),
                                 QStringLiteral("and wrote one file") }));
}

// A converter that writes nothing has not converted anything, whatever it
// thinks -- the message has to say so rather than claiming success.
TEST(ExportScene, WritingNothingIsAFailure)
{
    QTemporaryDir root;
    QTemporaryDir output;
    ASSERT_TRUE(root.isValid() && output.isValid());

    writeConverter(root.filePath(QStringLiteral("idle")),
                   QStringLiteral(R"({"name": "Idle"})"),
                   QStringLiteral(
                       "function exportScene(scene, io) {\n"
                       "    io.log('thought about it');\n"
                       "}\n"));

    CanvasScene scene;
    Fixtures::buildCart(&scene);

    const QVector<SceneExporter::Converter> found = SceneExporter::discover(root.path());
    ASSERT_EQ(found.size(), 1);

    QString error;
    QStringList log;
    EXPECT_FALSE(SceneExporter::run(found.first(), &scene, output.path(), kNoSettings,
                                    &error, nullptr, &log));
    EXPECT_TRUE(error.contains(QStringLiteral("wrote no files"))) << error.toStdString();
    EXPECT_EQ(log, QStringList { QStringLiteral("thought about it") })
        << "and what it said on the way is still worth showing";
}

// The scene does not say what colour a dynamic body is -- that is one of the
// application's preferences -- so a converter that wants to paint a scene the
// way the editor paints it has to be handed them.
TEST(ExportScene, SettingsReachTheConverter)
{
    CanvasScene scene;
    Fixtures::buildCart(&scene);

    QJsonObject physics;
    physics.insert(QStringLiteral("bodyDynamicColor"), QStringLiteral("#ff0000ff"));
    physics.insert(QStringLiteral("bodyStaticColor"), QStringLiteral("#ff00ff00"));
    physics.insert(QStringLiteral("fillAlpha"), 90);
    QJsonObject settings;
    settings.insert(QStringLiteral("Physics"), physics);

    const QVector<SceneExporter::Converter> found =
        SceneExporter::discover(shippedConverters());
    ASSERT_FALSE(found.isEmpty());

    QTemporaryDir output;
    ASSERT_TRUE(output.isValid());
    QString error;
    ASSERT_TRUE(SceneExporter::run(found.first(), &scene, output.path(), settings, &error))
        << error.toStdString();

    const QString generated = contentsOf(output.filePath(QStringLiteral("Scene.cpp")));
    EXPECT_TRUE(generated.contains(QStringLiteral("QColor(\"#ff0000ff\")")))
        << "the cart's dynamic bodies are outlined in the colour the settings gave";
    EXPECT_TRUE(generated.contains(QStringLiteral("QColor(\"#ff00ff00\")")))
        << "and its ground and wall in the static one";
    // 90 is 0x5a, and Qt writes a colour as #aarrggbb.
    EXPECT_TRUE(generated.contains(QStringLiteral("QColor(\"#5a0000ff\")")))
        << "filled at the transparency the settings asked for";
}

// Solid field bounds are not bodies in the document -- the editor makes four
// static walls when a run starts. A converter that only walks the document's
// bodies exports an open world and lets everything fall out of it.
TEST(ExportScene, CarriesSolidFieldBounds)
{
    const QVector<SceneExporter::Converter> found =
        SceneExporter::discover(shippedConverters());
    ASSERT_FALSE(found.isEmpty());

    const auto exportWith = [&found](bool solid) {
        CanvasScene scene;
        Fixtures::buildCart(&scene);
        scene.setFieldBoundsSolid(solid);

        QTemporaryDir output;
        EXPECT_TRUE(output.isValid());
        QString error;
        EXPECT_TRUE(SceneExporter::run(found.first(), &scene, output.path(),
                                       kNoSettings, &error))
            << error.toStdString();
        return contentsOf(output.filePath(QStringLiteral("Scene.cpp")));
    };

    EXPECT_FALSE(exportWith(false).contains(QStringLiteral("field bounds")))
        << "a scene without them gets no walls";

    const QString walled = exportWith(true);
    EXPECT_TRUE(walled.contains(QStringLiteral("field bounds")));
    EXPECT_EQ(walled.count(QStringLiteral("field_bounds")), 4)
        << "four walls, one on each side";
}

// The converter that ships with the project, on a real scene. It is not built
// here -- that would want Qt, a compiler and the network -- but every file it
// promises has to arrive, and the generated one has to carry the scene.
TEST(ExportScene, QtProjectConverterProducesAProject)
{
    const QVector<SceneExporter::Converter> found =
        SceneExporter::discover(shippedConverters());
    ASSERT_FALSE(found.isEmpty()) << "no converters in " << shippedConverters().toStdString();

    const SceneExporter::Converter *qtProject = nullptr;
    for (const SceneExporter::Converter &converter : found) {
        if (converter.name.contains(QStringLiteral("Qt project")))
            qtProject = &converter;
    }
    ASSERT_NE(qtProject, nullptr);

    CanvasScene scene;
    Fixtures::buildCart(&scene);

    QTemporaryDir output;
    ASSERT_TRUE(output.isValid());

    QString error;
    QStringList written;
    ASSERT_TRUE(SceneExporter::run(*qtProject, &scene, output.path(), kNoSettings, &error, &written))
        << error.toStdString();

    const QStringList expected { QStringLiteral("CMakeLists.txt"), QStringLiteral("main.cpp"),
                                 QStringLiteral("Scene.h"), QStringLiteral("Scene.cpp"),
                                 QStringLiteral("Viewport.h"), QStringLiteral("Viewport.cpp"),
                                 QStringLiteral("Rules.h"), QStringLiteral("Rules.cpp") };
    for (const QString &name : expected)
        EXPECT_TRUE(written.contains(name)) << name.toStdString() << " was not written";

    const QString generated = contentsOf(output.filePath(QStringLiteral("Scene.cpp")));
    EXPECT_TRUE(generated.contains(QStringLiteral("b2CreateWorld")));
    // The cart has a ground, a wall, a chassis and two wheels, joined by two
    // wheel joints -- so the shapes, the joints and the rules all have to show.
    EXPECT_TRUE(generated.contains(QStringLiteral("b2CreateCircleShape")));
    EXPECT_TRUE(generated.contains(QStringLiteral("b2CreatePolygonShape")));
    EXPECT_TRUE(generated.contains(QStringLiteral("b2CreateWheelJoint")));
    EXPECT_TRUE(generated.contains(QStringLiteral("chassis")));
    // The cart's two rules are carried as data for the runtime to read, not
    // as generated branches.
    EXPECT_TRUE(generated.contains(QStringLiteral("SceneRule rule;")));
    EXPECT_TRUE(generated.contains(QStringLiteral("scene.rules.push_back(rule);")));
    EXPECT_TRUE(generated.contains(QStringLiteral("rule.event = \"contactBegin\"")))
        << "the contact rule kept its trigger";
    EXPECT_TRUE(generated.contains(QStringLiteral("scene.shapesByName.insert")))
        << "shapes are reachable by name, or no rule could name one";

    const QString cmake = contentsOf(output.filePath(QStringLiteral("CMakeLists.txt")));
    EXPECT_TRUE(cmake.contains(QStringLiteral("FetchContent_Declare(box2d")));
    EXPECT_FALSE(cmake.contains(QStringLiteral("{{"))) << "every placeholder was filled in";
}
