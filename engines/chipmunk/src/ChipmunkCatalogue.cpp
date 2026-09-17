#include "ChipmunkEngine.h"

#include <QHash>
#include <QObject>

#include <cmath>

// What a Chipmunk body, shape and space offer, and what can happen to them.
//
// The editor has no list of property names anywhere: it asks for these,
// renders whatever it is given, and stores the keys without interpreting them.
// Where Chipmunk does what Box2D does the key is the Box2D plugin's, so a rule
// written against one engine still means something under the other. What
// Chipmunk has that Box2D does not -- a space's iterations, its collision slop
// and bias -- is here under Chipmunk's own names; what it lacks, such as
// rolling resistance and continuous collision, is simply not offered.

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

JointParam choice(const QString &key, const QString &label, bool readable, bool settable,
                  const QStringList &choices, const QString &tooltip = QString())
{
    JointParam p;
    p.key = key;
    p.label = label;
    p.type = ParamType::Choice;
    p.choices = choices;
    p.defaultValue = 0;
    p.liveReadable = readable;
    p.liveSettable = settable;
    p.tooltip = tooltip;
    return p;
}

JointParam actionParam(const QString &key, const QString &label, qreal defaultValue,
                       qreal minValue, qreal maxValue, int decimals, qreal step,
                       const QString &tooltip = QString())
{
    JointParam p = number(key, label, false, false, minValue, maxValue, decimals, step, tooltip);
    p.defaultValue = defaultValue;
    return p;
}

// Which properties belong to the object rather than to the run, and what one
// starts out as. The editor keeps exactly these, and knows none of them by
// name -- see the same table in the Box2D plugin.
// The editor draws a sensor differently and estimates a body's balance point
// from density, so it is told which keys those are rather than guessing.
void markRole(PropertyList *list, const QString &key, PropertyRole role)
{
    for (JointParam &property : *list) {
        if (property.key == key)
            property.role = role;
    }
}

void markRulesOnly(PropertyList *list, const QStringList &keys)
{
    for (JointParam &property : *list) {
        if (keys.contains(property.key))
            property.rulesOnly = true;
    }
}

void markStored(PropertyList *list, const QHash<QString, QVariant> &defaults,
                const QString &section)
{
    for (JointParam &property : *list) {
        const auto it = defaults.constFind(property.key);
        if (it == defaults.constEnd())
            continue;
        property.stored = true;
        property.defaultValue = *it;
        if (property.section.isEmpty())
            property.section = section;
    }
}

} // namespace

