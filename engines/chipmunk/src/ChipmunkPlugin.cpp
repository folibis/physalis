#include "ChipmunkEngine.h"
#include "PluginApi.h"

// The three exported entry points. Everything else in this library is private
// to it -- the application only ever sees IPhysicsEngine. The version is asked
// of Chipmunk itself rather than written here, so it cannot drift from the
// library actually linked in.
PHYSALIS_DECLARE_ENGINE("Chipmunk2D", cpVersionString, physics::ChipmunkEngine)
