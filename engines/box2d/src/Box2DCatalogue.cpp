#include "Box2DEngine.h"

#include <QHash>
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

// The three switches every joint type has some of. Which ones a type gets is
// decided below; the wording is the same wherever they appear.
//
// Each only reads back the switch the joint already carries as a parameter, so
// they are marked as mirroring it: a rule wants them, to ask whether the motor
// is on, and a property table does not, because the switch itself is already a
// row in it.
void appendJointSwitches(PropertyList *into, bool spring, bool limit, bool motor)
{
    const auto mirror = [into] { into->back().mirrorsSetting = true; };
    if (spring) {
        into->push_back(flag(QStringLiteral("springEnabled"), QObject::tr("Spring Enabled"),
                             true, false,
                             QObject::tr("Whether the joint is holding softly rather than"
                                         " rigidly. Read only: it is set on the joint.")));
        mirror();
    }
    if (limit) {
        into->push_back(flag(QStringLiteral("limitEnabled"), QObject::tr("Limit Enabled"),
                             true, false,
                             QObject::tr("Whether the joint is stopping at its lower and"
                                         " upper bounds.")));
        mirror();
        // The same thing the limit events report, asked as a question rather
        // than waited for: an event fires on arrival, these stay true for as
        // long as the joint is sitting there.
        into->push_back(flag(QStringLiteral("atLowerLimit"), QObject::tr("At Lower Limit"),
                             true, false,
                             QObject::tr("Whether the joint is against its lower bound"
                                         " right now. False while it has room to move,"
                                         " and always false with the limit switched off.")));
        into->push_back(flag(QStringLiteral("atUpperLimit"), QObject::tr("At Upper Limit"),
                             true, false,
                             QObject::tr("Whether the joint is against its upper bound"
                                         " right now. False while it has room to move,"
                                         " and always false with the limit switched off.")));
    }
    if (motor) {
        into->push_back(flag(QStringLiteral("motorEnabled"), QObject::tr("Motor Enabled"),
                             true, false,
                             QObject::tr("Whether the joint is driving itself towards its"
                                         " motor speed.")));
        mirror();
    }
}