PropertyList ChipmunkEngine::bodyProperties() const
{
    PropertyList properties = {
        // cpBodyApplyImpulseAtWorldPoint, through the centre of mass.
        number(QStringLiteral("impulseY"), QObject::tr("Impulse Up/Down (up is negative)"),
               false, true, -1e6, 1e6, 1, 10.0,
               QObject::tr("A kick. Adds to whatever the body was already doing.")),
        number(QStringLiteral("impulseX"), QObject::tr("Impulse Left/Right"),
               false, true, -1e6, 1e6, 1, 10.0,
               QObject::tr("A kick sideways. Adds to whatever the body was already doing.")),
        number(QStringLiteral("angularImpulse"), QObject::tr("Spin Impulse"),
               false, true, -1e9, 1e9, 2, 1.0,
               QObject::tr("A twist. Sets something spinning, or adds to the spin it"
                           " already had.")),

        // cpBodyGetVelocity / SetVelocity.
        number(QStringLiteral("velocityY"), QObject::tr("Velocity Y (up is negative)"),
               true, true, -1e6, 1e6, 1, 10.0,
               QObject::tr("How fast it is travelling. Setting it replaces the motion"
                           " outright, where an impulse adds to it.")),
        number(QStringLiteral("velocityX"), QObject::tr("Velocity X"),
               true, true, -1e6, 1e6, 1, 10.0,
               QObject::tr("How fast it is travelling. Setting it replaces the motion"
                           " outright, where an impulse adds to it.")),
        number(QStringLiteral("speed"), QObject::tr("Speed"), true, false, 0.0, 1e6, 1, 10.0,
               QObject::tr("How fast it is going, whichever way. Read only.")),
        number(QStringLiteral("angularVelocity"), QObject::tr("Angular Velocity (deg/s)"),
               true, true, -1e5, 1e5, 1, 10.0,
               QObject::tr("How fast it turns, and which way.")),

        // cpBodySetPosition / SetAngle: a teleport, as in Box2D.
        number(QStringLiteral("positionX"), QObject::tr("Position X"),
               true, true, -1e7, 1e7, 1, 10.0,
               QObject::tr("Moving a body while it runs places it there outright, without"
                           " travelling -- so it can land inside something. Glide To X"
                           " arrives the long way instead.")),
        number(QStringLiteral("positionY"), QObject::tr("Position Y (down is positive)"),
               true, true, -1e7, 1e7, 1, 10.0,
               QObject::tr("Moving a body while it runs places it there outright, without"
                           " travelling -- so it can land inside something. Glide To Y"
                           " arrives the long way instead.")),
        number(QStringLiteral("angle"), QObject::tr("Angle (deg)"),
               true, true, -1e5, 1e5, 1, 1.0,
               QObject::tr("Which way it faces. Setting it mid-run turns it on the spot;"
                           " Glide To Angle turns it the long way.")),

        // The velocity that arrives there by the end of the step.
        number(QStringLiteral("targetX"), QObject::tr("Glide To X"),
               false, true, -1e7, 1e7, 1, 10.0,
               QObject::tr("Travels there rather than appearing there, so it pushes what"
                           " is in the way. Meant for kinematic bodies.")),
        number(QStringLiteral("targetY"), QObject::tr("Glide To Y"),
               false, true, -1e7, 1e7, 1, 10.0,
               QObject::tr("Travels there rather than appearing there, so it pushes what"
                           " is in the way. Meant for kinematic bodies.")),
        number(QStringLiteral("targetAngle"), QObject::tr("Glide To Angle (deg)"),
               false, true, -1e5, 1e5, 1, 1.0,
               QObject::tr("Turns there rather than appearing turned.")),

        // Chipmunk has no switch for this; the body is taken out of the space.
        flag(QStringLiteral("isEnabled"), QObject::tr("Enabled"), true, true,
             QObject::tr("A disabled body is taken out of the world, with its joints, until"
                         " it is enabled again.")),
        // cpBodyIsSleeping / cpBodyActivate / cpBodySleep.
        flag(QStringLiteral("isAwake"), QObject::tr("Awake"), true, true,
             QObject::tr("A sleeping body is left out of the simulation until something"
                         " disturbs it. Only a dynamic body can be put to sleep, and only"
                         " while the world allows sleeping.")),
        flag(QStringLiteral("enableSleep"), QObject::tr("Allow Sleep"), true, true,
             QObject::tr("Whether it may drop out of the simulation at all. Turn it off for"
                         " something that must keep reacting.")),
        flag(QStringLiteral("fixedRotation"), QObject::tr("Fixed Rotation"), true, true,
             QObject::tr("Stops it turning, whatever hits it: an infinite moment of"
                         " inertia.")),

        // cpBodyGetType / SetType, listed in the order the editor stores.
        choice(QStringLiteral("bodyType"), QObject::tr("Body Type"), true, true,
               { QObject::tr("Static"), QObject::tr("Kinematic"), QObject::tr("Dynamic") },
               QObject::tr("Turning scenery dynamic mid-run is how a shelf gives way. Its"
                           " mass is worked out again from its shapes.")),

        number(QStringLiteral("gravityScale"), QObject::tr("Gravity Scale"),
               true, true, -100.0, 100.0, 2, 0.1,
               QObject::tr("How much of the world's gravity this body feels. Zero floats,"
                           " negative falls upwards.")),
        number(QStringLiteral("linearDamping"), QObject::tr("Linear Damping"),
               true, true, 0.0, 1000.0, 2, 0.1,
               QObject::tr("Slows it as it travels, the way air does.")),
        number(QStringLiteral("angularDamping"), QObject::tr("Angular Damping"),
               true, true, 0.0, 1000.0, 2, 0.1,
               QObject::tr("The same, for spin.")),

        // cpBodyEachArbiter, counted: how many contacts this body is in.
        number(QStringLiteral("contactCount"), QObject::tr("Contacts"), true, false,
               0.0, 1e6, 0, 1.0,
               QObject::tr("How many contacts this body is in right now, across all its"
                           " shapes. Zero when nothing is touching it.")),
        // cpBodyEachConstraint, counted.
        number(QStringLiteral("jointCount"), QObject::tr("Joints"), true, false,
               0.0, 1e6, 0, 1.0,
               QObject::tr("How many joints hold this body. Drops when a rule breaks one.")),

        // The shapes' bounding boxes, together: the box the body takes up.
        number(QStringLiteral("boundsMinX"), QObject::tr("Bounds Left"), true, false,
               -1e7, 1e7, 1, 10.0,
               QObject::tr("The box the body takes up right now, worked out from its"
                           " shapes where they have got to.")),
        number(QStringLiteral("boundsMinY"), QObject::tr("Bounds Top"), true, false,
               -1e7, 1e7, 1, 10.0,
               QObject::tr("The box the body takes up right now, worked out from its"
                           " shapes where they have got to.")),
        number(QStringLiteral("boundsMaxX"), QObject::tr("Bounds Right"), true, false,
               -1e7, 1e7, 1, 10.0,
               QObject::tr("The box the body takes up right now, worked out from its"
                           " shapes where they have got to.")),
        number(QStringLiteral("boundsMaxY"), QObject::tr("Bounds Bottom"), true, false,
               -1e7, 1e7, 1, 10.0,
               QObject::tr("The box the body takes up right now, worked out from its"
                           " shapes where they have got to.")),

        // Chipmunk reports contacts per shape, so these switch every shape on
        // the body at once. Read back as they were set.
        flag(QStringLiteral("enableContactEvents"), QObject::tr("Contact Events"), true, true,
             QObject::tr("Switches contact reporting for every shape on this body at once,"
                         " rather than one shape at a time.")),
        flag(QStringLiteral("enableHitEvents"), QObject::tr("Hit Events"), true, true,
             QObject::tr("The same for hard knocks, across the whole body.")),

        // cpBodyGetCenterOfGravity, in the body's own frame.
        number(QStringLiteral("localCenterOfMassX"), QObject::tr("Centre of Mass X (local)"),
               true, false, -1e7, 1e7, 1, 10.0,
               QObject::tr("Where the body balances, measured from its own origin rather"
                           " than in the scene.")),
        number(QStringLiteral("localCenterOfMassY"), QObject::tr("Centre of Mass Y (local)"),
               true, false, -1e7, 1e7, 1, 10.0,
               QObject::tr("Where the body balances, measured from its own origin rather"
                           " than in the scene.")),

        // cpBodyGetMass / SetMass, GetMoment / SetMoment.
        number(QStringLiteral("mass"), QObject::tr("Mass (kg)"), true, true,
               0.0, 1e9, 4, 0.1,
               QObject::tr("Worked out from the shapes and their density. Setting it"
                           " overrides that until a shape's density changes.")),
        number(QStringLiteral("rotationalInertia"), QObject::tr("Moment of Inertia (kg·m²)"),
               true, true, 0.0, 1e9, 6, 0.1,
               QObject::tr("How hard it is to start or stop the body spinning.")),
        number(QStringLiteral("centerOfMassX"), QObject::tr("Centre of Mass X"),
               true, false, -1e7, 1e7, 1, 10.0,
               QObject::tr("A body turns about this point, not about its origin.")),
        number(QStringLiteral("centerOfMassY"), QObject::tr("Centre of Mass Y"),
               true, false, -1e7, 1e7, 1, 10.0,
               QObject::tr("A body turns about this point, not about its origin.")),
        // cpBodyKineticEnergy.
        number(QStringLiteral("kineticEnergy"), QObject::tr("Kinetic Energy (mJ)"),
               true, false, 0.0, 1e15, 3, 0.1,
               QObject::tr("How much motion it carries, travelling and turning together, in millijoules --"
                           " a body at the usual scale weighs grams, and joules read as zero."
                           " Reaches zero when it has come to rest.")),

        // cpBodyApplyForceAtWorldPoint and SetTorque, which Chipmunk clears
        // after every step -- so, as in Box2D, a rule's force is one step long.
        number(QStringLiteral("forceY"), QObject::tr("Force Up/Down (one step)"),
               false, true, -1e9, 1e9, 1, 10.0,
               QObject::tr("A force lasts only as long as it is applied, and a rule fires"
                           " for a single step. An impulse is usually what is wanted.")),
        number(QStringLiteral("forceX"), QObject::tr("Force Left/Right (one step)"),
               false, true, -1e9, 1e9, 1, 10.0,
               QObject::tr("Sideways, and just as brief.")),
        number(QStringLiteral("torque"), QObject::tr("Torque (one step)"),
               false, true, -1e9, 1e9, 2, 1.0,
               QObject::tr("The turning equivalent, and just as brief.")),
    };

    markStored(&properties, {
        {QStringLiteral("velocityX"), 0.0},
        {QStringLiteral("velocityY"), 0.0},
        {QStringLiteral("angularVelocity"), 0.0},
        {QStringLiteral("linearDamping"), 0.0},
        {QStringLiteral("angularDamping"), 0.0},
        {QStringLiteral("gravityScale"), 1.0},
        {QStringLiteral("fixedRotation"), false},
        {QStringLiteral("enableSleep"), true},
        {QStringLiteral("isAwake"), true},
    }, QObject::tr("Body"));
    // cpBodyApplyImpulseAtWorldPoint at the centre of gravity, one direction
    // each: what the editor's slingshot pushes a body with.
    markRole(&properties, QStringLiteral("impulseX"), PropertyRole::ImpulseX);
    markRole(&properties, QStringLiteral("impulseY"), PropertyRole::ImpulseY);
    // What "Init state" puts back and stops a body with.
    markRole(&properties, QStringLiteral("positionX"), PropertyRole::PositionX);
    markRole(&properties, QStringLiteral("positionY"), PropertyRole::PositionY);
    markRole(&properties, QStringLiteral("angle"), PropertyRole::Angle);
    markRole(&properties, QStringLiteral("velocityX"), PropertyRole::VelocityX);
    markRole(&properties, QStringLiteral("velocityY"), PropertyRole::VelocityY);
    markRole(&properties, QStringLiteral("angularVelocity"), PropertyRole::AngularVelocity);
    markRulesOnly(&properties, {QStringLiteral("boundsMinX"), QStringLiteral("boundsMinY"),
                                QStringLiteral("boundsMaxX"), QStringLiteral("boundsMaxY"),
                                QStringLiteral("localCenterOfMassX"), QStringLiteral("localCenterOfMassY"),
                                QStringLiteral("centerOfMassX"), QStringLiteral("centerOfMassY"),
                                // Where it stands is edited on the canvas.
                                QStringLiteral("positionX"), QStringLiteral("positionY"),
                                QStringLiteral("angle")});
    return properties;
}

