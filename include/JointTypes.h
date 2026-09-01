#pragma once

#include "PhysicsTypes.h"

#include <QColor>
#include <QPointF>
#include <QString>
#include <QVariant>
#include <QVariantMap>
#include <QVector>

namespace physics {

// The editor knows how to edit these, and nothing about what they mean.
enum class ParamType {
    Bool,
    Real,     // plain number, shown with `decimals` and stepped by `step`
    Integer,
    Choice,   // one of `choices`, stored as the index
};

// Where a parameter's starting value comes from. Most are a fixed number the
// engine names; a few only make sense measured from the two bodies as they
// stand, and a fixed 0 would mean "snap them together" the moment a run starts.
enum class DefaultSource {
    Fixed,
    RelativeX,             // bodyB minus bodyA, in bodyA's frame, scene units
    RelativeY,
    RelativeAngleDegrees,  // bodyB angle minus bodyA angle
};

// Something an engine can *do* to an object, as opposed to a value it can be
// set to. Its parameters belong to the action, not to the object, because
// nothing about them survives the moment it happens.
struct ActionType {
    QString id;                 // stable identifier, used in scene files
    QString label;              // what the rule editor shows
    QString description;
    QVector<struct JointParam> params;
};

struct JointParam {
    QString key;              // stable identifier, used in the value map and in scene files
    QString label;            // what the property panel shows
    QString section;          // groups parameters into tabs, e.g. "Spring", "Limit", "Motor"
    ParamType type = ParamType::Real;

    QVariant defaultValue;    // also what the panel's revert button restores
    qreal minValue = -1e9;
    qreal maxValue = 1e9;
    int decimals = 2;
    qreal step = 0.1;
    QStringList choices;      // ParamType::Choice only

    QString tooltip;

    DefaultSource defaultSource = DefaultSource::Fixed;
    bool liveSettable = false;
    bool liveReadable = false;
};

using PropertyList = QVector<JointParam>;

struct EventType {
    QString id;           // stable, e.g. "limitLower"; what scene files store
    QString label;        // what the rule editor shows
    QString description;  // one line, for the tooltip
};

enum class JointVisual {
    Pivot,    // a ring at anchor A -- things that rotate about a point
    Segment,  // a line from anchor A to anchor B -- things that hold a distance
    Axis,     // a line through anchor A along the axis -- things that slide
    Rigid,    // a square at anchor A -- things that hold two bodies fixed
    Link,     // a plain connector between the two bodies -- anchorless joints
};

struct JointType {
    QString id;               // stable, e.g. "revolute"; what scene files store
    QString label;            // what the toolbar and property panel show
    QString description;      // one line, for the tooltip

    int anchorCount = 1;

    bool needsAxis = false;

    // The direction the backend's own default gives such a joint, in degrees.
    // Published so the property table can bold the axis once it differs from
    // it, the same way every other property is marked -- without this the row
    // has nothing to be "changed from".
    qreal defaultAxisDegrees = 0.0;

    JointVisual visual = JointVisual::Pivot;

    QColor color { 0xE8, 0xC4, 0x6A };

    QVector<JointParam> params;

    QVector<EventType> events;

    QVariantMap defaultValues() const
    {
        QVariantMap values;
        for (const JointParam &param : params)
            values.insert(param.key, param.defaultValue);
        return values;
    }
};

using JointHandle = int;
inline constexpr JointHandle kInvalidJoint = -1;

struct EngineEvent {
    JointHandle joint = kInvalidJoint;
    BodyHandle body = kInvalidBody;
    BodyHandle otherBody = kInvalidBody;

    QString subjectShape;
    QString otherShape;

    QString eventId;
};

// One joint, as handed to an engine. Anchors and axis are in scene units and
// scene coordinates -- the same convention ShapePart geometry uses, so the
// editor never has to think in body-local frames.
struct JointDesc {
    QString typeId;
    QString name;             // label only; joints are addressed by handle

    BodyHandle bodyA = kInvalidBody;
    BodyHandle bodyB = kInvalidBody;

    QVector<QPointF> anchors; // as many as the type asked for
    QPointF axis { 1.0, 0.0 };

    QVariantMap params;

    bool collideConnected = false;
};

} // namespace physics
