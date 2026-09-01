#include "EngineRegistry.h"
#include "IPhysicsEngine.h"
#include "PluginApi.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLibrary>
#include <cstdio>

#include <gtest/gtest.h>

TEST(Plugins, Behaves)
{
    const QStringList names = physics::EngineRegistry::availableEngines();
    const QStringList paths = physics::EngineRegistry::pluginPaths();
    EXPECT_TRUE(!names.isEmpty()) << "at least one engine";
    EXPECT_TRUE(names.contains(QStringLiteral("Box2D"))) << "Box2D is among them";

    const QString exeDir = QCoreApplication::applicationDirPath();
    bool besideExe = !paths.isEmpty();
    for (const QString &p : paths)
        besideExe = besideExe && QFileInfo(p).absolutePath() == exeDir;
    EXPECT_TRUE(besideExe) << "each came from the executable's folder" << " -- " << (paths.isEmpty() ? QStringLiteral("none")
                          : QFileInfo(paths.first()).absolutePath()).toStdString();

    EXPECT_TRUE(!paths.isEmpty() && QLibrary::isLibrary(paths.first())) << "the file is a shared library" << " -- " << (paths.isEmpty() ? QString() : QFileInfo(paths.first()).fileName()).toStdString();

    auto engine = physics::EngineRegistry::create(QStringLiteral("Box2D"));
    EXPECT_TRUE(engine != nullptr) << "the plugin hands one over";
    if (engine) {
        EXPECT_TRUE(engine->name() == QLatin1String("Box2D")) << "it names itself" << " -- " << (engine->name()).toStdString();
        EXPECT_TRUE(!engine->jointTypes().isEmpty()) << "and declares its joint types" << " -- " << (QStringLiteral("%1 types").arg(engine->jointTypes().size())).toStdString();
        physics::WorldDesc world;
        engine->createWorld(world);
        engine->step(1.0 / 60.0);
        engine->destroyWorld();
        EXPECT_TRUE(true) << "a world can be built and stepped through it";
    }
    EXPECT_TRUE(physics::EngineRegistry::create(QStringLiteral("NoSuchEngine")) == nullptr) << "an unknown name returns nothing";

    // Qt's own DLLs sit in the same folder and must never be taken for engines.
    const QDir dir(exeDir);
    bool onlySigned = true;
    for (const QString &f : dir.entryList({ QStringLiteral("Qt6*.dll") }, QDir::Files))
        onlySigned = onlySigned && !paths.contains(dir.absoluteFilePath(f));
    EXPECT_TRUE(onlySigned) << "Qt's libraries were not mistaken for plugins";
    EXPECT_TRUE(QByteArray(PHYSALIS_ENGINE_API) == QByteArray("PhysalisEngine-1")) << "the signature string is what the plugin exports";

}
