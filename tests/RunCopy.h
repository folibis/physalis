#pragma once

#include "CanvasScene.h"
#include "Joint.h"
#include "PhysicsBody.h"
#include "RayItem.h"
#include "ShapeItem.h"
#include "SimulationController.h"

// A run moves its own copy of the scene and never the one it was started
// from. These find the copy of something a test built, by its name.
namespace runcopy {
template <class T, class List>
T *named(const List &items, const QString &name)
{
    for (T *item : items) {
        if (item->name() == name)
            return item;
    }
    return nullptr;
}
}

inline ShapeItem *inRun(const SimulationController &sim, const ShapeItem *original)
{
    return sim.runScene() && original
               ? runcopy::named<ShapeItem>(sim.runScene()->shapes(), original->name())
               : nullptr;
}

inline PhysicsBody *inRun(const SimulationController &sim, const PhysicsBody *original)
{
    return sim.runScene() && original
               ? runcopy::named<PhysicsBody>(sim.runScene()->bodies(), original->name())
               : nullptr;
}

inline Joint *inRun(const SimulationController &sim, const Joint *original)
{
    return sim.runScene() && original
               ? runcopy::named<Joint>(sim.runScene()->joints(), original->name())
               : nullptr;
}

inline RayItem *inRun(const SimulationController &sim, const RayItem *original)
{
    return sim.runScene() && original
               ? runcopy::named<RayItem>(sim.runScene()->rays(), original->name())
               : nullptr;
}
