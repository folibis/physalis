#include "CanvasScene.h"
#include "EngineRegistry.h"
#include "IPhysicsEngine.h"
#include "SceneExporter.h"
#include "SceneFixtures.h"

#include <QDir>
#include <QFile>
#include <QJsonObject>
#include <QTemporaryDir>
#include <gtest/gtest.h>

// How close two shapes come before Box2D makes a contact is the world's Contact
// Margin, in scene units. The engine publishes it, and every export sets Box2D
// up from it the same way the engine does, or an export jams where the editor
// does not.

namespace {

QString exported(CanvasScene *scene, const QString &converterId, const QString &file)
{
    QDir dir(QStringLiteral(__FILE__));
    dir.cdUp();
    dir.cdUp();
    for (const SceneExporter::Converter &converter :
         SceneExporter::discover(dir.absoluteFilePath(QStringLiteral("exporters")))) {
        if (converter.id != converterId)
            continue;
        QTemporaryDir output;
        QString error;
        if (!output.isValid() || !SceneExporter::run(converter, scene, output.path(), QJsonObject(), &error))
            return error;
        QFile generated(output.filePath(file));
        if (!generated.open(QIODevice::ReadOnly | QIODevice::Text))
            return {};
        return QString::fromUtf8(generated.readAll());
    }
    return {};
}

} // namespace

TEST(ContactMargin, TheEnginePublishesIt)
{
    auto engine = physics::EngineRegistry::create(QStringLiteral("Box2D"));
    ASSERT_TRUE(engine);
    bool found = false;
    for (const physics::JointParam &property : engine->worldProperties()) {
        if (property.key != QLatin1String("contactMargin"))
            continue;
        found = true;
        EXPECT_TRUE(property.stored) << "a scene keeps its own margin";
        EXPECT_DOUBLE_EQ(property.defaultValue.toDouble(), 2.0);
    }
    EXPECT_TRUE(found) << "Contact Margin is not among the world's properties";
}

TEST(ContactMargin, EveryExportIsSetUpFromIt)
{
    CanvasScene scene;
    Fixtures::buildCart(&scene);

    // 2 px at 1000 px per metre: a length unit of 0.1, so a slop of 0.0005 m.
    EXPECT_TRUE(exported(&scene, QStringLiteral("box2d-qt-project"), QStringLiteral("main.cpp"))
                    .contains(QStringLiteral("b2SetLengthUnitsPerMeter(0.1f);")));
    EXPECT_TRUE(exported(&scene, QStringLiteral("planck-js"), QStringLiteral("index.html"))
                    .contains(QStringLiteral("pl.Settings.linearSlop = 0.005 * 0.1;")));
    EXPECT_TRUE(exported(&scene, QStringLiteral("qml-box2d"), QStringLiteral("scale-box2d.cmake"))
                    .contains(QStringLiteral("b2_linearSlop;0.0005")));

    scene.world().params.insert(QStringLiteral("contactMargin"), 4.0);
    EXPECT_TRUE(exported(&scene, QStringLiteral("box2d-qt-project"), QStringLiteral("main.cpp"))
                    .contains(QStringLiteral("b2SetLengthUnitsPerMeter(0.2f);")));
    EXPECT_TRUE(exported(&scene, QStringLiteral("planck-js"), QStringLiteral("index.html"))
                    .contains(QStringLiteral("pl.Settings.linearSlop = 0.005 * 0.2;")));
    EXPECT_TRUE(exported(&scene, QStringLiteral("qml-box2d"), QStringLiteral("scale-box2d.cmake"))
                    .contains(QStringLiteral("b2_linearSlop;0.001")));
}
