#pragma once

#include <QPointF>
#include <QtMath>
#include <QString>
#include <QVector>
#include <cstdint>

namespace physics {

// Units, throughout this header:
//   * geometry, positions and transforms are in *scene units* (pixels), as
//     they come straight off a ShapeItem and go straight back onto one. The
//     engine converts them via kMotionPixelsPerMeter.
//   * everything else -- gravity, velocity, density, friction -- is a
//     physical quantity in SI units and is passed through untouched.
//   * angles are in degrees, matching QGraphicsItem::rotation().

enum class BodyType {
    Static,     // never moves; infinite mass
    Kinematic,  // moved by velocity only, unaffected by forces/collisions
    Dynamic     // fully simulated
};

enum class GeometryKind {
    Box,      // axis-aligned in body-local space; uses halfExtents
    Circle,   // uses radius
    Polygon,  // solid outline through points; the engine may need it convex
    Chain     // just the edges through points -- no interior, and so no area
};

// A shape's collision geometry, expressed in body-local coordinates and in
// scene units (pixels).
struct Geometry {
    GeometryKind kind = GeometryKind::Box;
    QPointF center;               // Box/Circle: geometry center
    qreal rotationDegrees = 0.0;
    QPointF halfExtents;          // Box
    qreal radius = 0.0;           // Circle
    QVector<QPointF> points;      // Polygon/Chain
    bool closed = false;          // Chain: does the last point join the first
    // Box: rounds the corners without growing the outline. At half the shorter
    // side the shape becomes a capsule.
    qreal cornerRadius = 0.0;
    // Chain: join the edges into one surface, so a body sliding along does not
    // catch where two of them meet. The cost is that the surface becomes
    // one-sided, and it needs at least four points.
    bool smoothChain = false;
};

struct Material {
    // Coulomb friction coefficient, usually [0, 1].
    qreal friction = 0.6;
    // Bounciness, usually [0, 1]. 0 is a dead stop, 1 is a perfect bounce.
    qreal restitution = 0.0;
    // Resistance to rolling, usually [0, 1]. Stops balls rolling forever.
    qreal rollingResistance = 0.0;
    qreal tangentSpeed = 0.0;
};

struct Filter {
    quint64 categoryBits = 0x0001; // what this shape *is*
    quint64 maskBits = ~quint64(0); // what this shape collides *with*
    int groupIndex = 0;             // >0 always collide, <0 never, 0 no effect
};

struct ShapePart {
    QString name;

    Geometry geometry;
    Material material;

    qreal density = 1.0;

    Filter filter;

    // A sensor detects overlap but never collides -- things pass through it.
    bool isSensor = false;

    bool enableSensorEvents = false;
    bool enableContactEvents = false;
    bool enableHitEvents = false;      // fired on fast impacts
    bool enablePreSolveEvents = false; // lets contacts be inspected/cancelled
};

struct BodyDesc {
    // At least one part; an empty body can't be simulated.
    QVector<ShapePart> parts;

    QString name;

    // World transform at simulation start, in scene units / degrees.
    QPointF position;
    qreal rotationDegrees = 0.0;

    BodyType type = BodyType::Dynamic;

    QPointF linearVelocity;                // m/s
    qreal angularVelocityDegrees = 0.0;    // degrees/s

    qreal linearDamping = 0.0;
    qreal angularDamping = 0.0;

    qreal gravityScale = 1.0;

    bool enableSleep = true;
    bool isAwake = true;
    qreal sleepThreshold = 0.05;

    bool fixedRotation = false;

    bool isBullet = false;


    // Lets a small round body spin faster than the usual safety limit.
    bool allowFastRotation = false;

    // A disabled body stays in the world but neither moves nor collides.
    bool isEnabled = true;
};

using BodyHandle = int;
inline constexpr BodyHandle kInvalidBody = -1;

inline bool isConvex(const QVector<QPointF> &points)
{
    const int n = points.size();
    if (n < 3)
        return false;

    int sign = 0;
    for (int i = 0; i < n; ++i) {
        const QPointF &a = points[i];
        const QPointF &b = points[(i + 1) % n];
        const QPointF &c = points[(i + 2) % n];
        const qreal cross = (b.x() - a.x()) * (c.y() - b.y()) - (b.y() - a.y()) * (c.x() - b.x());
        if (qFuzzyIsNull(cross))
            continue;
        const int currentSign = cross > 0 ? 1 : -1;
        if (sign == 0)
            sign = currentSign;
        else if (sign != currentSign)
            return false;
    }
    return true;
}

inline constexpr qreal kReferencePixelsPerMeter = 50.0;

struct WorldDesc {
    QPointF gravity { 0.0, 9.81 };

    qreal pixelsPerMeter = 1000.0;

    // The solver's own tuning. Defaults are Box2D's, so leaving these alone
    // reproduces its out-of-the-box behaviour exactly.

    // Below this closing speed restitution is ignored entirely, so a bouncy
    // shape moving slower than it simply will not bounce.
    qreal restitutionThreshold = 1.0;   // m/s
    // The speed an impact must reach before a hit event is raised.
    qreal hitEventThreshold = 1.0;      // m/s

    // How stiffly overlap is pushed apart. Higher recovers faster but jitters.
    qreal contactHertz = 30.0;          // Hz
    qreal contactDampingRatio = 10.0;
    // Caps how fast overlap recovery may push, whatever the stiffness says.
    qreal maxContactPushSpeed = 3.0;    // m/s

    qreal maximumLinearSpeed = 400.0;   // m/s

    // How many times the solver relaxes the constraints within one step. More
    // holds a tall stack together; fewer is faster and springier. Box2D's
    // samples use 4.
    int subStepCount = 4;

    bool enableSleep = true;
    // Continuous collision, which stops fast bodies tunnelling through thin
    // ones. Off is cheaper but lets things pass through walls.
    bool enableContinuous = true;
};

// What the simulation produces each step, in the same scene units/degrees the
// BodyDesc came in as, ready to push straight onto the editor's shapes.
// What a ray found. Everything is in scene units, measured from the ray's
// own origin, so the caller never sees the engine's units.
struct RayHit {
    bool hit = false;
    QPointF point;      // where it struck
    QPointF normal;     // the surface it struck, pointing away from it
    qreal distance = 0.0;
    QString shapeName;  // what was struck, empty if nothing was
};

struct BodyState {
    // False once the body has been taken out of the world. Everything else is
    // then meaningless -- there is nothing left to have a position.
    bool exists = true;
    QPointF position;
    // Where the mass actually sits, in scene units. A body rotates about this,
    // not about its origin, so it is the point worth showing.
    QPointF centerOfMass;
    qreal rotationDegrees = 0.0;
    bool awake = true;
};

} // namespace physics
