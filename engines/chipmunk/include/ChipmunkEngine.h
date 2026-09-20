// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ruslan Muhlinin. See LICENSE.
#pragma once

#include "IPhysicsEngine.h"

#include <QHash>
#include <QVector>
#include <chipmunk/chipmunk.h>

#include <memory>
#include <vector>

namespace physics {

// A collision filter's bits, read from whatever a scene carried. A JSON file
// keeps numbers as doubles, and a file written when the default mask was "every
// bit set" holds 18446744073709552000 -- 2^64 rounded up, past what a quint64
// can hold. Casting that over is undefined, and what it produced was a mask of
// zero: a shape that collides with nothing. At or beyond the top of the range
// means all of them.
inline quint64 filterBits(const QVariant &value, quint64 fallback)
{
    if (!value.isValid())
        return fallback;
    const double asNumber = value.toDouble();
    if (!(asNumber > 0.0))
        return 0;
    if (asNumber >= 18446744073709551615.0)
        return ~quint64(0);
    return static_cast<quint64>(asNumber);
}


// Chipmunk2D backend. The only translation units in the project that include a
// Chipmunk header -- everything above it speaks the structs in PhysicsTypes.h.
//
// The conventions are the Box2D plugin's, on purpose, so a scene behaves the
// same whichever engine runs it: lengths are scene units divided by
// WorldDesc::pixelsPerMeter, gravity and the speeds a scene is written with are
// quoted at kReferencePixelsPerMeter, angles go in as degrees. Chipmunk is
// unitless, so metres are only a choice -- but its mass, density and force
// then mean kilograms and newtons, as Box2D's do. No Y flip: Chipmunk, like
// Box2D, has no handedness of its own.
class ChipmunkEngine : public IPhysicsEngine
{
public:
    ~ChipmunkEngine() override;

    QString name() const override { return QStringLiteral("Chipmunk2D"); }

    void createWorld(const WorldDesc &desc) override;
    void destroyWorld() override;
    BodyHandle addBody(const BodyDesc &desc) override;
    void step(qreal dt) override;
    BodyState bodyState(BodyHandle handle) const override;

    // ChipmunkJointTypes.cpp -- the whole joint catalogue lives in that file.
    QVector<JointType> jointTypes() const override;

    // ChipmunkCatalogue.cpp -- everything published about bodies, shapes and
    // the world, and what can happen to them.
    PropertyList bodyProperties() const override;
    PropertyList shapeProperties() const override;
    PropertyList worldProperties() const override;
    PropertyList jointReadables(const QString &typeId) const override;
    QVector<EventType> bodyEvents() const override;
    QVector<EventType> shapeEvents() const override;
    QVector<ActionType> bodyActions() const override;
    QVector<ActionType> jointActions() const override;

    // ChipmunkJoints.cpp.
    JointHandle addJoint(const JointDesc &desc) override;
    void setJointParam(JointHandle handle, const QString &key, const QVariant &value) override;
    QVariant jointValue(JointHandle handle, const QString &key) const override;
    void performJointAction(const QString &id, JointHandle target,
                            const QVariantMap &params) override;

    // ChipmunkReadback.cpp -- reads and writes on a running world.
    QVariant bodyValue(BodyHandle handle, const QString &key) const override;
    void setBodyParam(BodyHandle handle, const QString &key, const QVariant &value) override;
    QVariant shapeValue(const QString &name, const QString &key) const override;
    void setShapeParam(const QString &name, const QString &key, const QVariant &value) override;
    QVariant worldValue(const QString &key) const override;
    void setWorldParam(const QString &key, const QVariant &value) override;

    // ChipmunkEvents.cpp -- contacts, sensors, rays and blasts.
    QVector<EngineEvent> pollEvents() override;
    RayHit castRay(const QPointF &origin, const QPointF &translation,
                   quint64 maskBits) const override;
    void performAction(const QString &id, BodyHandle target,
                       const QVariantMap &params) override;
    void performActionAt(const QString &id, const QPointF &position,
                         const QVariantMap &params) override;

    // What Chipmunk knows as a body, plus what Box2D has per body and Chipmunk
    // does not: gravity scale, damping, a sleep switch, fixed rotation. Those
    // are applied here, in the body's velocity update. Owned by the engine and
    // pointed to from the body's user data, so a callback can find it.
    struct BodyRecord {
        ChipmunkEngine *engine = nullptr;
        cpBody *body = nullptr;
        BodyHandle handle = kInvalidBody;
        std::vector<cpShape *> shapes;
        qreal gravityScale = 1.0;
        qreal linearDamping = 0.0;
        qreal angularDamping = 0.0;
        bool enableSleep = true;
        bool fixedRotation = false;
        // Taken out of the space while disabled, and put back as it was.
        bool inSpace = true;
        // Taken out for good by a rule. Kept until the world goes, so a
        // callback still holding the pointer never reads freed memory.
        bool removed = false;
        bool wasSleeping = false;
    };

