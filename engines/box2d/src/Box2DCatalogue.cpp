#include "Box2DEngine.h"

#include <QObject>

// What a Box2D body and shape offer, and what can happen to them.
//
// The editor has no list of property names anywhere: it asks for these,
// renders whatever it is given, and stores the keys without interpreting
// them. Everything here names a real Box2D call -- the read/write flags say
// which of b2Body_Get*/b2Body_Set* exists, and the event ids are the streams
// b2World_GetContactEvents actually produces.

namespace physics {

namespace {

JointParam number(const QString &key, const QString &label, bool readable, bool settable,
                  qreal minValue, qreal maxValue, int decimals, qreal step,
                  const QString &tooltip = QString())
{
    JointParam p;
    p.key = key;
    p.label = label;
    p.type = ParamType::Real;
    p.minValue = minValue;
    p.maxValue = maxValue;
    p.decimals = decimals;
    p.step = step;
    p.defaultValue = 0.0;
    p.liveReadable = readable;
    p.liveSettable = settable;
    p.tooltip = tooltip;
    return p;
}

JointParam flag(const QString &key, const QString &label, bool readable, bool settable,
                const QString &tooltip = QString())
{
    JointParam p;
    p.key = key;
    p.label = label;
    p.type = ParamType::Bool;
    p.defaultValue = false;
    p.liveReadable = readable;
    p.liveSettable = settable;
    p.tooltip = tooltip;
    return p;
}

} // namespace

PropertyList Box2DEngine::bodyProperties() const
{
    // Lengths and speeds are in scene units, angles in degrees -- the same
    // numbers the property table shows, converted on the way in and out.
    return {
        // Applied, not assigned: b2Body_ApplyLinearImpulseToCenter. Nothing to
        // read back, which is why these are write-only.
        number(QStringLiteral("impulseY"), QObject::tr("Impulse Up/Down (up is negative)"),
               false, true, -1e6, 1e6, 1, 10.0,
               QObject::tr("A kick. Adds to whatever the body was already doing,"
                           " so something arriving fast leaves faster.")),
        number(QStringLiteral("impulseX"), QObject::tr("Impulse Left/Right"),
               false, true, -1e6, 1e6, 1, 10.0),

        // b2Body_GetLinearVelocity / SetLinearVelocity.
        number(QStringLiteral("velocityY"), QObject::tr("Velocity Y (up is negative)"),
               true, true, -1e6, 1e6, 1, 10.0),
        number(QStringLiteral("velocityX"), QObject::tr("Velocity X"),
               true, true, -1e6, 1e6, 1, 10.0),
        // b2Body_GetLinearVelocity, magnitude only -- there is no setter for a
        // speed without a direction.
        number(QStringLiteral("speed"), QObject::tr("Speed"), true, false, 0.0, 1e6, 1, 10.0),

        // b2Body_GetPosition / b2Body_SetTransform. Setting one is a
        // teleport: the body arrives without travelling, so it can appear
        // inside something. That is what "move it there" means, and it is the
        // caller's business.
        number(QStringLiteral("positionX"), QObject::tr("Position X"),
               true, true, -1e7, 1e7, 1, 10.0,
               QObject::tr("Moving a body while it runs places it there outright,"
                           " without travelling -- so it can land inside something.")),
        number(QStringLiteral("positionY"), QObject::tr("Position Y (down is positive)"),
               true, true, -1e7, 1e7, 1, 10.0,
               QObject::tr("Moving a body while it runs places it there outright,"
                           " without travelling -- so it can land inside something.")),
        number(QStringLiteral("angle"), QObject::tr("Angle (deg)"),
               true, true, -1e5, 1e5, 1, 1.0),
        number(QStringLiteral("angularVelocity"), QObject::tr("Angular Velocity (deg/s)"),
               true, false, -1e5, 1e5, 1, 10.0),

        // b2Body_Enable / Disable / IsEnabled.
        flag(QStringLiteral("isEnabled"), QObject::tr("Enabled"), true, true,
             QObject::tr("A disabled body stops colliding and moving entirely.")),
        flag(QStringLiteral("isAwake"), QObject::tr("Awake"), true, true),
        flag(QStringLiteral("enableSleep"), QObject::tr("Allow Sleep"), true, true),
        flag(QStringLiteral("fixedRotation"), QObject::tr("Fixed Rotation"), true, true),
        flag(QStringLiteral("isBullet"), QObject::tr("Bullet"), true, true),

        number(QStringLiteral("gravityScale"), QObject::tr("Gravity Scale"),
               true, true, -100.0, 100.0, 2, 0.1),
        number(QStringLiteral("linearDamping"), QObject::tr("Linear Damping"),
               true, true, 0.0, 1000.0, 2, 0.1),
        number(QStringLiteral("angularDamping"), QObject::tr("Angular Damping"),
               true, true, 0.0, 1000.0, 2, 0.1),

        // b2Body_ApplyForceToCenter. Last, because a rule fires for one step
        // and a force applied for one step is nearly nothing.
        number(QStringLiteral("forceY"), QObject::tr("Force Up/Down (one step)"),
               false, true, -1e9, 1e9, 1, 10.0,
               QObject::tr("A force lasts only as long as it is applied, and a rule"
                           " fires for a single step. An impulse is usually what"
                           " is wanted.")),
        number(QStringLiteral("forceX"), QObject::tr("Force Left/Right (one step)"),
               false, true, -1e9, 1e9, 1, 10.0),

    };
}

QVector<ActionType> Box2DEngine::bodyActions() const
{
    // b2World_Explode. It takes a position rather than a body, so the body a
    // rule names only says where -- everything else is the action's own.
    ActionType explode;
    explode.id = QStringLiteral("explode");
    explode.label = QObject::tr("Explode");
    explode.description = QObject::tr(
        "Sets off a blast centred on the chosen object, pushing everything "
        "within reach away from it.");

    JointParam impulse;
    impulse.key = QStringLiteral("impulse");
    impulse.label = QObject::tr("Impulse");
    impulse.defaultValue = 3.0;
    impulse.minValue = -1e6;
    impulse.maxValue = 1e6;
    impulse.decimals = 2;
    impulse.step = 1.0;
    impulse.tooltip = QObject::tr("How hard, per unit of surface facing the blast."
                                  " Negative pulls inward instead.");

    JointParam radius;
    radius.key = QStringLiteral("radius");
    radius.label = QObject::tr("Radius");
    radius.defaultValue = 200.0;
    radius.minValue = 0.0;
    radius.maxValue = 1e6;
    radius.decimals = 0;
    radius.step = 10.0;
    radius.tooltip = QObject::tr("How far the blast reaches, in scene units.");

    JointParam falloff;
    falloff.key = QStringLiteral("falloff");
    falloff.label = QObject::tr("Falloff");
    falloff.defaultValue = 100.0;
    falloff.minValue = 0.0;
    falloff.maxValue = 1e6;
    falloff.decimals = 0;
    falloff.step = 10.0;
    falloff.tooltip = QObject::tr("How far past the radius the push fades to nothing.");

    explode.params = { impulse, radius, falloff };
    return { explode };
}

PropertyList Box2DEngine::shapeProperties() const
{
    // The three b2Shape_Set*/Get* pairs. Geometry is not among them: changing
    // an outline means rebuilding the shape, which is not setting a value.
    return {
        number(QStringLiteral("density"), QObject::tr("Density"), true, true,
               0.0, 1e6, 2, 0.1),
        number(QStringLiteral("friction"), QObject::tr("Friction"), true, true,
               0.0, 100.0, 2, 0.05),
        number(QStringLiteral("restitution"), QObject::tr("Restitution"), true, true,
               0.0, 100.0, 2, 0.05),
    };
}

PropertyList Box2DEngine::jointReadables(const QString &typeId) const
{
    // Only what that type actually measures. A revolute has an angle and no
    // translation; offering both would let a condition be written that could
    // never be true.
    PropertyList result;

    if (typeId == QLatin1String("revolute")) {
        result = {
            number(QStringLiteral("angle"), QObject::tr("Angle (deg)"), true, false,
                   -1e5, 1e5, 1, 1.0),
            number(QStringLiteral("motorSpeed"), QObject::tr("Motor Speed (deg/s)"), true, false,
                   -1e5, 1e5, 1, 10.0),
            number(QStringLiteral("motorTorque"), QObject::tr("Motor Torque"), true, false,
                   -1e9, 1e9, 2, 1.0),
        };
    }
    if (typeId == QLatin1String("prismatic")) {
        result = {
            number(QStringLiteral("translation"), QObject::tr("Translation"), true, false,
                   -1e7, 1e7, 1, 10.0),
            number(QStringLiteral("speed"), QObject::tr("Speed"), true, false,
                   -1e6, 1e6, 1, 10.0),
            number(QStringLiteral("motorSpeed"), QObject::tr("Motor Speed"), true, false,
                   -1e6, 1e6, 1, 10.0),
            number(QStringLiteral("motorForce"), QObject::tr("Motor Force"), true, false,
                   -1e9, 1e9, 2, 1.0),
        };
    }
    if (typeId == QLatin1String("distance")) {
        result = {
            number(QStringLiteral("length"), QObject::tr("Length"), true, false,
                   0.0, 1e7, 1, 10.0),
            number(QStringLiteral("motorSpeed"), QObject::tr("Motor Speed"), true, false,
                   -1e6, 1e6, 1, 10.0),
        };
    }
    if (typeId == QLatin1String("wheel")) {
        result = {
            number(QStringLiteral("motorSpeed"), QObject::tr("Motor Speed (deg/s)"), true, false,
                   -1e5, 1e5, 1, 10.0),
            number(QStringLiteral("motorTorque"), QObject::tr("Motor Torque"), true, false,
                   -1e9, 1e9, 2, 1.0),
        };
    }

    // Every joint reports the load it is carrying, whatever kind it is -- so
    // these are appended to whatever that type measures of its own.
    result.push_back(number(QStringLiteral("constraintForce"),
                            QObject::tr("Constraint Force (N)"), true, false,
                            0.0, 1e12, 2, 1.0));
    result.push_back(number(QStringLiteral("constraintTorque"),
                            QObject::tr("Constraint Torque (N·m)"), true, false,
                            -1e12, 1e12, 2, 1.0));
    return result;
}

QVector<EventType> Box2DEngine::shapeEvents() const
{
    // The three streams b2World_GetContactEvents returns. Contacts are per
    // shape in Box2D, so this is where they belong.
    return {
        {QStringLiteral("contactBegin"), QObject::tr("begins contact"),
         QObject::tr("Raised when another shape starts touching this one.")},
        {QStringLiteral("contactEnd"), QObject::tr("ends contact"),
         QObject::tr("Raised when a shape that was touching this one separates.")},
        {QStringLiteral("contactHit"), QObject::tr("is hit"),
         QObject::tr("Raised on an impact above the world's hit threshold.")},
        {QStringLiteral("sensorBegin"), QObject::tr("is entered"),
         QObject::tr("Raised on a Sensor shape when something enters it. The "
                     "sensor needs Sensor on, and whatever enters needs "
                     "Sensor Events on.")},
        {QStringLiteral("sensorEnd"), QObject::tr("is left"),
         QObject::tr("Raised on a Sensor shape when something that was inside "
                     "it leaves.")},
    };
}

QVector<EventType> Box2DEngine::bodyEvents() const
{
    // None. A body does not collide -- its shapes do -- and Box2D raises no
    // body-level event at all. Saying so plainly beats offering a trigger
    // that could never fire.
    return {};
}

} // namespace physics
