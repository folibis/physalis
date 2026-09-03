#include "CanvasScene.h"
#include "OptionsDialog.h"
#include "SceneExporter.h"
#include "SceneFixtures.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QJsonObject>
#include <QLineEdit>
#include <QSpinBox>
#include <QTabWidget>
#include <QToolButton>
#include <QTemporaryDir>
#include <QTextStream>
#include <gtest/gtest.h>

namespace {

// A converter that asks for one of everything, so the whole type list is
// exercised without depending on what the shipped one happens to want.
const char *kEveryType = R"({
    "name": "Every Type",
    "description": "asks for one of each",
    "settings": [
        { "key": "title",  "label": "Title",  "type": "string", "default": "untitled" },
        { "key": "count",  "label": "Count",  "type": "int",    "default": 7, "min": 1, "max": 99 },
        { "key": "ratio",  "label": "Ratio",  "type": "double", "default": 0.5, "decimals": 3 },
        { "key": "loud",   "label": "Loud",   "type": "bool",   "default": true },
        { "key": "tint",   "label": "Tint",   "type": "color",  "default": "#ff112233" },
        { "key": "flavour","label": "Flavour","type": "choice", "default": "salt",
          "choices": ["salt", "pepper"] },
        { "key": "where",  "label": "Where",  "type": "path",   "default": "C:/somewhere" },
        { "key": "future", "label": "Future", "type": "quantum", "default": "as text" }
    ]
})";

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

QString shippedConverters()
{
    QDir dir(QStringLiteral(__FILE__));
    dir.cdUp();
    dir.cdUp();
    return dir.absoluteFilePath(QStringLiteral("exporters"));
}

} // namespace

// What a converter wants asked is declared in its manifest, not in its script:
// the options page has to be built without running anybody's code, the same
// reason the name is there.
TEST(ConverterSettings, AreReadFromTheManifest)
{
    QTemporaryDir root;
    ASSERT_TRUE(root.isValid());
    writeConverter(root.filePath(QStringLiteral("every")), QString::fromUtf8(kEveryType),
                   QStringLiteral("throw new Error('reading a manifest must not run me');"));

    const QVector<SceneExporter::Converter> found = SceneExporter::discover(root.path());
    ASSERT_EQ(found.size(), 1);
    const SceneExporter::Converter &converter = found.first();

    EXPECT_EQ(converter.id, QStringLiteral("every")) << "settings are filed under the folder name";
    ASSERT_EQ(converter.settings.size(), 8);

    EXPECT_EQ(converter.settings[1].type, QStringLiteral("int"));
    EXPECT_EQ(converter.settings[1].defaultValue.toInt(), 7);
    EXPECT_EQ(converter.settings[1].minValue, 1.0);
    EXPECT_EQ(converter.settings[1].maxValue, 99.0);
    EXPECT_EQ(converter.settings[2].decimals, 3);
    EXPECT_EQ(converter.settings[5].choices,
              QStringList({ QStringLiteral("salt"), QStringLiteral("pepper") }));
    EXPECT_EQ(converter.settings[6].type, QStringLiteral("path"));

    const QVariantMap defaults = converter.defaults();
    EXPECT_EQ(defaults.value(QStringLiteral("title")).toString(), QStringLiteral("untitled"));
    EXPECT_TRUE(defaults.value(QStringLiteral("loud")).toBool());
}

