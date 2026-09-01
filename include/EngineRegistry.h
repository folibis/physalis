#pragma once

#include "IPhysicsEngine.h"
#include <QString>
#include <QStringList>
#include <QVector>
#include <memory>

namespace physics {

namespace EngineRegistry {

// The engines found beside the executable, in the order they were loaded.
QStringList availableEngines();

// Where each came from -- for diagnostics and for the About box.
QStringList pluginPaths();

// One entry per loaded plugin, for anything that wants to list them.
struct PluginInfo {
    QString name;
    QString version;
    QString path;
};
QVector<PluginInfo> loadedPlugins();

// Returns nullptr for an unknown name.
std::unique_ptr<IPhysicsEngine> create(const QString &name);

} // namespace EngineRegistry

} // namespace physics