QVector<ActionType> ChipmunkEngine::bodyActions() const
{
    QVector<ActionType> actions;

    // Chipmunk has no explosion; the engine applies Box2D's, shape by shape.
    ActionType explode;
    explode.id = QStringLiteral("explode");
    explode.label = QObject::tr("Explode");
    explode.description = QObject::tr(
        "Sets off a blast centred on the chosen object, pushing everything within reach "
        "away from it.");
    explode.params = {
        actionParam(QStringLiteral("impulse"), QObject::tr("Impulse"), 3.0, -1e6, 1e6, 2, 1.0,
                    QObject::tr("How hard, per unit of width facing the blast. Negative pulls"
                                " inward instead.")),
        actionParam(QStringLiteral("radius"), QObject::tr("Radius"), 200.0, 0.0, 1e6, 0, 10.0,
                    QObject::tr("How far the blast reaches, in scene units.")),
        actionParam(QStringLiteral("falloff"), QObject::tr("Falloff"), 100.0, 0.0, 1e6, 0, 10.0,
                    QObject::tr("How far past the radius the push fades to nothing.")),
        actionParam(QStringLiteral("maskBits"), QObject::tr("Affects Groups"), 0.0, 0.0,
                    9.007199254740992e15, 0, 1.0,
                    QObject::tr("Which collision groups the blast reaches, as the sum of"
                                " their bits. Zero means everything.")),
    };
    actions.append(explode);

    // cpBodyApplyImpulseAtWorldPoint, away from the centre of mass.
    ActionType push;
    push.id = QStringLiteral("pushAt");
    push.label = QObject::tr("Push at a Point");
    push.description = QObject::tr(
        "Kicks the object at an offset from its centre, so it spins as well as moves.");
    const QString offsetTip = QObject::tr("Where the kick lands, measured from the body's"
                                          " centre of mass in scene units.");
    push.params = {
        actionParam(QStringLiteral("impulseX"), QObject::tr("Impulse X"), 0.0, -1e6, 1e6, 1, 10.0),
        actionParam(QStringLiteral("impulseY"), QObject::tr("Impulse Y (up is negative)"), 0.0,
                    -1e6, 1e6, 1, 10.0),
        actionParam(QStringLiteral("offsetX"), QObject::tr("Offset X"), 0.0, -1e6, 1e6, 1, 10.0,
                    offsetTip),
        actionParam(QStringLiteral("offsetY"), QObject::tr("Offset Y"), 0.0, -1e6, 1e6, 1, 10.0,
                    offsetTip),
    };
    actions.append(push);

    // cpBodyApplyForceAtWorldPoint: the steady version of the kick above, and
    // as brief as any force -- Chipmunk clears it after the step.
    ActionType pushForce;
    pushForce.id = QStringLiteral("pushForceAt");
    pushForce.label = QObject::tr("Force at a Point (one step)");
    pushForce.description = QObject::tr(
        "Pushes with a force at an offset from the centre, for one step. A rule fires "
        "for a single step, so an impulse is usually what is wanted.");
    pushForce.params = push.params;
    actions.append(pushForce);

    // Chipmunk works a body's mass out from its shapes whenever one changes,
    // so setting any density again is how a mass set by hand is thrown away.
    ActionType resetMass;
    resetMass.id = QStringLiteral("resetMass");
    resetMass.label = QObject::tr("Recalculate Mass");
    resetMass.description = QObject::tr(
        "Works the mass and the moment out again from the shapes and their density, "
        "throwing away anything set by hand.");
    actions.append(resetMass);

    // cpSpaceRemoveBody, with its shapes and constraints.
    ActionType remove;
    remove.id = QStringLiteral("removeBody");
    remove.removesBody = true;
    remove.label = QObject::tr("Remove");
    remove.description = QObject::tr(
        "Takes the object out of the world for the rest of the run, along with any joints "
        "attached to it. Stopping brings it back.");
    actions.append(remove);

    return actions;
}

