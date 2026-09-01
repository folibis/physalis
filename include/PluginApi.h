#pragma once

#include "IPhysicsEngine.h"

#include <QtGlobal>

// What every engine plugin must export. A shared library is only treated as a
// plugin if it exports physalisEngineApi() and that returns this exact string
// -- so an unrelated library sitting beside the executable is ignored rather
// than half-loaded.
#define PHYSALIS_ENGINE_API "PhysalisEngine-1"

extern "C" {

// Identifies the library as an engine plugin, and which revision of this
// interface it was built against.
using PhysalisEngineApiFn = const char *(*)();

// The name shown in the engine chooser, e.g. "Box2D".
using PhysalisEngineNameFn = const char *(*)();

// The version of the engine behind the plugin, e.g. "3.1.1". Optional -- a
// plugin built before this existed simply does not export it.
using PhysalisEngineVersionFn = const char *(*)();

// A fresh engine. The caller owns it and deletes it through the base class,
// so IPhysicsEngine's destructor has to stay virtual.
using PhysalisCreateEngineFn = physics::IPhysicsEngine *(*)();

}

// Put this in exactly one translation unit of a plugin.
#define PHYSALIS_DECLARE_ENGINE(NAME, VERSION, TYPE)                          \
    extern "C" Q_DECL_EXPORT const char *physalisEngineApi()                   \
    {                                                                          \
        return PHYSALIS_ENGINE_API;                                            \
    }                                                                          \
    extern "C" Q_DECL_EXPORT const char *physalisEngineName() { return NAME; } \
    extern "C" Q_DECL_EXPORT const char *physalisEngineVersion()              \
    {                                                                         \
        return VERSION;                                                       \
    }                                                                         \
    extern "C" Q_DECL_EXPORT physics::IPhysicsEngine *physalisCreateEngine()   \
    {                                                                          \
        return new TYPE;                                                       \
    }
