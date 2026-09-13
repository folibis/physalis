#include "ChipmunkEngine.h"

#include <QColor>
#include <QObject>

#include <cmath>

// Chipmunk's constraint catalogue, as data.
//
// Chipmunk builds a hinge with a motor and a limit out of three constraints
// between the same two bodies -- a pivot, a simple motor, a rotary limit --
// rather than one joint with switches, and the catalogue offers them that way:
// every constraint Chipmunk has, under Chipmunk's own names.
//
// Every one has the three solver settings cpConstraint carries, and every
// setting has a setter on a constraint that already exists, so all of them can
// be changed by a rule while running.
//
// The liberties taken, undone in ChipmunkJoints.cpp: lengths in scene units,
// angles in degrees and measured from where the bodies started rather than
// from zero, "unlimited" as zero where Chipmunk's own value is infinity, and
// springs set by frequency and damping ratio, where Chipmunk wants a stiffness
// that only means something next to the masses being sprung.

namespace physics {

namespace {

QString jointSection()  { return QObject::tr("Joint"); }
QString springSection() { return QObject::tr("Spring"); }
QString limitSection()  { return QObject::tr("Limit"); }
QString solverSection() { return QObject::tr("Solver"); }

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
    p.liveSettable = true;
    return p;
}

JointParam angleParam(const QString &key, const QString &label, const QString &section,
                      qreal defaultValue, const QString &tooltip)
{
    return realParam(key, label, section, defaultValue, -3600.0, 3600.0, 2, 1.0, tooltip);
}

JointParam lengthParam(const QString &key, const QString &label, const QString &section,
                       qreal defaultValue, const QString &tooltip)
{
    return realParam(key, label, section, defaultValue, -1e6, 1e6, 1, 1.0, tooltip);
}

QVector<EventType> limitEvents()
{
    return {
        // None of them names a second object: a joint arrives at a limit on
        // its own, and there is nothing for a rule to single out.
        {QStringLiteral("limitLower"), QObject::tr("Lower limit reached"),
         QObject::tr("Raised when the joint arrives at its lower limit."), false},
        {QStringLiteral("limitUpper"), QObject::tr("Upper limit reached"),
         QObject::tr("Raised when the joint arrives at its upper limit."), false},
        {QStringLiteral("limitEither"), QObject::tr("Either limit reached"),
         QObject::tr("Raised at whichever limit the joint arrives at."), false},
    };
}

// cpConstraintSetMaxForce / SetErrorBias / SetMaxBias. Chipmunk's defaults are
// infinity, (1 - 0.1)^60 and infinity.
QVector<JointParam> solverBlock(qreal maxForce = 0.0, qreal errorBias = std::pow(0.9, 60.0))
{
    const QString section = solverSection();
    return {
        realParam(QStringLiteral("maxForce"), QObject::tr("Max Force"), section, maxForce,
                  0.0, 1e9, 4, 0.1,
                  QObject::tr("The most force the joint may use to hold -- a torque, for"
                              " the ones that act on angles. Zero is unlimited, which is"
                              " Chipmunk's own default.")),
        realParam(QStringLiteral("errorBias"), QObject::tr("Error Left After 1 s"), section,
                  errorBias, 0.0, 1.0, 6, 0.0001,
                  QObject::tr("How much of any drift the joint leaves uncorrected after a"
                              " second. Smaller pulls back harder.")),
        realParam(QStringLiteral("maxBias"), QObject::tr("Max Correction Speed"), section, 0.0,
                  0.0, 1e9, 1, 10.0,
                  QObject::tr("The fastest the joint may pull drift back, in scene units"
                              " per second -- degrees per second, for the ones that act"
                              " on angles. Zero is unlimited.")),
    };
}

// The two a spring is set by, turned into Chipmunk's stiffness and damping
// from the masses it is springing.
void appendSpring(JointType *type, qreal hertz, qreal dampingRatio)
{
    type->params.append(realParam(QStringLiteral("hertz"), QObject::tr("Frequency (Hz)"),
                                  springSection(), hertz, 0.0, 1000.0, 2, 0.5,
                                  QObject::tr("How many times a second it would swing if"
                                              " left alone. Higher is stiffer.")));
    type->params.append(realParam(QStringLiteral("dampingRatio"), QObject::tr("Damping Ratio"),
                                  springSection(), dampingRatio, 0.0, 100.0, 2, 0.05,
                                  QObject::tr("How quickly it settles. 0 swings forever, 1"
                                              " stops without overshooting.")));
}

void append(QVector<JointParam> *into, const QVector<JointParam> &block)
{
    for (const JointParam &param : block)
        into->append(param);
}

} // namespace

