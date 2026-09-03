#include "Box2DEngine.h"

#include <QColor>
#include <QObject>
#include <QSet>

// Box2D's joint catalogue, as data.
//
// Everything the editor shows about a joint -- which types exist, what they're
// called, which parameters they have, their units, ranges and defaults, how
// many anchors to ask for, how to draw one -- is declared here and nowhere
// else. Adding a joint type or renaming a parameter is a change to this file
// alone.
//
// Every field of every b2*JointDef appears below, keyed by Box2D's own field
// name, plus the two constraint-tuning knobs that b2Joint_SetConstraintTuning
// exposes on all of them. Defaults match b2Default*JointDef, so a joint left
// alone here behaves exactly as one made straight from the API -- and the
// property table's bold "changed from default" marking means what it says.
//
// Two conversions separate what is shown from what Box2D stores, and they are
// the only liberties taken: angles are shown in degrees where Box2D holds
// radians, and lengths in scene units where Box2D holds metres. Both are
// undone in Box2DJoints.cpp. Fields Box2D fills in itself -- bodyIdA/B, the
// local anchors, localAxisA, userData -- are not parameters: the editor sets
// them from the joint's bodies, its anchor handles and its axis.

namespace physics {

namespace {

// Sections are headings inside the joint's one table, not tabs: a joint is
// one thing, and splitting its spring from its limit from its motor across
// tabs would mean hunting through them to set up a single hinge. The editor
// turns each change of section into a header row.
QString jointSection()  { return QObject::tr("Joint"); }
QString springSection() { return QObject::tr("Spring"); }
QString limitSection()  { return QObject::tr("Limit"); }
QString motorSection()  { return QObject::tr("Motor"); }
QString solverSection() { return QObject::tr("Solver"); }

// Which parameters Box2D can change on a joint that already exists. Kept as
// one list rather than a flag threaded through every call below: it is a fact
// about the Box2D API, and reads better as the API's own answer to "what has a
// setter?" than as an argument repeated forty times.
bool isLiveSettable(const QString &key)
{
    static const QSet<QString> settable {
        QStringLiteral("enableSpring"),  QStringLiteral("hertz"),
        QStringLiteral("dampingRatio"),  QStringLiteral("enableLimit"),
        QStringLiteral("lowerAngle"),    QStringLiteral("upperAngle"),
        QStringLiteral("lowerTranslation"), QStringLiteral("upperTranslation"),
        QStringLiteral("minLength"),     QStringLiteral("maxLength"),
        QStringLiteral("length"),        QStringLiteral("enableMotor"),
        QStringLiteral("motorSpeed"),    QStringLiteral("maxMotorForce"),
        QStringLiteral("maxMotorTorque"), QStringLiteral("targetAngle"),
        QStringLiteral("targetTranslation"),
        QStringLiteral("maxForce"),      QStringLiteral("maxTorque"),
        QStringLiteral("correctionFactor"),
        QStringLiteral("linearOffsetX"), QStringLiteral("linearOffsetY"),
        QStringLiteral("angularOffset"),
        QStringLiteral("targetX"),       QStringLiteral("targetY"),
        QStringLiteral("linearHertz"),   QStringLiteral("angularHertz"),
        QStringLiteral("linearDampingRatio"), QStringLiteral("angularDampingRatio"),
        // b2Joint_SetConstraintTuning takes these on a joint that already
        // exists, so they are as live as anything else here even though no
        // b2*JointDef carries them.
        QStringLiteral("constraintHertz"), QStringLiteral("constraintDampingRatio"),
    };
    return settable.contains(key);
}

JointParam boolParam(const QString &key, const QString &label, const QString &section,
                     bool defaultValue, const QString &tooltip)
{
    JointParam p;
    p.key = key;
    p.label = label;
    p.section = section;
    p.type = ParamType::Bool;
    p.defaultValue = defaultValue;
    p.tooltip = tooltip;
    p.liveSettable = isLiveSettable(key);
    return p;
}

JointParam realParam(const QString &key, const QString &label, const QString &section,
                     qreal defaultValue, qreal minValue, qreal maxValue,
                     int decimals, qreal step, const QString &tooltip)
{
    JointParam p;
    p.key = key;
    p.label = label;
    p.section = section;
    p.type = ParamType::Real;
    p.defaultValue = defaultValue;
    p.minValue = minValue;
    p.maxValue = maxValue;
    p.decimals = decimals;
    p.step = step;
    p.tooltip = tooltip;
    p.liveSettable = isLiveSettable(key);
    return p;
}

// Shorthands for the three shapes of number that keep recurring. They set the
// range and precision; the caller still gives Box2D's own default, because
// that is what differs between joint types.

JointParam hertzParam(const QString &key, const QString &label, const QString &section,
                      qreal defaultValue, const QString &tooltip)
{
    return realParam(key, label, section, defaultValue, 0.0, 1000.0, 2, 0.5, tooltip);
}

JointParam ratioParam(const QString &key, const QString &label, const QString &section,
                      qreal defaultValue, const QString &tooltip)
{
    return realParam(key, label, section, defaultValue, 0.0, 100.0, 2, 0.05, tooltip);
}

JointParam angleParam(const QString &key, const QString &label, const QString &section,
                      qreal defaultValue, const QString &tooltip)
{
    // Box2D clamps revolute limits to +/-0.99*pi; the wider range here is for
    // reference and target angles, which have no such bound.
    return realParam(key, label, section, defaultValue, -360.0, 360.0, 2, 1.0, tooltip);
}

JointParam lengthParam(const QString &key, const QString &label, const QString &section,
                       qreal defaultValue, const QString &tooltip)
{
    return realParam(key, label, section, defaultValue, -1e6, 1e6, 1, 1.0, tooltip);
}

JointParam forceParam(const QString &key, const QString &label, const QString &section,
                      qreal defaultValue, const QString &tooltip)
{
    return realParam(key, label, section, defaultValue, 0.0, 1e9, 2, 10.0, tooltip);
}

// The constraint softness every joint has, whatever its type. Not part of any
// b2*JointDef -- Box2D applies these through b2Joint_SetConstraintTuning after
// the joint exists -- but they belong to the joint just as much as the rest.
// The events a joint with a limit can raise. Edge-triggered by the engine:
// raised on arriving at a limit, not repeated for every step spent sitting
// against it -- a rule that flips a motor must fire once, not sixty times a
// second.
QVector<EventType> limitEvents()
{
    return {
        {QStringLiteral("limitLower"), QObject::tr("Lower limit reached"),
         QObject::tr("Raised when the joint arrives at its lower limit.")},
        {QStringLiteral("limitUpper"), QObject::tr("Upper limit reached"),
         QObject::tr("Raised when the joint arrives at its upper limit.")},
        {QStringLiteral("limitEither"), QObject::tr("Either limit reached"),
         QObject::tr("Raised at whichever limit the joint arrives at.")},
    };
}

QVector<JointParam> solverBlock()
{
    const QString section = solverSection();
    return {
        hertzParam(QStringLiteral("constraintHertz"), QObject::tr("Constraint Hertz"), section,
                   60.0, QObject::tr("How stiffly the solver corrects drift in this joint."
                                     " Box2D's default is 60 Hz.")),
        ratioParam(QStringLiteral("constraintDampingRatio"), QObject::tr("Constraint Damping"),
                   section, 2.0,
                   QObject::tr("Damping applied to that correction. Box2D's default is 2.")),
    };
}

void append(QVector<JointParam> *into, const QVector<JointParam> &block)
{
    for (const JointParam &param : block)
        into->append(param);
}

} // namespace

QVector<JointType> Box2DEngine::jointTypes() const
{
    QVector<JointType> types;

    // --- revolute: a hinge (b2RevoluteJointDef) ---------------------------
    {
        JointType t;
        t.id = QStringLiteral("revolute");
        t.color = QColor(0xE8, 0xC4, 0x6A); // amber -- the hinge, and the one you meet first
        t.label = QObject::tr("Revolute (hinge)");
        t.description = QObject::tr("Pins two bodies together at a point and lets them rotate"
                                    " about it.");
        t.anchorCount = 1;
        t.visual = JointVisual::Pivot;

        t.params.append(angleParam(QStringLiteral("referenceAngle"),
                                   QObject::tr("Reference Angle (deg)"), jointSection(), 0.0,
                                   QObject::tr("The body-B-minus-body-A angle that counts as zero"
                                               " for the limit and the spring.")));
        t.params.append(realParam(QStringLiteral("drawSize"), QObject::tr("Draw Size"),
                                  jointSection(), 0.25, 0.0, 1000.0, 2, 0.05,
                                  QObject::tr("Size Box2D uses when it debug-draws this joint.")));

        t.params.append(boolParam(QStringLiteral("enableSpring"), QObject::tr("Enabled"),
                                  springSection(), false,
                                  QObject::tr("Drive the hinge towards its target angle instead of"
                                              " letting it swing freely.")));
        t.params.append(angleParam(QStringLiteral("targetAngle"), QObject::tr("Target Angle (deg)"),
                                   springSection(), 0.0,
                                   QObject::tr("The angle the spring pulls towards.")));
        t.params.append(hertzParam(QStringLiteral("hertz"), QObject::tr("Frequency (Hz)"),
                                   springSection(), 0.0,
                                   QObject::tr("Spring stiffness in oscillations per second."
                                               " Higher is stiffer.")));
        t.params.append(ratioParam(QStringLiteral("dampingRatio"), QObject::tr("Damping Ratio"),
                                   springSection(), 0.0,
                                   QObject::tr("How quickly the spring settles. 0 oscillates"
                                               " forever, 1 stops without overshooting.")));

        t.params.append(boolParam(QStringLiteral("enableLimit"), QObject::tr("Enabled"),
                                  limitSection(), false,
                                  QObject::tr("Stop the hinge turning past the angles below.")));
        t.params.append(angleParam(QStringLiteral("lowerAngle"), QObject::tr("Lower Angle (deg)"),
                                   limitSection(), 0.0,
                                   QObject::tr("Measured from the reference angle."
                                               " Box2D clamps this to -178.2 deg.")));
        t.params.append(angleParam(QStringLiteral("upperAngle"), QObject::tr("Upper Angle (deg)"),
                                   limitSection(), 0.0,
                                   QObject::tr("Measured from the reference angle."
                                               " Box2D clamps this to 178.2 deg.")));

        t.params.append(boolParam(QStringLiteral("enableMotor"), QObject::tr("Enabled"),
                                  motorSection(), false,
                                  QObject::tr("Drive the hinge rather than letting it turn"
                                              " freely.")));
        t.params.append(forceParam(QStringLiteral("maxMotorTorque"),
                                   QObject::tr("Max Motor Torque (N·m)"), motorSection(), 0.0,
                                   QObject::tr("The most torque the motor may apply.")));
        t.params.append(realParam(QStringLiteral("motorSpeed"), QObject::tr("Motor Speed (deg/s)"),
                                  motorSection(), 0.0, -100000.0, 100000.0, 2, 10.0,
                                  QObject::tr("Target rotation speed.")));

        append(&t.params, solverBlock());
        t.events = limitEvents();
        types.append(t);
    }

    // --- distance: a rod, rope or spring (b2DistanceJointDef) -------------
    {
        JointType t;
        t.id = QStringLiteral("distance");
        t.color = QColor(0x6A, 0xB0, 0xE8); // blue -- a rod or rope holding a length
        t.label = QObject::tr("Distance (rod)");
        t.description = QObject::tr("Holds two points a fixed distance apart. With a spring it"
                                    " behaves like a shock absorber, with a limit like a rope.");
        t.anchorCount = 2;
        t.visual = JointVisual::Segment;

        t.params.append(realParam(QStringLiteral("length"), QObject::tr("Length"), jointSection(),
                                  0.0, 0.0, 1e6, 1, 1.0,
                                  QObject::tr("Rest length in scene units. Zero means whatever the"
                                              " anchors are apart when the joint is created.")));

        t.params.append(boolParam(QStringLiteral("enableSpring"), QObject::tr("Enabled"),
                                  springSection(), false,
                                  QObject::tr("Let the rod flex. While off it is rigid and"
                                              " overrides both the limit and the motor.")));
        t.params.append(hertzParam(QStringLiteral("hertz"), QObject::tr("Frequency (Hz)"),
                                   springSection(), 0.0,
                                   QObject::tr("Spring stiffness in oscillations per second.")));
        t.params.append(ratioParam(QStringLiteral("dampingRatio"), QObject::tr("Damping Ratio"),
                                   springSection(), 0.0,
                                   QObject::tr("How quickly the spring settles.")));

        t.params.append(boolParam(QStringLiteral("enableLimit"), QObject::tr("Enabled"),
                                  limitSection(), false,
                                  QObject::tr("Keep the length between the bounds below."
                                              " Only has an effect while the spring is on.")));
        t.params.append(realParam(QStringLiteral("minLength"), QObject::tr("Min Length"),
                                  limitSection(), 0.0, 0.0, 1e6, 1, 1.0,
                                  QObject::tr("Shortest allowed length, in scene units.")));
        t.params.append(realParam(QStringLiteral("maxLength"), QObject::tr("Max Length"),
                                  limitSection(), 0.0, 0.0, 1e6, 1, 1.0,
                                  QObject::tr("Longest allowed length, in scene units."
                                              " Zero means unbounded, which is Box2D's default.")));

        t.params.append(boolParam(QStringLiteral("enableMotor"), QObject::tr("Enabled"),
                                  motorSection(), false,
                                  QObject::tr("Drive the length rather than letting it settle.")));
        t.params.append(forceParam(QStringLiteral("maxMotorForce"),
                                   QObject::tr("Max Motor Force (N)"), motorSection(), 0.0,
                                   QObject::tr("The most force the motor may apply.")));
        t.params.append(lengthParam(QStringLiteral("motorSpeed"), QObject::tr("Motor Speed"),
                                    motorSection(), 0.0,
                                    QObject::tr("Target rate of change of length, in scene units"
                                                " per second.")));

        append(&t.params, solverBlock());
        t.events = limitEvents();
        types.append(t);
    }

    // --- weld: hold two bodies rigidly (b2WeldJointDef) -------------------
    {
        JointType t;
        t.id = QStringLiteral("weld");
        t.color = QColor(0xB4, 0x8A, 0xE8); // violet -- rigid, nothing moves
        t.label = QObject::tr("Weld");
        t.description = QObject::tr("Fixes two bodies together. Give it a frequency to make the"
                                    " join slightly soft instead of rigid.");
        t.anchorCount = 1;
        t.visual = JointVisual::Rigid;

        t.params.append(angleParam(QStringLiteral("referenceAngle"),
                                   QObject::tr("Reference Angle (deg)"), jointSection(), 0.0,
                                   QObject::tr("The body-B-minus-body-A angle the weld holds.")));

        t.params.append(hertzParam(QStringLiteral("linearHertz"),
                                   QObject::tr("Linear Frequency (Hz)"), springSection(), 0.0,
                                   QObject::tr("Zero is perfectly rigid; above zero lets the bodies"
                                               " shift slightly against each other.")));
        t.params.append(ratioParam(QStringLiteral("linearDampingRatio"),
                                   QObject::tr("Linear Damping Ratio"), springSection(), 0.0,
                                   QObject::tr("Use 1 for critical damping.")));
        t.params.append(hertzParam(QStringLiteral("angularHertz"),
                                   QObject::tr("Angular Frequency (Hz)"), springSection(), 0.0,
                                   QObject::tr("Zero is perfectly rigid; above zero lets the bodies"
                                               " rotate slightly against each other.")));
        t.params.append(ratioParam(QStringLiteral("angularDampingRatio"),
                                   QObject::tr("Angular Damping Ratio"), springSection(), 0.0,
                                   QObject::tr("Use 1 for critical damping.")));

        append(&t.params, solverBlock());
        types.append(t);
    }

    // --- prismatic: a slider (b2PrismaticJointDef) ------------------------
    {
        JointType t;
        t.id = QStringLiteral("prismatic");
        t.color = QColor(0x6A, 0xD1, 0xA8); // green -- travel along a line
        t.label = QObject::tr("Prismatic (slider)");
        t.description = QObject::tr("Lets two bodies slide along one direction and nothing else.");
        // b2PrismaticJointDef carries localAnchorA and localAnchorB, and they
        // are independent points -- one on each body.
        t.anchorCount = 2;
        t.needsAxis = true;
        t.visual = JointVisual::Axis;
        // b2DefaultPrismaticJointDef sets localAxisA to (1, 0).
        t.defaultAxisDegrees = 0.0;

        t.params.append(angleParam(QStringLiteral("referenceAngle"),
                                   QObject::tr("Reference Angle (deg)"), jointSection(), 0.0,
                                   QObject::tr("The body-B-minus-body-A angle the slider holds.")));

        t.params.append(boolParam(QStringLiteral("enableSpring"), QObject::tr("Enabled"),
                                  springSection(), false,
                                  QObject::tr("Drive the slider towards its target translation.")));
        t.params.append(lengthParam(QStringLiteral("targetTranslation"),
                                    QObject::tr("Target Translation"), springSection(), 0.0,
                                    QObject::tr("Where along the axis the spring pulls towards,"
                                                " in scene units.")));
        t.params.append(hertzParam(QStringLiteral("hertz"), QObject::tr("Frequency (Hz)"),
                                   springSection(), 0.0,
                                   QObject::tr("Spring stiffness in oscillations per second.")));
        t.params.append(ratioParam(QStringLiteral("dampingRatio"), QObject::tr("Damping Ratio"),
                                   springSection(), 0.0,
                                   QObject::tr("How quickly the spring settles.")));

        t.params.append(boolParam(QStringLiteral("enableLimit"), QObject::tr("Enabled"),
                                  limitSection(), false,
                                  QObject::tr("Stop the slider travelling past the bounds"
                                              " below.")));
        t.params.append(lengthParam(QStringLiteral("lowerTranslation"),
                                    QObject::tr("Lower Translation"), limitSection(), 0.0,
                                    QObject::tr("How far back along the axis, in scene units.")));
        t.params.append(lengthParam(QStringLiteral("upperTranslation"),
                                    QObject::tr("Upper Translation"), limitSection(), 0.0,
                                    QObject::tr("How far forward along the axis, in scene"
                                                " units.")));

        t.params.append(boolParam(QStringLiteral("enableMotor"), QObject::tr("Enabled"),
                                  motorSection(), false,
                                  QObject::tr("Drive the slider rather than letting it move"
                                              " freely.")));
        t.params.append(forceParam(QStringLiteral("maxMotorForce"),
                                   QObject::tr("Max Motor Force (N)"), motorSection(), 0.0,
                                   QObject::tr("The most force the motor may apply.")));
        t.params.append(lengthParam(QStringLiteral("motorSpeed"), QObject::tr("Motor Speed"),
                                    motorSection(), 0.0,
                                    QObject::tr("Target speed along the axis, in scene units per"
                                                " second.")));

        append(&t.params, solverBlock());
        t.events = limitEvents();
        types.append(t);
    }

    // --- wheel: a suspension (b2WheelJointDef) ----------------------------
    {
        JointType t;
        t.id = QStringLiteral("wheel");
        t.color = QColor(0xE8, 0x8A, 0x6A); // orange -- suspension
        t.label = QObject::tr("Wheel (suspension)");
        t.description = QObject::tr("Lets one body spin freely while sliding along an axis of"
                                    " another -- a wheel on a suspension arm.");
        t.anchorCount = 1;
        t.needsAxis = true;
        t.visual = JointVisual::Axis;
        // b2DefaultWheelJointDef sets localAxisA.y to 1, which is straight
        // down in this canvas -- 90 degrees.
        t.defaultAxisDegrees = 90.0;

        // The one type Box2D ships with its spring already on.
        t.params.append(boolParam(QStringLiteral("enableSpring"), QObject::tr("Enabled"),
                                  springSection(), true,
                                  QObject::tr("Suspend body B along the axis instead of holding it"
                                              " rigidly.")));
        t.params.append(hertzParam(QStringLiteral("hertz"), QObject::tr("Frequency (Hz)"),
                                   springSection(), 1.0,
                                   QObject::tr("Suspension stiffness in oscillations per"
                                               " second.")));
        t.params.append(ratioParam(QStringLiteral("dampingRatio"), QObject::tr("Damping Ratio"),
                                   springSection(), 0.7,
                                   QObject::tr("How quickly the suspension settles.")));

        t.params.append(boolParam(QStringLiteral("enableLimit"), QObject::tr("Enabled"),
                                  limitSection(), false,
                                  QObject::tr("Stop the travel past the bounds below.")));
        t.params.append(lengthParam(QStringLiteral("lowerTranslation"),
                                    QObject::tr("Lower Translation"), limitSection(), 0.0,
                                    QObject::tr("How far back along the axis, in scene units.")));
        t.params.append(lengthParam(QStringLiteral("upperTranslation"),
                                    QObject::tr("Upper Translation"), limitSection(), 0.0,
                                    QObject::tr("How far forward along the axis, in scene"
                                                " units.")));

        t.params.append(boolParam(QStringLiteral("enableMotor"), QObject::tr("Enabled"),
                                  motorSection(), false,
                                  QObject::tr("Drive the spin rather than letting it turn"
                                              " freely.")));
        t.params.append(forceParam(QStringLiteral("maxMotorTorque"),
                                   QObject::tr("Max Motor Torque (N·m)"), motorSection(), 0.0,
                                   QObject::tr("The most torque the motor may apply.")));
        t.params.append(realParam(QStringLiteral("motorSpeed"), QObject::tr("Motor Speed (deg/s)"),
                                  motorSection(), 0.0, -100000.0, 100000.0, 2, 10.0,
                                  QObject::tr("Target spin speed.")));

        append(&t.params, solverBlock());
        // No limit events: Box2D cannot report a wheel joint's travel.
        types.append(t);
    }

    // --- motor: drive one body to an offset from another ------------------
    // (b2MotorJointDef -- no anchors at all; the constraint is the offset.)
    {
        JointType t;
        t.id = QStringLiteral("motor");
        t.color = QColor(0xE0, 0x6A, 0xA8); // pink -- driven, not constrained
        t.label = QObject::tr("Motor (offset drive)");
        t.description = QObject::tr("Drives body B to hold a set position and angle relative to"
                                    " body A, within a force and torque budget.");
        t.anchorCount = 0;
        t.visual = JointVisual::Link;

        t.params.append(lengthParam(QStringLiteral("linearOffsetX"),
                                    QObject::tr("Linear Offset X"), jointSection(), 0.0,
                                    QObject::tr("Where body B should sit relative to body A,"
                                                " measured in body A's frame, in scene units.")));
        t.params.append(lengthParam(QStringLiteral("linearOffsetY"),
                                    QObject::tr("Linear Offset Y"), jointSection(), 0.0,
                                    QObject::tr("Where body B should sit relative to body A,"
                                                " measured in body A's frame, in scene units.")));
        // b2MotorJointDef defaults these to zero, which asks the solver to drag
        // body B onto body A the moment the world starts. Started from where
        // the bodies already are, the joint holds them instead.
        t.params.last().defaultSource = DefaultSource::RelativeY;
        t.params[t.params.size() - 2].defaultSource = DefaultSource::RelativeX;

        t.params.append(angleParam(QStringLiteral("angularOffset"),
                                   QObject::tr("Angular Offset (deg)"), jointSection(), 0.0,
                                   QObject::tr("The body-B-minus-body-A angle to hold.")));
        t.params.last().defaultSource = DefaultSource::RelativeAngleDegrees;

        t.params.append(realParam(QStringLiteral("correctionFactor"),
                                  QObject::tr("Correction Factor"), jointSection(), 0.3,
                                  0.0, 1.0, 2, 0.05,
                                  QObject::tr("How hard the joint pulls towards the offset, from 0"
                                              " to 1.")));

        t.params.append(forceParam(QStringLiteral("maxForce"), QObject::tr("Max Force (N)"),
                                   motorSection(), 1.0,
                                   QObject::tr("The most force the motor may apply.")));
        t.params.append(forceParam(QStringLiteral("maxTorque"), QObject::tr("Max Torque (N·m)"),
                                   motorSection(), 1.0,
                                   QObject::tr("The most torque the motor may apply.")));

        append(&t.params, solverBlock());
        types.append(t);
    }

    // --- mouse: drag a point on a body towards a target -------------------
    // (b2MouseJointDef -- body A is assumed static; the anchor is the target.)
    {
        JointType t;
        t.id = QStringLiteral("mouse");
        t.color = QColor(0x8A, 0xC8, 0x5A); // lime -- a soft pull towards a target
        t.label = QObject::tr("Mouse (soft target)");
        t.description = QObject::tr("Pulls a point on body B softly towards a target point."
                                    " Body A must be static -- Box2D refuses this joint"
                                    " otherwise.");
        t.anchorCount = 1;
        t.visual = JointVisual::Pivot;

        // b2MouseJoint_SetTarget is the only way this joint does anything
        // interesting: moved by a rule, it leads a body across the scene
        // without ever placing it, which is what setting a position cannot do.
        t.params.append(lengthParam(QStringLiteral("targetX"), QObject::tr("Target X"),
                                    jointSection(), 0.0,
                                    QObject::tr("Where the body is pulled towards, in scene"
                                                " coordinates. Leave both at zero to use the"
                                                " anchor the joint was placed at.")));
        t.params.append(lengthParam(QStringLiteral("targetY"), QObject::tr("Target Y"),
                                    jointSection(), 0.0,
                                    QObject::tr("Where the body is pulled towards, in scene"
                                                " coordinates. Leave both at zero to use the"
                                                " anchor the joint was placed at.")));

        t.params.append(hertzParam(QStringLiteral("hertz"), QObject::tr("Frequency (Hz)"),
                                   springSection(), 4.0,
                                   QObject::tr("How stiffly the point is pulled towards the"
                                               " target.")));
        t.params.append(ratioParam(QStringLiteral("dampingRatio"), QObject::tr("Damping Ratio"),
                                   springSection(), 1.0,
                                   QObject::tr("How quickly the pull settles.")));
        t.params.append(forceParam(QStringLiteral("maxForce"), QObject::tr("Max Force (N)"),
                                   motorSection(), 1.0,
                                   QObject::tr("The most force the joint may apply.")));

        append(&t.params, solverBlock());
        types.append(t);
    }

    // --- filter: stop two bodies colliding (b2FilterJointDef) -------------
    // The one def with no settings of its own -- not an omission.
    {
        JointType t;
        t.id = QStringLiteral("filter");
        t.color = QColor(0x9E, 0x9E, 0x9E); // grey -- holds nothing, only stops collision
        t.label = QObject::tr("Filter (no collision)");
        t.description = QObject::tr("Holds nothing together -- it only stops these two bodies"
                                    " colliding with each other.");
        t.anchorCount = 0;
        t.visual = JointVisual::Link;
        types.append(t);
    }

    return types;
}

} // namespace physics
