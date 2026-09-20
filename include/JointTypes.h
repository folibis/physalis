// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ruslan Muhlinin. See LICENSE.
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

// A property is the engine's to name, but two of them the editor also has to
// recognise: it hatches a shape things pass through rather than filling it, and
// it works out where a body balances from what its shapes are made of. Each
// engine tags whichever of its own keys plays those parts; an engine with no
// such property tags nothing, and the editor draws and measures without it.
enum class PropertyRole {
    None,
    Sensor,   // things pass through this shape
    Density,  // mass per unit of area
    // A body property that pushes through the centre of mass rather than
    // storing anything, one per direction, in scene units. The editor's
    // slingshot finds them this way and names neither.
    ImpulseX,
    ImpulseY,
    // Where a body is, which way it faces, and how it is moving, in scene units
    // and degrees -- settable while running. "Init state" puts a body back and
    // stops it through these, and names none of them.
    PositionX,
    PositionY,
    Angle,
    VelocityX,
    VelocityY,
    AngularVelocity,
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
    // Performing this takes the body out of the world. Before it does, the
    // application asks whether a rule answers "is about to be removed" -- and
    // if one does, that rule is carried out instead and the body stays.
    bool removesBody = false;
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
    // True when the value belongs to the object rather than to the run: the
    // editor keeps it in the scene, shows it in the property table, saves it,
    // and hands it back when the world is built. False for readouts and
    // one-shot pushes -- where a body has got to, how fast it is going, a kick.
    bool stored = false;
    PropertyRole role = PropertyRole::None;
    bool liveSettable = false;
    bool liveReadable = false;
    // True when reading this only gives back a setting the object already
    // carries -- "is the motor on", where the motor's own switch is a
    // parameter. A rule still wants it, so it can ask; a property table does
    // not, because the setting is already a row three lines further up.
    bool mirrorsSetting = false;
    // A reading a rule can ask for that the property table leaves out, so the
    // table carries the few worth watching rather than every number the
    // engine can produce.
    bool rulesOnly = false;
};

using PropertyList = QVector<JointParam>;

// What an object of this kind starts out as, straight from the engine that
// described it. The editor holds only what differs from this, so a property an
// engine drops or renames leaves nothing stale behind.
// The engine's own name for a property the editor has to recognise, or an
// empty string if this engine has none.
inline QString keyForRole(const PropertyList &properties, PropertyRole role)
{
    for (const JointParam &property : properties) {
        if (property.role == role)
            return property.key;
    }
    return QString();
}

inline QVariantMap storedDefaults(const PropertyList &properties)
{
    QVariantMap values;
    for (const JointParam &property : properties) {
        if (property.stored)
            values.insert(property.key, property.defaultValue);
    }
    return values;
}

struct EventType {
    QString id;           // stable, e.g. "limitLower"; what scene files store
    QString label;        // what the rule editor shows
    QString description;  // one line, for the tooltip

    // Does this event happen *with* something -- a shape touched, a body that
    // entered a sensor -- or does it just happen? A touch names the other
    // party and a rule can single one out; a joint arriving at its limit names
    // nobody, and asking which object it arrived at is not a question. The
    // editor shows the "or anything" chooser only for the first kind.
    bool namesOther = true;
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

    // How many bodies the joint connects. Two for everything that holds one
    // thing to another; one for a joint that holds a body to a point in the
    // world, which has no second body to name. The application asks for this
    // many and does not otherwise know the difference.
    int bodyCount = 2;

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