QVector<ActionType> ChipmunkEngine::jointActions() const
{
    // cpSpaceRemoveConstraint.
    ActionType breakJoint;
    breakJoint.id = QStringLiteral("breakJoint");
    breakJoint.label = QObject::tr("Break");
    breakJoint.description = QObject::tr(
        "Removes the joint for the rest of the run, letting go of whatever it was holding. "
        "Stopping puts it back.");
    return { breakJoint };
}

PropertyList ChipmunkEngine::shapeProperties() const
{
    PropertyList properties = {
        // cpShapeSetDensity: the body's mass is worked out again at once.
        number(QStringLiteral("density"), QObject::tr("Density"), true, true,
               0.0, 1e6, 2, 0.1,
               QObject::tr("Mass per unit area. The body's own mass is worked out from this"
                           " and the size of its shapes.")),
        number(QStringLiteral("friction"), QObject::tr("Friction"), true, true,
               0.0, 100.0, 2, 0.05,
               QObject::tr("How much it resists sliding. Zero is ice. Two surfaces combine"
                           " as the geometric mean, as they do in Box2D.")),
        // cpShapeSetElasticity.
        number(QStringLiteral("restitution"), QObject::tr("Restitution"), true, true,
               0.0, 100.0, 2, 0.05,
               QObject::tr("How much it bounces: 0 stops dead, 1 gives back all the speed."
                           " The bouncier of the two surfaces wins.")),
        // cpShapeSetSurfaceVelocity, along the body's own x axis.
        number(QStringLiteral("tangentSpeed"), QObject::tr("Surface Speed"),
               true, true, -1e6, 1e6, 1, 10.0,
               QObject::tr("The surface drags along what touches it, like a conveyor belt,"
                           " in the direction the body's x axis points.")),
        number(QStringLiteral("radius"), QObject::tr("Radius"), true, true,
               0.0, 1e7, 1, 1.0,
               QObject::tr("Circles only. Changing it resizes the collision shape where it"
                           " stands; what is drawn does not follow.")),
        number(QStringLiteral("mass"), QObject::tr("Mass (kg)"), true, false,
               0.0, 1e9, 4, 0.1,
               QObject::tr("What this shape alone contributes to its body -- its area times"
                           " its density.")),
        // cpShapeGetArea.
        number(QStringLiteral("area"), QObject::tr("Area"), true, false,
               0.0, 1e12, 0, 100.0,
               QObject::tr("How much it covers, in square scene units.")),
        // cpShapeGetMoment and cpShapeGetCenterOfGravity: this shape's own
        // share of what the body weighs, and where it sits.
        number(QStringLiteral("rotationalInertia"), QObject::tr("Moment of Inertia (kg·m²)"),
               true, false, 0.0, 1e9, 6, 0.1,
               QObject::tr("What this shape alone contributes to how hard the body is to"
                           " spin.")),
        number(QStringLiteral("centerOfMassX"), QObject::tr("Centre of Mass X"), true, false,
               -1e7, 1e7, 1, 10.0,
               QObject::tr("Where this shape balances, in scene coordinates.")),
        number(QStringLiteral("centerOfMassY"), QObject::tr("Centre of Mass Y"), true, false,
               -1e7, 1e7, 1, 10.0,
               QObject::tr("Where this shape balances, in scene coordinates.")),

        // cpShapeGetBB: the box it currently occupies.
        number(QStringLiteral("boundsMinX"), QObject::tr("Bounds Left"), true, false,
               -1e7, 1e7, 1, 10.0,
               QObject::tr("The box this shape takes up right now, where it has got to.")),
        number(QStringLiteral("boundsMinY"), QObject::tr("Bounds Top"), true, false,
               -1e7, 1e7, 1, 10.0,
               QObject::tr("The box this shape takes up right now, where it has got to.")),
        number(QStringLiteral("boundsMaxX"), QObject::tr("Bounds Right"), true, false,
               -1e7, 1e7, 1, 10.0,
               QObject::tr("The box this shape takes up right now, where it has got to.")),
        number(QStringLiteral("boundsMaxY"), QObject::tr("Bounds Bottom"), true, false,
               -1e7, 1e7, 1, 10.0,
               QObject::tr("The box this shape takes up right now, where it has got to.")),

        // The arbiters this shape is in, counted.
        number(QStringLiteral("contactCount"), QObject::tr("Contacts"), true, false,
               0.0, 1e6, 0, 1.0,
               QObject::tr("How many other shapes this one is touching right now.")),

        number(QStringLiteral("lastHitSpeed"), QObject::tr("Last Hit Speed"), true, false,
               0.0, 1e7, 1, 10.0,
               QObject::tr("How fast the two were closing on the last impact above the"
                           " world's hit threshold. Zero until something hits it.")),
        number(QStringLiteral("lastHitX"), QObject::tr("Last Hit X"), true, false,
               -1e7, 1e7, 1, 10.0,
               QObject::tr("Where the last hard impact landed.")),
        number(QStringLiteral("lastHitY"), QObject::tr("Last Hit Y"), true, false,
               -1e7, 1e7, 1, 10.0,
               QObject::tr("Where the last hard impact landed.")),
        number(QStringLiteral("lastHitNormalX"), QObject::tr("Last Hit Normal X"), true, false,
               -1.0, 1.0, 3, 0.1,
               QObject::tr("Which way the surface faced where it was struck.")),
        number(QStringLiteral("lastHitNormalY"), QObject::tr("Last Hit Normal Y"), true, false,
               -1.0, 1.0, 3, 0.1,
               QObject::tr("Which way the surface faced where it was struck.")),

        // cpShapeGetSensor / SetSensor -- settable while running, unlike Box2D.
        flag(QStringLiteral("isSensor"), QObject::tr("Sensor"), true, true,
             QObject::tr("A sensor is passed straight through: it reports what overlaps it"
                         " and stops nothing. Chipmunk can switch it while running.")),
        number(QStringLiteral("sensorOverlapCount"), QObject::tr("Things Inside"),
               true, false, 0.0, 1e6, 0, 1.0,
               QObject::tr("How many shapes are inside this sensor right now.")),

        flag(QStringLiteral("enableContactEvents"), QObject::tr("Contact Events"), true, true,
             QObject::tr("Whether touching and parting are reported to rules.")),
        flag(QStringLiteral("enableHitEvents"), QObject::tr("Hit Events"), true, true,
             QObject::tr("Whether hard knocks are reported, with the speed they arrived at.")),
        flag(QStringLiteral("enableSensorEvents"), QObject::tr("Sensor Events"), true, true,
             QObject::tr("Whether this shape can be noticed by sensors.")),
        flag(QStringLiteral("enablePreSolveEvents"), QObject::tr("Pre-Solve Events"), true, true,
             QObject::tr("Reports the contact every step while it lasts, before it is"
                         " resolved.")),

        number(QStringLiteral("categoryBits"), QObject::tr("Belongs To"), true, true,
               0.0, 9.007199254740992e15, 0, 1.0,
               QObject::tr("Which collision groups this shape is part of, as the sum of their"
                           " numbers: 1, 2, 4, 8 and so on.")),
        number(QStringLiteral("maskBits"), QObject::tr("Collides With"), true, true,
               0.0, 9.007199254740992e15, 0, 1.0,
               QObject::tr("Which groups it will hit, as the sum of their numbers. Both sides"
                           " have to agree before two shapes collide.")),
        number(QStringLiteral("groupIndex"), QObject::tr("Group Override"), true, true,
               -32768.0, 32767.0, 0, 1.0,
               QObject::tr("Shapes sharing a number above zero always collide, and sharing one"
                           " below zero never do -- whatever the two rows above say. Zero"
                           " leaves them to it.")),
    };

    markStored(&properties, {
        {QStringLiteral("density"), 1.0},
        // Chipmunk's own default is frictionless; this is Box2D's, so a scene
        // moved between the two slides the same.
        {QStringLiteral("friction"), 0.6},
        {QStringLiteral("restitution"), 0.0},
        {QStringLiteral("tangentSpeed"), 0.0},
        {QStringLiteral("isSensor"), false},
    }, QObject::tr("Shape"));
    markStored(&properties, {
        {QStringLiteral("enableContactEvents"), false},
        {QStringLiteral("enableHitEvents"), false},
        {QStringLiteral("enableSensorEvents"), false},
        {QStringLiteral("enablePreSolveEvents"), false},
        {QStringLiteral("categoryBits"), 1.0},
        {QStringLiteral("maskBits"), 9007199254740991.0},
        {QStringLiteral("groupIndex"), 0.0},
    }, QObject::tr("Collision"));
    markRole(&properties, QStringLiteral("isSensor"), PropertyRole::Sensor);
    markRole(&properties, QStringLiteral("density"), PropertyRole::Density);
    markRulesOnly(&properties, {QStringLiteral("boundsMinX"), QStringLiteral("boundsMinY"),
                                QStringLiteral("boundsMaxX"), QStringLiteral("boundsMaxY"),
                                QStringLiteral("centerOfMassX"), QStringLiteral("centerOfMassY"),
                                QStringLiteral("lastHitSpeed"), QStringLiteral("lastHitX"),
                                QStringLiteral("lastHitY"), QStringLiteral("lastHitNormalX"),
                                QStringLiteral("lastHitNormalY"), QStringLiteral("area"),
                                // The size is drawn; and a sensor's count belongs
                                // with the rules that watch it.
                                QStringLiteral("radius"), QStringLiteral("sensorOverlapCount")});
    return properties;
}