// The script is handed its own settings resolved -- what the user chose over
// what the manifest asked for -- rather than being made to find them in the
// application's preferences itself.
TEST(ConverterSettings, ReachTheScriptWithDefaultsApplied)
{
    QTemporaryDir root;
    QTemporaryDir output;
    ASSERT_TRUE(root.isValid() && output.isValid());

    writeConverter(root.filePath(QStringLiteral("every")), QString::fromUtf8(kEveryType),
                   QStringLiteral(
                       "function exportScene(scene, io) {\n"
                       "    var s = scene.converterSettings;\n"
                       "    io.write('out.txt', 'title=' + s.title + ' count=' + s.count\n"
                       "        + ' loud=' + s.loud + ' flavour=' + s.flavour\n"
                       "        + ' future=' + s.future);\n"
                       "}\n"));

    CanvasScene scene;
    Fixtures::buildCart(&scene);

    const QVector<SceneExporter::Converter> found = SceneExporter::discover(root.path());
    ASSERT_EQ(found.size(), 1);

    // Two of the seven chosen; the rest have to arrive at their defaults.
    QJsonObject chosen;
    chosen.insert(QStringLiteral("title"), QStringLiteral("chosen"));
    chosen.insert(QStringLiteral("count"), 42);
    QJsonObject exports;
    exports.insert(QStringLiteral("every"), chosen);
    QJsonObject settings;
    settings.insert(QStringLiteral("Export"), exports);

    QString error;
    ASSERT_TRUE(SceneExporter::run(found.first(), &scene, output.path(), settings, &error))
        << error.toStdString();

    const QString said = contentsOf(output.filePath(QStringLiteral("out.txt")));
    EXPECT_EQ(said, QStringLiteral("title=chosen count=42 loud=true flavour=salt"
                                   " future=as text"))
        << said.toStdString();
}

// The Export tab exists only when there is a converter to configure, and one
// page per converter inside it.
TEST(ConverterSettings, ShowAsATabOnlyWhenThereAreConverters)
{
    QTemporaryDir root;
    ASSERT_TRUE(root.isValid());

    const auto exportTabOf = [](const OptionsDialog &dialog) -> QTabWidget * {
        // The outer tabs are the dialog's own; the Export page holds a second
        // QTabWidget, one page per converter.
        for (QTabWidget *tabs : dialog.findChildren<QTabWidget *>()) {
            for (int i = 0; i < tabs->count(); ++i) {
                if (tabs->tabText(i) == QStringLiteral("Export"))
                    return tabs->widget(i)->findChild<QTabWidget *>();
            }
        }
        return nullptr;
    };

    OptionsDialog::Settings empty;
    empty.converterPath = root.path();
    {
        OptionsDialog dialog(empty);
        EXPECT_EQ(exportTabOf(dialog), nullptr) << "no converters, no tab";
    }

    writeConverter(root.filePath(QStringLiteral("every")), QString::fromUtf8(kEveryType),
                   QStringLiteral("function exportScene() {}"));

    OptionsDialog dialog(empty);
    QTabWidget *pages = exportTabOf(dialog);
    ASSERT_NE(pages, nullptr) << "one converter, one tab";
    ASSERT_EQ(pages->count(), 1);
    EXPECT_EQ(pages->tabText(0), QStringLiteral("Every Type"));

    // Each declared type became the widget it should have. Direct children
    // only: a spin box has a QLineEdit of its own inside it.
    EXPECT_EQ(pages->widget(0)
                  ->findChildren<QLineEdit *>(QString(), Qt::FindDirectChildrenOnly)
                  .size(),
              2)   // the string one, and the type this version has never heard of
        << "an unknown type still gets a usable box";
    EXPECT_EQ(pages->widget(0)->findChildren<QSpinBox *>().size(), 1);
    EXPECT_EQ(pages->widget(0)->findChildren<QCheckBox *>().size(), 1);
    EXPECT_EQ(pages->widget(0)->findChildren<QComboBox *>().size(), 1);
    // A path is a box and a browse button, so its editor is a container: the
    // page's own direct line edits are still just the two above.
    const QList<QToolButton *> buttons = pages->widget(0)->findChildren<QToolButton *>();
    EXPECT_EQ(buttons.size(), 2) << "the colour swatch, and the path's browse button";
}

// What is typed on that tab comes back out, filed under the converter it
// belongs to, ready to be stored.
TEST(ConverterSettings, RoundTripThroughTheDialog)
{
    QTemporaryDir root;
    ASSERT_TRUE(root.isValid());
    writeConverter(root.filePath(QStringLiteral("every")), QString::fromUtf8(kEveryType),
                   QStringLiteral("function exportScene() {}"));

    OptionsDialog::Settings before;
    before.converterPath = root.path();
    OptionsDialog dialog(before);

    // Nothing was ever chosen, so what comes back is what the manifest asked
    // for -- which is what makes the page show the defaults in the first place.
    const OptionsDialog::Settings after = dialog.settings();
    const QVariantMap values = after.converterSettings.value(QStringLiteral("every"));
    EXPECT_EQ(values.value(QStringLiteral("title")).toString(), QStringLiteral("untitled"));
    EXPECT_EQ(values.value(QStringLiteral("count")).toInt(), 7);
    EXPECT_TRUE(values.value(QStringLiteral("loud")).toBool());
    EXPECT_EQ(values.value(QStringLiteral("flavour")).toString(), QStringLiteral("salt"));
    EXPECT_EQ(values.value(QStringLiteral("tint")).toString(), QStringLiteral("#ff112233"))
        << "a colour comes back as Qt writes one";
    EXPECT_EQ(values.value(QStringLiteral("where")).toString(), QStringLiteral("C:/somewhere"))
        << "and a path comes out of the box beside its button";
}

