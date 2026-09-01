#include "EngineRegistry.h"

#include "PluginApi.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QLibrary>
#include <QVector>

namespace physics {
namespace EngineRegistry {

namespace {

struct Plugin {
    QString name;
    QString version;
    QString path;
    PhysalisCreateEngineFn create = nullptr;
};

// Only files named like this are even opened. The exported symbol is what
// actually decides, but the prefix keeps the loader from prodding every
// unrelated library that happens to sit beside the executable.
const QString kPrefix = QStringLiteral("physalis_");

// Loaded once and kept for the life of the process: unloading a plugin would
// pull the vtable out from under any engine it handed out.
QVector<Plugin> &plugins()
{
    static QVector<Plugin> loaded;
    static bool scanned = false;
    if (scanned)
        return loaded;
    scanned = true;

    const QDir dir(QCoreApplication::applicationDirPath());
    QStringList filters;
    for (const QString &suffix : { QStringLiteral("dll"), QStringLiteral("so"),
                                   QStringLiteral("dylib") })
        filters << kPrefix + QStringLiteral("*.") + suffix;

    const QFileInfoList candidates = dir.entryInfoList(filters, QDir::Files, QDir::Name);
    for (const QFileInfo &file : candidates) {
        auto *library = new QLibrary(file.absoluteFilePath());
        if (!library->load()) {
            qWarning() << "engine plugin failed to load:" << file.fileName()
                       << library->errorString();
            delete library;
            continue;
        }

        // The signature. A library without it, or built against a different
        // revision, is left alone rather than half-used.
        auto api = reinterpret_cast<PhysalisEngineApiFn>(library->resolve("physalisEngineApi"));
        if (!api || QByteArray(api()) != QByteArray(PHYSALIS_ENGINE_API)) {
            library->unload();
            delete library;
            continue;
        }

        auto nameOf = reinterpret_cast<PhysalisEngineNameFn>(library->resolve("physalisEngineName"));
        auto create = reinterpret_cast<PhysalisCreateEngineFn>(library->resolve("physalisCreateEngine"));
        if (!nameOf || !create) {
            qWarning() << "engine plugin is missing an entry point:" << file.fileName();
            library->unload();
            delete library;
            continue;
        }

        // Optional: a plugin built before versions existed omits it.
        auto versionOf =
            reinterpret_cast<PhysalisEngineVersionFn>(library->resolve("physalisEngineVersion"));

        loaded.append(Plugin { QString::fromUtf8(nameOf()),
                               versionOf ? QString::fromUtf8(versionOf()) : QString(),
                               file.absoluteFilePath(), create });
    }

    return loaded;
}

} // namespace

QStringList availableEngines()
{
    QStringList names;
    for (const Plugin &plugin : plugins())
        names << plugin.name;
    return names;
}

QStringList pluginPaths()
{
    QStringList paths;
    for (const Plugin &plugin : plugins())
        paths << plugin.path;
    return paths;
}

QVector<PluginInfo> loadedPlugins()
{
    QVector<PluginInfo> infos;
    for (const Plugin &plugin : plugins())
        infos.append(PluginInfo { plugin.name, plugin.version, plugin.path });
    return infos;
}

std::unique_ptr<IPhysicsEngine> create(const QString &name)
{
    for (const Plugin &plugin : plugins()) {
        if (plugin.name == name)
            return std::unique_ptr<IPhysicsEngine>(plugin.create());
    }

    if (name.isEmpty() && !plugins().isEmpty())
        return std::unique_ptr<IPhysicsEngine>(plugins().front().create());

    return nullptr; // no plugin answers to that name
}

} // namespace EngineRegistry
} // namespace physics