PropertyList ChipmunkEngine::worldProperties() const
{
    PropertyList properties = {
        // cpSpaceSetGravity.
        number(QStringLiteral("gravityX"), QObject::tr("Gravity X (m/s²)"), true, true,
               -1000.0, 1000.0, 2, 0.5,
               QObject::tr("Sideways gravity. Usually zero.")),
        number(QStringLiteral("gravityY"), QObject::tr("Gravity Y (m/s²)"), true, true,
               -1000.0, 1000.0, 2, 0.5,
               QObject::tr("Positive is down. A sleeping body does not notice it change.")),

        number(QStringLiteral("restitutionThreshold"),
               QObject::tr("Restitution Threshold (m/s)"), true, true, 0.0, 1000.0, 2, 0.1,
               QObject::tr("Below this closing speed nothing bounces, however bouncy it is.")),
        number(QStringLiteral("hitEventThreshold"), QObject::tr("Hit Event Threshold (m/s)"),
               true, true, 0.0, 1000.0, 2, 0.1,
               QObject::tr("How hard a knock has to be before it counts as a hit.")),
        number(QStringLiteral("maximumLinearSpeed"), QObject::tr("Max Speed (m/s)"),
               true, true, 0.0, 100000.0, 1, 10.0,
               QObject::tr("Nothing in the world may travel faster than this.")),

        flag(QStringLiteral("enableSleep"), QObject::tr("Allow Sleeping"), true, true,
             QObject::tr("Whether any body may drop out of the simulation once it has"
                         " settled.")),
        // cpSpaceSetIdleSpeedThreshold / SetSleepTimeThreshold.
        number(QStringLiteral("idleSpeedThreshold"), QObject::tr("Sleep Below Speed (m/s)"),
               true, true, 0.0, 1000.0, 3, 0.01,
               QObject::tr("Slower than this counts as idle. Chipmunk has one for the whole"
                           " world rather than one per body.")),
        number(QStringLiteral("sleepTimeThreshold"), QObject::tr("Sleep After (s)"),
               true, true, 0.0, 1000.0, 2, 0.1,
               QObject::tr("How long a group of bodies has to stay idle before it is put to"
                           " sleep.")),

        // cpSpaceSetIterations.
        number(QStringLiteral("iterations"), QObject::tr("Iterations"), true, true,
               1.0, 1000.0, 0, 1.0,
               QObject::tr("How many times each step the solver works through the contacts"
                           " and joints. More holds a tall stack together; fewer is faster.")),
        // cpSpaceSetDamping.
        number(QStringLiteral("damping"), QObject::tr("Velocity Kept per Second"), true, true,
               0.0, 1.0, 3, 0.05,
               QObject::tr("Chipmunk's own damping, for the whole world at once: 0.9 means"
                           " every body keeps 90% of its speed each second.")),
        // cpSpaceSetCollisionSlop / Bias / Persistence.
        number(QStringLiteral("collisionSlop"), QObject::tr("Collision Slop"), true, true,
               0.0, 1000.0, 2, 0.05,
               QObject::tr("How far shapes may overlap before they are pushed apart, in scene"
                           " units. A little stops resting contacts jittering.")),
        number(QStringLiteral("collisionBias"), QObject::tr("Overlap Left After 1 s"),
               true, true, 0.0, 1.0, 6, 0.0001,
               QObject::tr("How much of an overlap is still there after a second of pushing"
                           " apart. Smaller pushes harder.")),
        number(QStringLiteral("collisionPersistence"), QObject::tr("Contact Memory (steps)"),
               true, true, 0.0, 1000.0, 0, 1.0,
               QObject::tr("How many steps a contact is remembered after the shapes part,"
                           " so a jiggling pair does not begin and end every step.")),

        number(QStringLiteral("awakeBodyCount"), QObject::tr("Awake Bodies"), true, false,
               0.0, 1e9, 0, 1.0,
               QObject::tr("Reaches zero when everything has settled.")),
        number(QStringLiteral("bodyCount"), QObject::tr("Bodies"), true, false,
               0.0, 1e9, 0, 1.0,
               QObject::tr("How many bodies the world holds, asleep or not.")),
        number(QStringLiteral("contactCount"), QObject::tr("Contacts"), true, false,
               0.0, 1e9, 0, 1.0,
               QObject::tr("How many pairs of shapes are touching right now.")),
        number(QStringLiteral("jointCount"), QObject::tr("Joints"), true, false,
               0.0, 1e9, 0, 1.0,
               QObject::tr("How many joints the world holds.")),
        number(QStringLiteral("shapeCount"), QObject::tr("Shapes"), true, false,
               0.0, 1e9, 0, 1.0,
               QObject::tr("How many shapes the world holds, across every body.")),
    };

    markStored(&properties, {
        {QStringLiteral("gravityX"), 0.0},
        {QStringLiteral("gravityY"), 9.81},
    }, QObject::tr("World"));
    markStored(&properties, {
        {QStringLiteral("restitutionThreshold"), 1.0},
        {QStringLiteral("hitEventThreshold"), 1.0},
        {QStringLiteral("maximumLinearSpeed"), 400.0},
        {QStringLiteral("enableSleep"), true},
        {QStringLiteral("idleSpeedThreshold"), 0.05},
        {QStringLiteral("sleepTimeThreshold"), 0.5},
        {QStringLiteral("iterations"), 10},
        {QStringLiteral("damping"), 1.0},
        // A quarter of a scene unit: what Box2D's slop comes to at this scale.
        {QStringLiteral("collisionSlop"), 0.25},
        {QStringLiteral("collisionBias"), std::pow(1.0 - 0.1, 60.0)},
        {QStringLiteral("collisionPersistence"), 3},
    }, QObject::tr("Solver"));
    return properties;
}