// The shipped converter's own settings, on a real scene: the ones that change
// the generated project have to actually change it.
TEST(ConverterSettings, QtProjectHonoursItsOwn)
{
    const QVector<SceneExporter::Converter> found =
        SceneExporter::discover(shippedConverters());
    ASSERT_FALSE(found.isEmpty());
    ASSERT_FALSE(found.first().settings.isEmpty());

    CanvasScene scene;
    Fixtures::buildCart(&scene);

    QJsonObject chosen;
    chosen.insert(QStringLiteral("projectName"), QStringLiteral("MyCart"));
    chosen.insert(QStringLiteral("qtPath"), QStringLiteral("C:/Qt/6.7.0/mingw_64"));
    chosen.insert(QStringLiteral("stepsPerSecond"), 120);
    chosen.insert(QStringLiteral("addControls"), true);
    chosen.insert(QStringLiteral("cxxStandard"), QStringLiteral("20"));
    chosen.insert(QStringLiteral("debugView"), true);
    chosen.insert(QStringLiteral("box2dRepository"),
                  QStringLiteral("git@internal:mirrors/box2d.git"));
    chosen.insert(QStringLiteral("box2dRef"), QStringLiteral("our-v3-branch"));
    QJsonObject exports;
    exports.insert(found.first().id, chosen);

    // The axes are drawn the way the editor draws them, which makes their
    // appearance a preference rather than part of the scene.
    QJsonObject physics;
    physics.insert(QStringLiteral("bodyAxisLength"), 55.0);
    physics.insert(QStringLiteral("bodyAxisXColor"), QStringLiteral("#ff010203"));

    QJsonObject settings;
    settings.insert(QStringLiteral("Export"), exports);
    settings.insert(QStringLiteral("Physics"), physics);

    QTemporaryDir output;
    ASSERT_TRUE(output.isValid());
    QString error;
    ASSERT_TRUE(SceneExporter::run(found.first(), &scene, output.path(), settings, &error))
        << error.toStdString();

    const QString cmake = contentsOf(output.filePath(QStringLiteral("CMakeLists.txt")));
    EXPECT_TRUE(cmake.contains(QStringLiteral("project(MyCart"))) << cmake.toStdString();
    EXPECT_TRUE(cmake.contains(QStringLiteral("CMAKE_PREFIX_PATH \"C:/Qt/6.7.0/mingw_64\"")))
        << "a Qt path given means the project configures without being told one";
    EXPECT_TRUE(cmake.contains(QStringLiteral("CMAKE_CXX_STANDARD 20")));
    EXPECT_TRUE(cmake.contains(QStringLiteral("GIT_REPOSITORY git@internal:mirrors/box2d.git")))
        << "a fork or a mirror is where it is told to look";
    EXPECT_TRUE(cmake.contains(QStringLiteral("GIT_TAG our-v3-branch")));
    EXPECT_FALSE(cmake.contains(QStringLiteral("{{"))) << "every placeholder was filled in";

    const QString generated = contentsOf(output.filePath(QStringLiteral("Scene.cpp")));
    EXPECT_TRUE(generated.contains(QStringLiteral("scene.stepsPerSecond = 120")));
    EXPECT_TRUE(generated.contains(QStringLiteral("scene.showControls = true")));
    EXPECT_TRUE(generated.contains(QStringLiteral("scene.debugView = true")));
    EXPECT_TRUE(generated.contains(QStringLiteral("scene.axisLength = 55.0")))
        << "and the axes take their look from the editor's own settings";
    EXPECT_TRUE(generated.contains(QStringLiteral("scene.axisXColor = QColor(\"#ff010203\")")));
}
