#include "Box2DEngine.h"
#include "PluginApi.h"

#include <box2d/base.h>

#include <cstdio>

namespace {
// Asked of Box2D itself rather than hard-coded, so it cannot drift from the
// library actually linked in.
const char *box2dVersion()
{
    static char text[32];
    const b2Version version = b2GetVersion();
    std::snprintf(text, sizeof(text), "%d.%d.%d", version.major, version.minor,
                  version.revision);
    return text;
}
} // namespace

// The three exported entry points. Everything else in this library is private
// to it -- the application only ever sees IPhysicsEngine.
PHYSALIS_DECLARE_ENGINE("Box2D", box2dVersion(), physics::Box2DEngine)