PropertyList ChipmunkEngine::jointReadables(const QString &typeId) const
{
    // Only what that type actually measures, so a condition cannot be written
    // that could never be true.
    PropertyList result;
    const auto angle = [] {
        return number(QStringLiteral("angle"), QObject::tr("Angle (deg)"), true, false,
                      -1e5, 1e5, 1, 1.0,
                      QObject::tr("How far the second body has turned against the first since"
                                  " the run started."));
    };
    const auto angularSpeed = [] {
        return number(QStringLiteral("angularSpeed"), QObject::tr("Turn Rate (deg/s)"),
                      true, false, -1e5, 1e5, 1, 10.0,
                      QObject::tr("How fast the second body is turning against the first."));
    };
    const auto currentLength = [] {
        return number(QStringLiteral("currentLength"), QObject::tr("Current Length"),
                      true, false, 0.0, 1e7, 1, 10.0,
                      QObject::tr("How far apart the two ends are right now."));
    };
    const auto springNumbers = [&result] {
        result.push_back(number(QStringLiteral("stiffness"), QObject::tr("Stiffness"), true, false,
                                0.0, 1e12, 6, 1.0,
                                QObject::tr("What the frequency came to for the masses on the"
                                            " spring, in Chipmunk's own terms.")));
        result.push_back(number(QStringLiteral("damping"), QObject::tr("Damping"), true, false,
                                0.0, 1e12, 6, 1.0,
                                QObject::tr("What the damping ratio came to, likewise.")));
    };

    if (typeId == QLatin1String("pivot")) {
        result = { angle(), angularSpeed() };
    } else if (typeId == QLatin1String("pin") || typeId == QLatin1String("slide")) {
        result = { currentLength() };
    } else if (typeId == QLatin1String("groove")) {
        result = {
            number(QStringLiteral("translation"), QObject::tr("Translation"), true, false,
                   -1e7, 1e7, 1, 10.0,
                   QObject::tr("How far along the slot the pin is from where it started.")),
            number(QStringLiteral("speed"), QObject::tr("Speed"), true, false,
                   -1e6, 1e6, 1, 10.0,
                   QObject::tr("How fast it is sliding along the slot right now.")),
            number(QStringLiteral("axisAngle"), QObject::tr("Axis Angle (deg)"), true, true,
                   -360.0, 360.0, 2, 1.0,
                   QObject::tr("Which way the slot runs, in scene degrees. The handle drawn"
                               " on the canvas does not follow.")),
        };
    } else if (typeId == QLatin1String("dampedSpring")) {
        result = { currentLength() };
        springNumbers();
    } else if (typeId == QLatin1String("dampedRotarySpring")) {
        result = { angle() };
        springNumbers();
    } else if (typeId == QLatin1String("rotaryLimit") || typeId == QLatin1String("gear")) {
        result = { angle(), angularSpeed() };
    } else if (typeId == QLatin1String("ratchet")) {
        result = {
            angle(),
            number(QStringLiteral("ratchetAngle"), QObject::tr("Caught At (deg)"), true, false,
                   -1e5, 1e5, 1, 1.0,
                   QObject::tr("The angle the ratchet last caught at -- the furthest it has"
                               " been turned.")),
        };
    } else if (typeId == QLatin1String("simpleMotor")) {
        result = { angularSpeed() };
    } else if (typeId == QLatin1String("mouse")) {
        result = {
            number(QStringLiteral("targetX"), QObject::tr("Target X"), true, true,
                   -1e7, 1e7, 1, 10.0,
                   QObject::tr("The point the joint is dragging the body towards.")),
            number(QStringLiteral("targetY"), QObject::tr("Target Y"), true, true,
                   -1e7, 1e7, 1, 10.0,
                   QObject::tr("The point the joint is dragging the body towards.")),
        };
    }

    // The types that stop somewhere also say whether they are stopped there
    // right now -- the same thing the limit events report, asked as a question
    // rather than waited for.
    if (typeId == QLatin1String("slide") || typeId == QLatin1String("groove")
        || typeId == QLatin1String("rotaryLimit")) {
        result.push_back(flag(QStringLiteral("atLowerLimit"), QObject::tr("At Lower Limit"),
                              true, false,
                              QObject::tr("Whether the joint is against its lower bound right"
                                          " now, rather than having just arrived there.")));
        result.push_back(flag(QStringLiteral("atUpperLimit"), QObject::tr("At Upper Limit"),
                              true, false,
                              QObject::tr("Whether the joint is against its upper bound right"
                                          " now, rather than having just arrived there.")));
    }

    // cpConstraintGetImpulse, over the step it was applied in.
    result.push_back(number(QStringLiteral("constraintForce"), QObject::tr("Constraint Force"),
                            true, false, 0.0, 1e12, 4, 1.0,
                            QObject::tr("The load the joint carried last step -- a torque, for"
                                        " the ones that act on angles. Watch it to break"
                                        " something under strain.")));
    // cpConstraintGetCollideBodies / SetCollideBodies.
    result.push_back(flag(QStringLiteral("collideConnected"), QObject::tr("Bodies Collide"),
                          true, true,
                          QObject::tr("While off, the two bodies pass through each other. If"
                                      " one of them is scenery, the other falls straight"
                                      " through it.")));

    // Where the joint holds, for the types that hold at a point. A groove
    // holds body A along a slot, so only its B end is a point.
    const QString moved = QObject::tr("Moving it mid-run moves where the joint holds. The"
                                      " handle drawn on the canvas stays where it was put.");
    const bool pointA = typeId == QLatin1String("pivot") || typeId == QLatin1String("pin")
                        || typeId == QLatin1String("slide")
                        || typeId == QLatin1String("dampedSpring");
    const bool pointB = pointA || typeId == QLatin1String("groove")
                        || typeId == QLatin1String("mouse");
    if (pointA) {
        result.push_back(number(QStringLiteral("anchorAX"), QObject::tr("Anchor A X"),
                                true, true, -1e7, 1e7, 1, 10.0, moved));
        result.push_back(number(QStringLiteral("anchorAY"), QObject::tr("Anchor A Y"),
                                true, true, -1e7, 1e7, 1, 10.0, moved));
    }
    if (pointB) {
        result.push_back(number(QStringLiteral("anchorBX"), QObject::tr("Anchor B X"),
                                true, true, -1e7, 1e7, 1, 10.0, moved));
        result.push_back(number(QStringLiteral("anchorBY"), QObject::tr("Anchor B Y"),
                                true, true, -1e7, 1e7, 1, 10.0, moved));
    }
    return result;
}