QVector<JointType> ChipmunkEngine::jointTypes() const
{
    QVector<JointType> types;

    // --- pivot: a hinge (cpPivotJoint) --------------------------------------
    {
        JointType t;
        t.id = QStringLiteral("pivot");
        t.color = QColor(0xE8, 0xC4, 0x6A);
        t.label = QObject::tr("Pivot");
        t.description = QObject::tr("Pins two bodies together at a point and lets them turn"
                                    " about it. Add a Simple Motor or a Rotary Limit for a"
                                    " driven or limited hinge.");
        t.anchorCount = 1;
        t.visual = JointVisual::Pivot;
        append(&t.params, solverBlock());
        types.append(t);
    }

    // --- pin: a rod (cpPinJoint) --------------------------------------------
    {
        JointType t;
        t.id = QStringLiteral("pin");
        t.color = QColor(0x6A, 0xB0, 0xE8);
        t.label = QObject::tr("Pin");
        t.description = QObject::tr("Holds two points a fixed distance apart, like a rod"
                                    " with a hinge at each end.");
        t.anchorCount = 2;
        t.visual = JointVisual::Segment;
        t.params.append(lengthParam(QStringLiteral("distance"), QObject::tr("Distance"),
                                    jointSection(), 0.0,
                                    QObject::tr("In scene units. Zero means however far apart"
                                                " the anchors are when the run starts.")));
        append(&t.params, solverBlock());
        types.append(t);
    }

    // --- slide: a rope, or a rod with play in it (cpSlideJoint) ------------
    {
        JointType t;
        t.id = QStringLiteral("slide");
        t.color = QColor(0x5A, 0xC8, 0xC8);
        t.label = QObject::tr("Slide");
        t.description = QObject::tr("Keeps two points between a shortest and a longest"
                                    " distance. With no shortest it is a rope.");
        t.anchorCount = 2;
        t.visual = JointVisual::Segment;
        t.params.append(lengthParam(QStringLiteral("minLength"), QObject::tr("Min Length"),
                                    limitSection(), 0.0,
                                    QObject::tr("The closest the anchors may come, in scene"
                                                " units.")));
        t.params.append(lengthParam(QStringLiteral("maxLength"), QObject::tr("Max Length"),
                                    limitSection(), 0.0,
                                    QObject::tr("The furthest apart they may go, in scene"
                                                " units. Zero means however far apart they"
                                                " are when the run starts.")));
        append(&t.params, solverBlock());
        t.events = limitEvents();
        types.append(t);
    }

    // --- groove: a pin sliding in a slot (cpGrooveJoint) --------------------
    {
        JointType t;
        t.id = QStringLiteral("groove");
        t.color = QColor(0x6A, 0xD1, 0xA8);
        t.label = QObject::tr("Groove");
        t.description = QObject::tr("A slot in the first body and a pin on the second that"
                                    " runs along it. The second body may still turn.");
        t.anchorCount = 1;
        t.needsAxis = true;
        t.defaultAxisDegrees = 0.0;
        t.visual = JointVisual::Axis;
        t.params.append(lengthParam(QStringLiteral("lowerTranslation"),
                                    QObject::tr("Slot Start"), limitSection(), -100.0,
                                    QObject::tr("Where the slot begins, measured back along"
                                                " the axis from the anchor, in scene units.")));
        t.params.append(lengthParam(QStringLiteral("upperTranslation"),
                                    QObject::tr("Slot End"), limitSection(), 100.0,
                                    QObject::tr("Where the slot ends, measured forward along"
                                                " the axis from the anchor, in scene units.")));
        append(&t.params, solverBlock());
        t.events = limitEvents();
        types.append(t);
    }

    // --- damped spring (cpDampedSpring) -------------------------------------
    {
        JointType t;
        t.id = QStringLiteral("dampedSpring");
        t.color = QColor(0xE8, 0x8A, 0x6A);
        t.label = QObject::tr("Damped Spring");
        t.description = QObject::tr("A spring with a shock absorber between two points.");
        t.anchorCount = 2;
        t.visual = JointVisual::Segment;
        t.params.append(lengthParam(QStringLiteral("restLength"), QObject::tr("Rest Length"),
                                    jointSection(), 0.0,
                                    QObject::tr("The length it springs back to, in scene"
                                                " units. Zero means however far apart the"
                                                " anchors are when the run starts.")));
        appendSpring(&t, 4.0, 0.5);
        append(&t.params, solverBlock());
        types.append(t);
    }

    // --- damped rotary spring (cpDampedRotarySpring) -----------------------
    {
        JointType t;
        t.id = QStringLiteral("dampedRotarySpring");
        t.color = QColor(0xE0, 0x6A, 0xA8);
        t.label = QObject::tr("Rotary Spring");
        t.description = QObject::tr("A torsion spring: turns the second body back towards an"
                                    " angle against the first.");
        t.anchorCount = 0;
        t.visual = JointVisual::Link;
        t.params.append(angleParam(QStringLiteral("restAngle"), QObject::tr("Rest Angle (deg)"),
                                   jointSection(), 0.0,
                                   QObject::tr("Measured from how the two bodies stand when"
                                               " the run starts.")));
        appendSpring(&t, 4.0, 0.5);
        append(&t.params, solverBlock());
        types.append(t);
    }

    // --- rotary limit (cpRotaryLimitJoint) ----------------------------------
    {
        JointType t;
        t.id = QStringLiteral("rotaryLimit");
        t.color = QColor(0xB4, 0x8A, 0xE8);
        t.label = QObject::tr("Rotary Limit");
        t.description = QObject::tr("Stops the second body turning further than a range of"
                                    " angles against the first.");
        t.anchorCount = 0;
        t.visual = JointVisual::Link;
        t.params.append(angleParam(QStringLiteral("lowerAngle"), QObject::tr("Lower Angle (deg)"),
                                   limitSection(), -45.0,
                                   QObject::tr("Measured from how the two bodies stand when"
                                               " the run starts.")));
        t.params.append(angleParam(QStringLiteral("upperAngle"), QObject::tr("Upper Angle (deg)"),
                                   limitSection(), 45.0,
                                   QObject::tr("Measured from how the two bodies stand when"
                                               " the run starts.")));
        append(&t.params, solverBlock());
        t.events = limitEvents();
        types.append(t);
    }

    // --- ratchet (cpRatchetJoint) -------------------------------------------
    {
        JointType t;
        t.id = QStringLiteral("ratchet");
        t.color = QColor(0xB0, 0x8A, 0x5A);
        t.label = QObject::tr("Ratchet");
        t.description = QObject::tr("Lets the second body turn one way against the first, and"
                                    " catches it every click if it turns back.");
        t.anchorCount = 0;
        t.visual = JointVisual::Link;
        t.params.append(angleParam(QStringLiteral("ratchet"), QObject::tr("Click (deg)"),
                                   jointSection(), 90.0,
                                   QObject::tr("The angle between two clicks. Its sign says"
                                               " which way it turns freely.")));
        t.params.append(angleParam(QStringLiteral("phase"), QObject::tr("Phase (deg)"),
                                   jointSection(), 0.0,
                                   QObject::tr("Where the first click sits.")));
        append(&t.params, solverBlock());
        types.append(t);
    }

    // --- gear (cpGearJoint) -------------------------------------------------
    {
        JointType t;
        t.id = QStringLiteral("gear");
        t.color = QColor(0x8A, 0x9E, 0xB8);
        t.label = QObject::tr("Gear");
        t.description = QObject::tr("Ties how the two bodies turn together, at a ratio, as"
                                    " if they were geared.");
        t.anchorCount = 0;
        t.visual = JointVisual::Link;
        t.params.append(realParam(QStringLiteral("ratio"), QObject::tr("Ratio"), jointSection(),
                                  1.0, -1000.0, 1000.0, 3, 0.1,
                                  QObject::tr("How many turns of the first body make one of"
                                              " the second. Negative turns them opposite ways."
                                              " Zero is not allowed.")));
        t.params.append(angleParam(QStringLiteral("phase"), QObject::tr("Phase (deg)"),
                                   jointSection(), 0.0,
                                   QObject::tr("An offset between the two, measured from how"
                                               " they stand when the run starts.")));
        append(&t.params, solverBlock());
        types.append(t);
    }

    // --- simple motor (cpSimpleMotor) ---------------------------------------
    {
        JointType t;
        t.id = QStringLiteral("simpleMotor");
        t.color = QColor(0xD8, 0x5A, 0x5A);
        t.label = QObject::tr("Simple Motor");
        t.description = QObject::tr("Turns the second body against the first at a steady rate."
                                    " Its Max Force is the torque it has.");
        t.anchorCount = 0;
        t.visual = JointVisual::Link;
        t.params.append(realParam(QStringLiteral("rate"), QObject::tr("Rate (deg/s)"),
                                  jointSection(), 90.0, -100000.0, 100000.0, 2, 10.0,
                                  QObject::tr("How fast it turns, and which way.")));
        append(&t.params, solverBlock());
        types.append(t);
    }

    // --- mouse: drag a point towards a target -------------------------------
    // Chipmunk has no joint of its own for this; its demos pin the body to a
    // kinematic body that follows the pointer, with a budget on the force. That
    // is what this is, and the second body is the engine's business.
    {
        JointType t;
        t.id = QStringLiteral("mouse");
        t.bodyCount = 1;
        t.color = QColor(0x8A, 0xC8, 0x5A);
        t.label = QObject::tr("Mouse");
        t.description = QObject::tr("Pulls a point on the body towards a point in the world."
                                    " Drag the first anchor to choose what is held, and the"
                                    " second to choose where it is pulled.");
        t.anchorCount = 2;
        t.visual = JointVisual::Pivot;
        t.params.append(lengthParam(QStringLiteral("targetX"), QObject::tr("Target X"),
                                    jointSection(), 0.0,
                                    QObject::tr("Where the body is pulled towards, in scene"
                                                " coordinates. Leave both at zero to use the"
                                                " second anchor.")));
        t.params.append(lengthParam(QStringLiteral("targetY"), QObject::tr("Target Y"),
                                    jointSection(), 0.0,
                                    QObject::tr("Where the body is pulled towards, in scene"
                                                " coordinates. Leave both at zero to use the"
                                                " second anchor.")));
        // The budget and the softness Chipmunk's own demo drags with.
        append(&t.params, solverBlock(1.0, std::pow(1.0 - 0.15, 60.0)));
        types.append(t);
    }

    return types;
}

} // namespace physics