// Which of the properties below belong to the *object* rather than to the run,
// and what one starts out as. Kept as tables rather than as two more arguments
// on every call: this is the answer to "what does the editor store", and it
// reads better in one place than spread over forty lines. Anything not named
// here is a readout or a one-shot push -- where a body has got to, a kick --
// and the editor neither stores nor shows it.
// The editor draws a sensor differently and estimates a body's balance point
// from density, so it is told which keys those are rather than guessing.
void markRole(PropertyList *list, const QString &key, PropertyRole role)
{
    for (JointParam &property : *list) {
        if (property.key == key)
            property.role = role;
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

PropertyList Box2DEngine::bodyProperties() const
{
    // Lengths and speeds are in scene units, angles in degrees -- the same
    // numbers the property table shows, converted on the way in and out.
    PropertyList properties = {
        // Applied, not assigned: b2Body_ApplyLinearImpulseToCenter. Nothing to
        // read back, which is why these are write-only.
        number(QStringLiteral("impulseY"), QObject::tr("Impulse Up/Down (up is negative)"),
               false, true, -1e6, 1e6, 1, 10.0,
               QObject::tr("A kick. Adds to whatever the body was already doing,"
                           " so something arriving fast leaves faster.")),
        number(QStringLiteral("impulseX"), QObject::tr("Impulse Left/Right"),
               false, true, -1e6, 1e6, 1, 10.0,
               QObject::tr("A kick sideways. Adds to whatever the body was already"
                           " doing.")),
        // b2Body_ApplyAngularImpulse -- the same kick, about the centre of
        // mass instead of through it.
        number(QStringLiteral("angularImpulse"), QObject::tr("Spin Impulse"),
               false, true, -1e9, 1e9, 2, 1.0,
               QObject::tr("A twist. Sets something spinning, or adds to the spin"
                           " it already had.")),

        // b2Body_GetLinearVelocity / SetLinearVelocity.
        number(QStringLiteral("velocityY"), QObject::tr("Velocity Y (up is negative)"),
               true, true, -1e6, 1e6, 1, 10.0,
               QObject::tr("How fast it is travelling. Setting it replaces the motion"
                           " outright, where an impulse adds to it.")),
        number(QStringLiteral("velocityX"), QObject::tr("Velocity X"),
               true, true, -1e6, 1e6, 1, 10.0,
               QObject::tr("How fast it is travelling. Setting it replaces the motion"
                           " outright, where an impulse adds to it.")),
        // b2Body_GetLinearVelocity, magnitude only -- there is no setter for a
        // speed without a direction.
        number(QStringLiteral("speed"), QObject::tr("Speed"), true, false, 0.0, 1e6, 1, 10.0,
               QObject::tr("How fast it is going, whichever way. Read only -- a speed"
                           " with no direction is not something to set.")),
        // b2Body_GetAngularVelocity / SetAngularVelocity.
        number(QStringLiteral("angularVelocity"), QObject::tr("Angular Velocity (deg/s)"),
               true, true, -1e5, 1e5, 1, 10.0,
               QObject::tr("How fast it turns, and which way. Positive turns the way"
                           " angles grow.")),

        // b2Body_GetPosition / b2Body_SetTransform. Setting one is a
        // teleport: the body arrives without travelling, so it can appear
        // inside something. That is what "move it there" means, and it is the
        // caller's business.
        number(QStringLiteral("positionX"), QObject::tr("Position X"),
               true, true, -1e7, 1e7, 1, 10.0,
               QObject::tr("Moving a body while it runs places it there outright,"
                           " without travelling -- so it can land inside something."
                           " Glide To X arrives the long way instead.")),
        number(QStringLiteral("positionY"), QObject::tr("Position Y (down is positive)"),
               true, true, -1e7, 1e7, 1, 10.0,
               QObject::tr("Moving a body while it runs places it there outright,"
                           " without travelling -- so it can land inside something."
                           " Glide To Y arrives the long way instead.")),
        number(QStringLiteral("angle"), QObject::tr("Angle (deg)"),
               true, true, -1e5, 1e5, 1, 1.0,
               QObject::tr("Which way it faces. Setting it mid-run turns it on the"
                           " spot; Glide To Angle turns it the long way.")),

        // b2Body_SetTargetTransform: the velocity that would arrive there by
        // the end of the step, rather than the arrival itself. Write-only --
        // what it reads back as is the position, above.
        number(QStringLiteral("targetX"), QObject::tr("Glide To X"),
               false, true, -1e7, 1e7, 1, 10.0,
               QObject::tr("Travels there rather than appearing there, so it pushes"
                           " what is in the way. Meant for kinematic bodies, and"
                           " ignored if the move would be slower than the sleep"
                           " threshold.")),
        number(QStringLiteral("targetY"), QObject::tr("Glide To Y"),
               false, true, -1e7, 1e7, 1, 10.0,
               QObject::tr("Travels there rather than appearing there, so it pushes"
                           " what is in the way. Meant for kinematic bodies, and"
                           " ignored if the move would be slower than the sleep"
                           " threshold.")),
        number(QStringLiteral("targetAngle"), QObject::tr("Glide To Angle (deg)"),
               false, true, -1e5, 1e5, 1, 1.0,
               QObject::tr("Turns there rather than appearing turned.")),

        // b2Body_Enable / Disable / IsEnabled.
        flag(QStringLiteral("isEnabled"), QObject::tr("Enabled"), true, true,
             QObject::tr("A disabled body stops colliding and moving entirely.")),
        flag(QStringLiteral("isAwake"), QObject::tr("Awake"), true, true,
             QObject::tr("A sleeping body is left out of the simulation until something"
                         " disturbs it. Waking one by hand costs nothing.")),
        flag(QStringLiteral("enableSleep"), QObject::tr("Allow Sleep"), true, true,
             QObject::tr("Whether it may drop out of the simulation at all. Turn it off"
                         " for something that must keep reacting.")),
        // b2Body_GetSleepThreshold / SetSleepThreshold.
        number(QStringLiteral("sleepThreshold"), QObject::tr("Sleep Below Speed (m/s)"),
               true, true, 0.0, 1000.0, 3, 0.01,
               QObject::tr("Move slower than this for long enough and the body stops"
                           " being simulated until something disturbs it.")),
        flag(QStringLiteral("fixedRotation"), QObject::tr("Fixed Rotation"), true, true,
             QObject::tr("Stops it turning, whatever hits it. What keeps a character"
                         " upright, or a platform level.")),
        flag(QStringLiteral("isBullet"), QObject::tr("Bullet"), true, true,
             QObject::tr("Checks for collisions along the way rather than only where it"
                         " lands, so something fast cannot pass through a wall.")),
        // b2BodyDef::allowFastRotation. Set when the body is made and never
        // after, so it is stored and never read back.
        flag(QStringLiteral("allowFastRotation"), QObject::tr("Allow Fast Rotation"),
             false, false,
             QObject::tr("Lets a small round body spin faster than Box2D's usual safety"
                         " limit, which would otherwise slow it down.")),

        // b2Body_GetType / SetType. The order matches b2BodyType, since the
        // editor stores a Choice as its index.
        choice(QStringLiteral("bodyType"), QObject::tr("Body Type"), true, true,
               { QObject::tr("Static"), QObject::tr("Kinematic"), QObject::tr("Dynamic") },
               QObject::tr("Turning scenery dynamic mid-run is how a shelf gives way."
                           " A body that changes type loses any mass set by hand.")),

        number(QStringLiteral("gravityScale"), QObject::tr("Gravity Scale"),
               true, true, -100.0, 100.0, 2, 0.1,
               QObject::tr("How much of the world's gravity this body feels. Zero floats,"
                           " negative falls upwards.")),
        number(QStringLiteral("linearDamping"), QObject::tr("Linear Damping"),
               true, true, 0.0, 1000.0, 2, 0.1,
               QObject::tr("Slows it as it travels, the way air does. Needs no contact,"
                           " so it is not friction.")),
        number(QStringLiteral("angularDamping"), QObject::tr("Angular Damping"),
               true, true, 0.0, 1000.0, 2, 0.1,
               QObject::tr("The same, for spin: a body left turning gradually stops.")),

        // b2Body_GetMass / GetRotationalInertia, and b2Body_SetMassData to put
        // one back. Normally both come from the shapes and their density, so
        // writing one is an override that a change of shape or type undoes.
        number(QStringLiteral("mass"), QObject::tr("Mass (kg)"), true, true,
               0.0, 1e9, 4, 0.1,
               QObject::tr("Worked out from the shapes and their density. Setting it"
                           " overrides that until a shape or the body type changes.")),
        number(QStringLiteral("rotationalInertia"), QObject::tr("Rotational Inertia (kg·m²)"),
               true, true, 0.0, 1e9, 4, 0.1,
               QObject::tr("How hard it is to start or stop the body spinning.")),
        // b2Body_GetContactData: how many of this body's shapes are touching
        // something right now, counted across all of them.
        number(QStringLiteral("contactCount"), QObject::tr("Contacts"), true, false,
               0.0, 1e6, 0, 1.0,
               QObject::tr("How many contacts this body is in right now, across all its"
                           " shapes. Zero when nothing is touching it.")),
        // b2Body_GetJointCount.
        number(QStringLiteral("jointCount"), QObject::tr("Joints"), true, false,
               0.0, 1e6, 0, 1.0,
               QObject::tr("How many joints hold this body. Drops when a rule breaks one.")),

        // b2Body_ComputeAABB: the box the body currently occupies, shapes and
        // all, in scene coordinates.
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

        // b2Body_EnableContactEvents / EnableHitEvents. The body-wide switches:
        // Box2D has no getter for either, so they read back as they were set.
        flag(QStringLiteral("enableContactEvents"), QObject::tr("Contact Events"), false, true,
             QObject::tr("Switches contact reporting for every shape on this body at once,"
                         " rather than one shape at a time.")),
        flag(QStringLiteral("enableHitEvents"), QObject::tr("Hit Events"), false, true,
             QObject::tr("The same for hard knocks, across the whole body.")),

        // b2Body_GetLocalCenterOfMass, in the body's own frame -- where it
        // balances regardless of where it has got to.
        number(QStringLiteral("localCenterOfMassX"), QObject::tr("Centre of Mass X (local)"),
               true, false, -1e7, 1e7, 1, 10.0,
               QObject::tr("Where the body balances, measured from its own origin rather"
                           " than in the scene.")),
        number(QStringLiteral("localCenterOfMassY"), QObject::tr("Centre of Mass Y (local)"),
               true, false, -1e7, 1e7, 1, 10.0,
               QObject::tr("Where the body balances, measured from its own origin rather"
                           " than in the scene.")),

        // b2Body_GetWorldCenterOfMass. A body turns about this, not about its
        // origin, so it is the point worth watching.
        number(QStringLiteral("centerOfMassX"), QObject::tr("Centre of Mass X"),
               true, false, -1e7, 1e7, 1, 10.0,
               QObject::tr("A body turns about this point, not about its origin. Worked"
                           " out from the shapes and their density.")),
        number(QStringLiteral("centerOfMassY"), QObject::tr("Centre of Mass Y"),
               true, false, -1e7, 1e7, 1, 10.0,
               QObject::tr("A body turns about this point, not about its origin. Worked"
                           " out from the shapes and their density.")),

        // b2Body_ApplyForceToCenter and b2Body_ApplyTorque. Last, because a
        // rule fires for one step and anything applied for one step is nearly
        // nothing.
        number(QStringLiteral("forceY"), QObject::tr("Force Up/Down (one step)"),
               false, true, -1e9, 1e9, 1, 10.0,
               QObject::tr("A force lasts only as long as it is applied, and a rule"
                           " fires for a single step. An impulse is usually what"
                           " is wanted.")),
        number(QStringLiteral("forceX"), QObject::tr("Force Left/Right (one step)"),
               false, true, -1e9, 1e9, 1, 10.0,
               QObject::tr("Sideways, and just as brief. An impulse is usually what"
                           " is wanted.")),
        number(QStringLiteral("torque"), QObject::tr("Torque (one step)"),
               false, true, -1e9, 1e9, 2, 1.0,
               QObject::tr("The turning equivalent, and just as brief. Spin Impulse"
                           " is usually what is wanted.")),
    };

    markStored(&properties, {
        {QStringLiteral("velocityX"), 0.0},
        {QStringLiteral("velocityY"), 0.0},
        {QStringLiteral("angularVelocity"), 0.0},
        {QStringLiteral("linearDamping"), 0.0},
        {QStringLiteral("angularDamping"), 0.0},
        {QStringLiteral("gravityScale"), 1.0},
        {QStringLiteral("fixedRotation"), false},
        {QStringLiteral("isBullet"), false},
        {QStringLiteral("allowFastRotation"), false},
        {QStringLiteral("enableSleep"), true},
        {QStringLiteral("isAwake"), true},
        {QStringLiteral("sleepThreshold"), 0.05},
    }, QObject::tr("Body"));
    return properties;
}

QVector<ActionType> Box2DEngine::bodyActions() const
{
    QVector<ActionType> actions;

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

    // b2ExplosionDef::maskBits -- the same bits a shape filters with, so a
    // blast can be made to reach only some of what is standing near it.
    JointParam mask;
    mask.key = QStringLiteral("maskBits");
    mask.label = QObject::tr("Affects Groups");
    mask.defaultValue = 0.0;
    mask.minValue = 0.0;
    mask.maxValue = 9.007199254740992e15; // every bit a double still counts exactly
    mask.decimals = 0;
    mask.step = 1.0;
    mask.tooltip = QObject::tr("Which collision groups the blast reaches, as the sum of"
                               " their bits. Zero means everything.");

    explode.params = { impulse, radius, falloff, mask };
    actions.append(explode);

    // b2Body_ApplyLinearImpulse at a world point rather than at the centre of
    // mass. Off centre it also spins the body, which is the whole reason to
    // want it -- and the reason it needs somewhere to be applied.
    ActionType push;
    push.id = QStringLiteral("pushAt");
    push.label = QObject::tr("Push at a Point");
    push.description = QObject::tr(
        "Kicks the object at an offset from its centre, so it spins as well as "
        "moves. A kick through the centre is the plain Impulse property.");

    JointParam pushX;
    pushX.key = QStringLiteral("impulseX");
    pushX.label = QObject::tr("Impulse X");
    pushX.defaultValue = 0.0;
    pushX.minValue = -1e6;
    pushX.maxValue = 1e6;
    pushX.decimals = 1;
    pushX.step = 10.0;

    JointParam pushY = pushX;
    pushY.key = QStringLiteral("impulseY");
    pushY.label = QObject::tr("Impulse Y (up is negative)");

    JointParam offsetX;
    offsetX.key = QStringLiteral("offsetX");
    offsetX.label = QObject::tr("Offset X");
    offsetX.defaultValue = 0.0;
    offsetX.minValue = -1e6;
    offsetX.maxValue = 1e6;
    offsetX.decimals = 1;
    offsetX.step = 10.0;
    offsetX.tooltip = QObject::tr("Where the kick lands, measured from the body's centre"
                                  " of mass in scene units. Zero is a straight push.");

    JointParam offsetY = offsetX;
    offsetY.key = QStringLiteral("offsetY");
    offsetY.label = QObject::tr("Offset Y");

    push.params = { pushX, pushY, offsetX, offsetY };
    actions.append(push);

    // b2Body_ApplyForce, at a point rather than through the centre of mass.
    // The push above is an impulse; this is the steady version, and lasts the
    // one step a rule fires for.
    ActionType pushForce;
    pushForce.id = QStringLiteral("pushForceAt");
    pushForce.label = QObject::tr("Force at a Point (one step)");
    pushForce.description = QObject::tr(
        "Pushes with a force at an offset from the centre, for one step. A rule fires "
        "for a single step, so an impulse is usually what is wanted.");
    pushForce.params = push.params;
    actions.append(pushForce);

    // b2Body_ApplyMassFromShapes: puts back what the shapes say the body
    // weighs, undoing a mass or inertia a rule set by hand.
    ActionType resetMass;
    resetMass.id = QStringLiteral("resetMass");
    resetMass.label = QObject::tr("Recalculate Mass");
    resetMass.description = QObject::tr(
        "Works the mass and the moment out again from the shapes and their density, "
        "throwing away anything set by hand.");
    actions.append(resetMass);

    // b2DestroyBody. Not a property with a value, because there is nothing
    // left afterwards to hold one -- and no way back within the same run.
    ActionType remove;
    remove.id = QStringLiteral("removeBody");
    remove.label = QObject::tr("Remove");
    remove.description = QObject::tr(
        "Takes the object out of the world for the rest of the run, along with "
        "any joints attached to it. The scene itself is untouched: stopping "
        "brings it back.");
    actions.append(remove);

    return actions;
}

QVector<ActionType> Box2DEngine::jointActions() const
{
    // b2DestroyJoint. The rope snaps, the hinge shears off -- there is no
    // value that says "no longer connected", so it is an action.
    ActionType breakJoint;
    breakJoint.id = QStringLiteral("breakJoint");
    breakJoint.label = QObject::tr("Break");
    breakJoint.description = QObject::tr(
        "Removes the joint for the rest of the run, letting go of whatever it "
        "was holding. Stopping puts it back.");
    return { breakJoint };
}

PropertyList Box2DEngine::shapeProperties() const
{
    // Everything b2Shape_Set*/Get* reaches on a shape that already exists.
    // Geometry is here too: Box2D can replace a shape's outline in place, so a
    // circle's radius really is a value and not a rebuild.
    PropertyList properties = {
        number(QStringLiteral("density"), QObject::tr("Density"), true, true,
               0.0, 1e6, 2, 0.1,
               QObject::tr("Mass per unit area. The body's own mass is worked out from"
                           " this and the size of its shapes.")),
        number(QStringLiteral("friction"), QObject::tr("Friction"), true, true,
               0.0, 100.0, 2, 0.05,
               QObject::tr("How much it resists sliding. Zero is ice. Both surfaces in a"
                           " contact are combined, so neither decides alone.")),
        number(QStringLiteral("restitution"), QObject::tr("Restitution"), true, true,
               0.0, 100.0, 2, 0.05,
               QObject::tr("How much it bounces: 0 stops dead, 1 gives back all the"
                           " speed. Combined between the two surfaces, so a bouncy ball"
                           " needs no bouncy floor.")),

        // The rest of b2SurfaceMaterial, through b2Shape_SetSurfaceMaterial.
        number(QStringLiteral("rollingResistance"), QObject::tr("Rolling Resistance"),
               true, true, 0.0, 100.0, 2, 0.05,
               QObject::tr("Stops a ball rolling forever.")),
        number(QStringLiteral("tangentSpeed"), QObject::tr("Surface Speed"),
               true, true, -1e6, 1e6, 1, 10.0,
               QObject::tr("The surface drags along what touches it, like a conveyor"
                           " belt. Switchable while running.")),

        // b2Shape_GetCircle / SetCircle. Circles only -- reading it on
        // anything else gives nothing, and writing it does nothing.
        number(QStringLiteral("radius"), QObject::tr("Radius"), true, true,
               0.0, 1e7, 1, 1.0,
               QObject::tr("Circles only. Changing it resizes the collision shape"
                           " where it stands; what is drawn does not follow.")),

        // b2Shape_GetMassData. Its own share of the body's mass.
        number(QStringLiteral("mass"), QObject::tr("Mass (kg)"), true, false,
               0.0, 1e9, 4, 0.1,
               QObject::tr("What this shape alone contributes to the body it belongs"
                           " to -- its area times its density.")),

        // What b2ContactHitEvent carried the last time this shape was hit.
        // The "is hit" event says only that it happened; these say how hard,
        // which is the difference between a tap and a crash.
        number(QStringLiteral("lastHitSpeed"), QObject::tr("Last Hit Speed"), true, false,
               0.0, 1e7, 1, 10.0,
               QObject::tr("How fast the two were closing on the last impact above"
                           " the world's hit threshold. Zero until something hits it.")),
        number(QStringLiteral("lastHitX"), QObject::tr("Last Hit X"), true, false,
               -1e7, 1e7, 1, 10.0,
               QObject::tr("Where the last hard impact landed. Useful for putting a"
                           " mark, a spark or an explosion at the point of contact.")),
        number(QStringLiteral("lastHitY"), QObject::tr("Last Hit Y"), true, false,
               -1e7, 1e7, 1, 10.0,
               QObject::tr("Where the last hard impact landed. Useful for putting a"
                           " mark, a spark or an explosion at the point of contact.")),
        number(QStringLiteral("lastHitNormalX"), QObject::tr("Last Hit Normal X"), true, false,
               -1.0, 1.0, 3, 0.1,
               QObject::tr("Which way the surface faced where it was struck.")),
        number(QStringLiteral("lastHitNormalY"), QObject::tr("Last Hit Normal Y"), true, false,
               -1.0, 1.0, 3, 0.1,
               QObject::tr("Which way the surface faced where it was struck.")),

        // b2Shape_GetMassData, the rest of it: what this shape alone contributes
        // to the body, and where.
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

        // b2Shape_GetAABB: the box it currently occupies.
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

        // b2Shape_GetContactData.
        number(QStringLiteral("contactCount"), QObject::tr("Contacts"), true, false,
               0.0, 1e6, 0, 1.0,
               QObject::tr("How many other shapes this one is touching right now.")),

        // b2Shape_GetMaterial / SetMaterial: a number of your own, which the
        // world's friction and restitution callbacks can sort shapes by.
        number(QStringLiteral("materialId"), QObject::tr("Material Id"), true, true,
               0.0, 1e9, 0, 1.0,
               QObject::tr("A number of your own for grouping surfaces -- ice, rubber, mud."
                           " Box2D does nothing with it unless a callback reads it.")),

        // b2Shape_IsSensor. There is no setter: what a shape is was decided
        // when it was made.
        flag(QStringLiteral("isSensor"), QObject::tr("Sensor"), true, false,
             QObject::tr("A sensor is passed straight through: it reports what overlaps"
                         " it and stops nothing. Decided when the shape is made.")),
        // b2Shape_GetSensorOverlaps. Sensors only, and zero for anything else.
        number(QStringLiteral("sensorOverlapCount"), QObject::tr("Things Inside"),
               true, false, 0.0, 1e6, 0, 1.0,
               QObject::tr("How many shapes are inside this sensor right now. The"
                           " entered and left events say when it changes; this says"
                           " what it is.")),

        // b2Shape_Enable* / Are*Enabled. Turning a stream off mid-run is how a
        // trigger is spent without removing what raised it.
        flag(QStringLiteral("enableContactEvents"), QObject::tr("Contact Events"), true, true,
             QObject::tr("Whether touching and parting are reported to rules. Only"
                         " dynamic and kinematic bodies raise them.")),
        flag(QStringLiteral("enableHitEvents"), QObject::tr("Hit Events"), true, true,
             QObject::tr("Whether hard knocks are reported, with the speed they arrived"
                         " at. Anything gentler than the world's threshold is ignored.")),
        flag(QStringLiteral("enableSensorEvents"), QObject::tr("Sensor Events"), true, true,
             QObject::tr("Whether this shape can be noticed by sensors. Off, it passes"
                         " through them unremarked.")),
        flag(QStringLiteral("enablePreSolveEvents"), QObject::tr("Pre-Solve Events"), true, true,
             QObject::tr("Reports a contact before it is resolved, while there is still"
                         " time to call it off. A one-way platform is the usual reason.")),

        // b2Shape_GetFilter / SetFilter. Bits rather than a number, but a
        // number is what a rule can carry -- so they are shown as the sum.
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
        {QStringLiteral("materialId"), 0.0},
        {QStringLiteral("density"), 1.0},
        {QStringLiteral("friction"), 0.6},
        {QStringLiteral("restitution"), 0.0},
        {QStringLiteral("rollingResistance"), 0.0},
        {QStringLiteral("tangentSpeed"), 0.0},
        {QStringLiteral("isSensor"), false},
    }, QObject::tr("Shape"));
    markStored(&properties, {
        {QStringLiteral("enableContactEvents"), false},
        {QStringLiteral("enableHitEvents"), false},
        {QStringLiteral("enableSensorEvents"), false},
        {QStringLiteral("enablePreSolveEvents"), false},
        {QStringLiteral("categoryBits"), 1.0},
        // Every bit a double still counts exactly: collides with everything.
        {QStringLiteral("maskBits"), 9007199254740991.0},
        {QStringLiteral("groupIndex"), 0.0},
    }, QObject::tr("Collision"));
    markRole(&properties, QStringLiteral("isSensor"), PropertyRole::Sensor);
    markRole(&properties, QStringLiteral("density"), PropertyRole::Density);
    return properties;
}

PropertyList Box2DEngine::worldProperties() const
{
    // What b2World_Set*/Enable* can still change once the world exists, plus
    // the two the world is built with. The rest of b2WorldDef -- the worker
    // count, the mixing callbacks -- is not offered at all.
    PropertyList properties = {
        // b2World_SetGravity. In m/s^2, the same units the world settings use.
        number(QStringLiteral("gravityX"), QObject::tr("Gravity X (m/s²)"), true, true,
               -1000.0, 1000.0, 2, 0.5,
               QObject::tr("Sideways gravity. Usually zero; tilt it to lean the whole"
                           " world one way.")),
        number(QStringLiteral("gravityY"), QObject::tr("Gravity Y (m/s²)"), true, true,
               -1000.0, 1000.0, 2, 0.5,
               QObject::tr("Positive is down. Turning it negative mid-run is how"
                           " everything loose ends up on the ceiling.")),

        number(QStringLiteral("restitutionThreshold"),
               QObject::tr("Restitution Threshold (m/s)"), true, true, 0.0, 1000.0, 2, 0.1,
               QObject::tr("Below this closing speed nothing bounces, however"
                           " bouncy it is.")),
        number(QStringLiteral("hitEventThreshold"), QObject::tr("Hit Event Threshold (m/s)"),
               true, true, 0.0, 1000.0, 2, 0.1,
               QObject::tr("How hard a knock has to be before it counts as a hit. Below"
                           " this the shapes still touch, but nothing is reported.")),
        number(QStringLiteral("maximumLinearSpeed"), QObject::tr("Max Speed (m/s)"),
               true, true, 0.0, 100000.0, 1, 10.0,
               QObject::tr("Nothing in the world may travel faster than this, whatever"
                           " it is hit with.")),

        // b2World_SetContactTuning -- one call for all three, and no getters,
        // so these read back what the world was last told.
        number(QStringLiteral("contactHertz"), QObject::tr("Contact Stiffness (Hz)"),
               true, true, 0.0, 1000.0, 1, 1.0,
               QObject::tr("How firmly contacts are held. Higher is less forgiving, and"
                           " cannot exceed a quarter of the sub-step rate.")),
        number(QStringLiteral("contactDampingRatio"), QObject::tr("Contact Damping"),
               true, true, 0.0, 100.0, 2, 0.5,
               QObject::tr("How quickly a contact stops springing back. Low values leave"
                           " stacks jittering.")),
        number(QStringLiteral("maxContactPushSpeed"), QObject::tr("Max Push Speed (m/s)"),
               true, true, 0.0, 1000.0, 2, 0.1,
               QObject::tr("How fast overlapping shapes are pushed apart. Too high and"
                           " anything caught inside another is flung out.")),

        flag(QStringLiteral("enableSleep"), QObject::tr("Allow Sleeping"), true, true,
             QObject::tr("Whether any body may drop out of the simulation once it has"
                         " settled. Off, everything keeps being solved.")),
        flag(QStringLiteral("enableContinuous"), QObject::tr("Continuous Collision"), true, true,
             QObject::tr("Stops fast bodies tunnelling through thin ones.")),
        flag(QStringLiteral("enableWarmStarting"), QObject::tr("Warm Starting"), true, true,
             QObject::tr("Lets the solver start from last step's answer. Off is much"
                         " worse at stacking, and is mostly of interest for seeing"
                         " what it does.")),
        flag(QStringLiteral("enableSpeculative"), QObject::tr("Speculative Contacts"), true, true,
             QObject::tr("Contacts are reported slightly before shapes visibly touch."
                         " Off, they touch first and are resolved after.")),

        // b2World_GetAwakeBodyCount and b2World_GetCounters: what the world
        // currently costs, as numbers a rule can watch.
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
        number(QStringLiteral("islandCount"), QObject::tr("Islands"), true, false,
               0.0, 1e9, 0, 1.0,
               QObject::tr("Groups of bodies the solver is holding together as one. They"
                           " fall apart as things stop touching.")),
        number(QStringLiteral("treeHeight"), QObject::tr("Broadphase Depth"), true, false,
               0.0, 1e6, 0, 1.0,
               QObject::tr("How deep the tree Box2D searches for possible collisions has"
                           " grown. A rough measure of how crowded the scene is.")),

        // b2World_GetProfile: where the last step went, in milliseconds.
        number(QStringLiteral("stepMilliseconds"), QObject::tr("Step Time (ms)"), true, false,
               0.0, 1e6, 3, 0.1,
               QObject::tr("How long the last step took. Everything below is part of it.")),
        number(QStringLiteral("collideMilliseconds"), QObject::tr("Collide Time (ms)"),
               true, false, 0.0, 1e6, 3, 0.1,
               QObject::tr("The part of it spent finding what touches what.")),
        number(QStringLiteral("solveMilliseconds"), QObject::tr("Solve Time (ms)"), true, false,
               0.0, 1e6, 3, 0.1,
               QObject::tr("The part spent working out where everything ends up.")),

        // An argument to every b2World_Step rather than a field of the world,
        // so it is stored and set when the world is made, never after.
        number(QStringLiteral("subStepCount"), QObject::tr("Solver Sub-Steps"), false, false,
               1.0, 64.0, 0, 1.0,
               QObject::tr("How many times the solver relaxes the constraints within one"
                           " step. More holds a tall stack together; fewer is faster and"
                           " springier. Box2D's samples use 4.")),
    };

    markStored(&properties, {
        {QStringLiteral("gravityX"), 0.0},
        {QStringLiteral("gravityY"), 9.81},
    }, QObject::tr("World"));
    markStored(&properties, {
        {QStringLiteral("restitutionThreshold"), 1.0},
        {QStringLiteral("hitEventThreshold"), 1.0},
        {QStringLiteral("maximumLinearSpeed"), 400.0},
        {QStringLiteral("contactHertz"), 30.0},
        {QStringLiteral("contactDampingRatio"), 10.0},
        {QStringLiteral("maxContactPushSpeed"), 3.0},
        {QStringLiteral("subStepCount"), 4},
        {QStringLiteral("enableSleep"), true},
        {QStringLiteral("enableContinuous"), true},
    }, QObject::tr("Solver"));
    return properties;
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
                   -1e5, 1e5, 1, 1.0,
                   QObject::tr("How far the hinge has turned from where it started."
                               " What a limit is measured against.")),
            number(QStringLiteral("motorSpeed"), QObject::tr("Motor Speed (deg/s)"), true, false,
                   -1e5, 1e5, 1, 10.0,
                   QObject::tr("The turn rate the motor is aiming for -- not necessarily"
                               " the one it is achieving.")),
            number(QStringLiteral("motorTorque"), QObject::tr("Motor Torque"), true, false,
                   -1e9, 1e9, 2, 1.0,
                   QObject::tr("What the motor is actually pulling this step. Sitting at"
                               " its maximum means it is stalled against something.")),
        };
        appendJointSwitches(&result, true, true, true);
    }
    if (typeId == QLatin1String("prismatic")) {
        result = {
            number(QStringLiteral("translation"), QObject::tr("Translation"), true, false,
                   -1e7, 1e7, 1, 10.0,
                   QObject::tr("How far along its axis the joint has slid from where it"
                               " started. What a limit is measured against.")),
            number(QStringLiteral("speed"), QObject::tr("Speed"), true, false,
                   -1e6, 1e6, 1, 10.0,
                   QObject::tr("How fast it is sliding right now.")),
            number(QStringLiteral("motorSpeed"), QObject::tr("Motor Speed"), true, false,
                   -1e6, 1e6, 1, 10.0,
                   QObject::tr("The sliding rate the motor is aiming for -- not"
                               " necessarily the one it is achieving.")),
            number(QStringLiteral("motorForce"), QObject::tr("Motor Force"), true, false,
                   -1e9, 1e9, 2, 1.0,
                   QObject::tr("What the motor is actually pushing with this step."
                               " Sitting at its maximum means it is stalled.")),
        };
        appendJointSwitches(&result, true, true, true);
    }
    if (typeId == QLatin1String("distance")) {
        result = {
            number(QStringLiteral("length"), QObject::tr("Rest Length"), true, false,
                   0.0, 1e7, 1, 10.0,
                   QObject::tr("The distance the joint is trying to hold. Drive this to"
                               " use the joint as a ram.")),
            // b2DistanceJoint_GetCurrentLength -- how far apart the anchors
            // are now, which is not the length the joint is holding for.
            number(QStringLiteral("currentLength"), QObject::tr("Current Length"), true, false,
                   0.0, 1e7, 1, 10.0,
                   QObject::tr("How far apart the two ends are right now. A rope under"
                               " load reads longer than its rest length.")),
            number(QStringLiteral("motorSpeed"), QObject::tr("Motor Speed"), true, false,
                   -1e6, 1e6, 1, 10.0,
                   QObject::tr("How fast the motor is trying to lengthen or shorten the"
                               " joint. Only acts while the spring is on.")),
            number(QStringLiteral("motorForce"), QObject::tr("Motor Force"), true, false,
                   -1e9, 1e9, 2, 1.0,
                   QObject::tr("What the motor is actually pulling with this step.")),
        };
        appendJointSwitches(&result, true, true, true);
    }
    if (typeId == QLatin1String("wheel")) {
        result = {
            number(QStringLiteral("motorSpeed"), QObject::tr("Motor Speed (deg/s)"), true, false,
                   -1e5, 1e5, 1, 10.0,
                   QObject::tr("How fast the motor is trying to spin the wheel about"
                               " its axle.")),
            number(QStringLiteral("motorTorque"), QObject::tr("Motor Torque"), true, false,
                   -1e9, 1e9, 2, 1.0,
                   QObject::tr("What the motor is actually turning it with this step.")),
        };
        appendJointSwitches(&result, true, true, true);
    }
    if (typeId == QLatin1String("mouse")) {
        // b2MouseJoint_GetTarget, so a rule can follow where the joint is
        // currently leading as well as change it.
        result = {
            number(QStringLiteral("targetX"), QObject::tr("Target X"), true, true,
                   -1e7, 1e7, 1, 10.0,
                   QObject::tr("The point the joint is dragging the body towards. Move"
                               " it and the body follows, softly.")),
            number(QStringLiteral("targetY"), QObject::tr("Target Y"), true, true,
                   -1e7, 1e7, 1, 10.0,
                   QObject::tr("The point the joint is dragging the body towards. Move"
                               " it and the body follows, softly.")),
        };
    }

    // Every joint reports the load it is carrying and how far it is being
    // pulled out of shape, whatever kind it is -- so these are appended to
    // whatever that type measures of its own.
    result.push_back(number(QStringLiteral("constraintForce"),
                            QObject::tr("Constraint Force (N)"), true, false,
                            0.0, 1e12, 2, 1.0,
                            QObject::tr("The load the joint is carrying. Watch it to"
                                        " break something under strain.")));
    result.push_back(number(QStringLiteral("constraintTorque"),
                            QObject::tr("Constraint Torque (N·m)"), true, false,
                            -1e12, 1e12, 2, 1.0,
                            QObject::tr("The twisting load on the joint, as the force"
                                        " above is the pulling one.")));
    // b2Joint_GetLinearSeparation / GetAngularSeparation: the error the solver
    // has not managed to remove. A joint being torn apart shows here first.
    result.push_back(number(QStringLiteral("linearSeparation"),
                            QObject::tr("Linear Separation"), true, false,
                            0.0, 1e7, 1, 1.0,
                            QObject::tr("How far the joint is from where it should be"
                                        " holding, in scene units. Climbs as the load"
                                        " on it grows.")));
    result.push_back(number(QStringLiteral("angularSeparation"),
                            QObject::tr("Angular Separation (deg)"), true, false,
                            -1e5, 1e5, 2, 1.0,
                            QObject::tr("The angle the solver has not managed to remove."
                                        " A joint being twisted apart shows here.")));
    // b2Joint_GetCollideConnected / SetCollideConnected. Belongs to the joint
    // rather than to any one kind of it, and can be changed while running.
    result.push_back(flag(QStringLiteral("collideConnected"),
                          QObject::tr("Bodies Collide"), true, true,
                          QObject::tr("While off, the two bodies pass through each other."
                                      " If one of them is scenery, the other falls"
                                      " straight through it.")));

    // b2Joint_SetLocalAnchorA/B and SetReferenceAngle. Where a joint holds is
    // as changeable as anything else about it -- a hinge can slide along the
    // thing it is hinged to. Shown in scene coordinates, as the anchors are.
    const QString moved =
        QObject::tr("Moving it mid-run moves where the joint holds. The handle"
                    " drawn on the canvas belongs to the scene and stays where"
                    " it was put.");
    result.push_back(number(QStringLiteral("anchorAX"), QObject::tr("Anchor A X"),
                            true, true, -1e7, 1e7, 1, 10.0, moved));
    result.push_back(number(QStringLiteral("anchorAY"), QObject::tr("Anchor A Y"),
                            true, true, -1e7, 1e7, 1, 10.0, moved));
    result.push_back(number(QStringLiteral("anchorBX"), QObject::tr("Anchor B X"),
                            true, true, -1e7, 1e7, 1, 10.0, moved));
    result.push_back(number(QStringLiteral("anchorBY"), QObject::tr("Anchor B Y"),
                            true, true, -1e7, 1e7, 1, 10.0, moved));
    result.push_back(number(QStringLiteral("referenceAngleNow"),
                            QObject::tr("Reference Angle Now (deg)"), true, true,
                            -360.0, 360.0, 2, 1.0,
                            QObject::tr("The body-B-minus-body-A angle the joint counts"
                                        " as zero. Re-zeroing it while running moves"
                                        " the limit and the spring with it.")));

    // b2Joint_SetLocalAxisA, for the two types that travel along one. As an
    // angle rather than a pair of components, which is how the editor's own
    // axis handle is expressed.
    if (typeId == QLatin1String("prismatic") || typeId == QLatin1String("wheel")) {
        result.push_back(number(QStringLiteral("axisAngle"), QObject::tr("Axis Angle (deg)"),
                                true, true, -360.0, 360.0, 2, 1.0,
                                QObject::tr("Which way the joint slides, in scene degrees."
                                            " As with the anchors, the handle drawn on the"
                                            " canvas does not follow.")));
    }
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
        // b2World_SetPreSolveCallback. A step earlier than contactBegin,
        // which is the whole of the difference.
        {QStringLiteral("preSolve"), QObject::tr("is about to touch"),
         QObject::tr("Raised inside the step, before the collision is worked out "
                     "-- one step earlier than \"begins contact\". The shape needs "
                     "Pre-Solve Events on.")},
    };
}

QVector<EventType> Box2DEngine::bodyEvents() const
{
    // b2World_GetBodyEvents. A body does not collide -- its shapes do -- so
    // there is nothing here about touching. What it does report is whether the
    // solver moved the body this step, and whether that was the step it
    // settled on.
    return {
        {QStringLiteral("bodyMoved"), QObject::tr("starts moving"),
         QObject::tr("Raised while the simulation is carrying the body along, so a "
                     "rule watching for it fires as it sets off."), false},
        {QStringLiteral("bodyFellAsleep"), QObject::tr("comes to rest"),
         QObject::tr("Raised on the step a body stops being simulated, having moved "
                     "slower than its sleep threshold for long enough."), false},
    };
}

} // namespace physics