QVector<EventType> ChipmunkEngine::shapeEvents() const
{
    // Chipmunk's collision handler callbacks. The ids are the Box2D plugin's,
    // so a rule written for one engine fires under the other.
    return {
        {QStringLiteral("contactBegin"), QObject::tr("begins contact"),
         QObject::tr("Raised when another shape starts touching this one.")},
        {QStringLiteral("contactEnd"), QObject::tr("ends contact"),
         QObject::tr("Raised when a shape that was touching this one separates.")},
        {QStringLiteral("contactHit"), QObject::tr("is hit"),
         QObject::tr("Raised when a contact begins faster than the world's hit threshold.")},
        {QStringLiteral("sensorBegin"), QObject::tr("is entered"),
         QObject::tr("Raised on a Sensor shape when something enters it. Whatever enters"
                     " needs Sensor Events on.")},
        {QStringLiteral("sensorEnd"), QObject::tr("is left"),
         QObject::tr("Raised on a Sensor shape when something that was inside it leaves.")},
        // The pre-solve callback runs every step the shapes touch. Chipmunk
        // has no speculative contacts, so unlike Box2D it comes no earlier
        // than "begins contact".
        {QStringLiteral("preSolve"), QObject::tr("is touching"),
         QObject::tr("Raised every step while the shape is in contact, before the"
                     " collision is resolved. The shape needs Pre-Solve Events on.")},
    };
}

QVector<EventType> ChipmunkEngine::bodyEvents() const
{
    return {
        {QStringLiteral("bodyMoved"), QObject::tr("starts moving"),
         QObject::tr("Raised while the simulation is carrying the body along, so a rule"
                     " watching for it fires as it sets off."), false},
        {QStringLiteral("bodyFellAsleep"), QObject::tr("comes to rest"),
         QObject::tr("Raised on the step a body is put to sleep, having stayed slower than"
                     " the world's sleep speed for long enough."), false},
    };
}

} // namespace physics