    // One Chipmunk shape. A part the editor sees as one shape can become
    // several -- an outline becomes a segment per edge, a concave polygon a
    // convex piece per hull -- and every one of them carries the part's name.
    struct ShapeRecord {
        QString name;
        cpShape *shape = nullptr;
        BodyRecord *owner = nullptr;
        // Box2D's filter, applied in the collision callbacks. Chipmunk's own
        // has 32 bits and no "always collide" group, so it is left open.
        Filter filter;
        bool contactEvents = false;
        bool hitEvents = false;
        bool sensorEvents = false;
        bool preSolveEvents = false;
        qreal tangentSpeed = 0.0;   // metres per second
        int overlaps = 0;           // sensors: what is inside right now
    };

    // Called from the collision handler's function pointers.
    cpBool onBegin(cpArbiter *arbiter);
    cpBool onPreSolve(cpArbiter *arbiter);
    void onSeparate(cpArbiter *arbiter);

private:
    friend void updateBodyVelocity(cpBody *, cpVect, cpFloat, cpFloat);

    struct JointRecord {
        QString typeId;
        cpConstraint *constraint = nullptr;
        // Out of the space for the rest of the run: broken by a rule, or gone
        // with a body a rule removed. Still owned, and freed with the world.
        bool broken = false;
        // Mouse joints: the kinematic body the held point is pinned to, which
        // is what moves when the target does.
        cpBody *hand = nullptr;
        // Body B's angle less body A's when the joint was made. Chipmunk
        // measures rotary limits, springs and gears from absolute angles;
        // the editor measures them from where the bodies started.
        cpFloat referenceAngle = 0.0;
        // Groove: where the anchor was and which way the slot runs, in body
        // A's frame. The slot's ends are measured from the one along the other.
        cpVect grooveOrigin = cpvzero;
        cpVect grooveAxis = cpv(1.0, 0.0);
        // Gear: the phase that holds the bodies as they started.
        cpFloat gearBase = 0.0;
        // Springs are set as a frequency and damping ratio, and Chipmunk wants
        // stiffness and damping -- which depend on what is being sprung.
        cpFloat sprungInertia = 0.0;
        qreal hertz = 0.0;
        qreal dampingRatio = 0.0;
        // Edge-triggered limit events, as in the Box2D plugin.
        bool sampled = false;
        bool atLower = false;
        bool atUpper = false;
    };

    struct HitRecord {
        qreal speed = 0.0;  // scene units per second
        QPointF point;      // scene coordinates
        QPointF normal;
    };

    cpVect toMeters(const QPointF &scenePoint) const;
    QPointF toScene(cpVect metres) const;
    cpFloat metres(qreal sceneUnits) const { return sceneUnits / m_pixelsPerMeter; }

    BodyRecord *bodyAt(BodyHandle handle) const;
    JointRecord *jointAt(JointHandle handle) const;
    QVector<ShapeRecord *> shapesNamed(const QString &name) const;

    bool attachShape(BodyRecord *record, const ShapePart &part, BodyType type);
    ShapeRecord *addShape(BodyRecord *record, cpShape *shape, const ShapePart &part);
    bool attachEdges(BodyRecord *record, const ShapePart &part,
                     const QVector<cpVect> &points, bool closed, bool smooth);
    bool attachConvexPieces(BodyRecord *record, const ShapePart &part,
                            const QVector<cpVect> &points);

    // Whether a body is part of the simulation at all: out of the space it
    // neither moves nor collides, and its joints go with it.
    void setInSpace(BodyRecord *record, bool inSpace);
    // Chipmunk recomputes the moment whenever mass changes; fixed rotation is
    // an infinite moment, so it has to be put back every time.
    void applyFixedRotation(BodyRecord *record);
    // Mouse joints and springs work from the bodies' masses.
    static cpFloat effectiveMass(cpBody *a, cpBody *b);
    static cpFloat effectiveMoment(cpBody *a, cpBody *b);
    void applySpring(JointRecord *joint);

    void raise(ShapeRecord *subject, ShapeRecord *other, const QString &id);
    void raiseBoth(ShapeRecord *a, ShapeRecord *b, const QString &id);
    void detectLimitEvents();
    void collectBodyEvents();
    void explodeAt(cpVect position, const QVariantMap &params);

    cpSpace *m_space = nullptr;
    std::vector<std::unique_ptr<BodyRecord>> m_bodies;   // indexed by handle
    std::vector<std::unique_ptr<ShapeRecord>> m_shapes;
    QHash<QString, QVector<ShapeRecord *>> m_shapesByName;
    // A rule watches a sensor somewhere in this space, so every shape has to be
    // able to set one off -- see addShape.
    bool m_sensorWatched = false;
    std::vector<std::unique_ptr<JointRecord>> m_joints;  // indexed by handle
    QVector<EngineEvent> m_pendingEvents;
    QHash<QString, HitRecord> m_lastHit;

    qreal m_pixelsPerMeter = kReferencePixelsPerMeter;
    qreal m_motionScale = 1.0;
    cpFloat m_lastStep = 1.0 / 60.0;

    // Box2D's world settings that Chipmunk has no counterpart for, applied by
    // the callbacks and the velocity update. In solver units.
    cpFloat m_restitutionThreshold = 0.0;
    cpFloat m_hitEventThreshold = 0.0;
    cpFloat m_maximumSpeed = 0.0;
    // Chipmunk turns sleeping on by giving the space a time to sleep after.
    // Box2D's is half a second, and so is this.
    cpFloat m_sleepTime = 0.5;
    bool m_enableSleep = true;
};

} // namespace physics
