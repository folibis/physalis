#pragma once

#include "PhysicsTypes.h"
#include "JointTypes.h"
#include <QString>
#include <QPair>
#include <QVariant>
#include <QVector>

namespace physics {


class IPhysicsEngine
{
public:
    virtual ~IPhysicsEngine() = default;

    virtual QString name() const = 0;

    virtual void createWorld(const WorldDesc &desc) = 0;
    virtual void destroyWorld() = 0;

    virtual BodyHandle addBody(const BodyDesc &desc) = 0;

    virtual void step(qreal dt) = 0;

    virtual BodyState bodyState(BodyHandle handle) const = 0;

    // --- Joints -------------------------------------------------------
    // The joint types this backend offers, fully described: parameters,
    // units, defaults, how many anchors to ask for, how to draw one. The
    // editor builds its joint interface from this and holds no knowledge of
    // any particular kind of joint, so a backend can add or change one
    // without touching a line outside itself.
    //
    // Returning an empty list means the backend supports no joints, and the
    // editor simply offers none.
    virtual QVector<JointType> jointTypes() const { return {}; }

    virtual QVector<EventType> bodyEvents() const { return {}; }
    virtual QVector<EventType> shapeEvents() const { return {}; }

    // One-shot things a rule can do to a body, each carrying its own
    // parameters. Nothing is stored: performing one is the whole effect.
    virtual QVector<ActionType> bodyActions() const { return {}; }
    virtual void performAction(const QString &, BodyHandle, const QVariantMap &) {}

    // The same actions, aimed at a bare coordinate in scene units. A explosion
    // has no body, so there is no handle to name.
    virtual void performActionAt(const QString &, const QPointF &, const QVariantMap &) {}

    virtual void setJointParam(JointHandle, const QString &, const QVariant &) {}
    virtual void setBodyParam(BodyHandle, const QString &, const QVariant &) {}

    virtual QVariant bodyValue(BodyHandle, const QString &) const { return {}; }
    virtual QVariant jointValue(JointHandle, const QString &) const { return {}; }

    virtual PropertyList bodyProperties() const { return {}; }
    virtual PropertyList shapeProperties() const { return {}; }

    virtual PropertyList jointReadables(const QString &) const { return {}; }

    virtual void setShapeParam(const QString &, const QString &, const QVariant &) {}
    virtual QVariant shapeValue(const QString &, const QString &) const { return {}; }


    virtual QVector<EngineEvent> pollEvents() { return {}; }

    // The nearest thing along a line, or a miss. Both points are in scene
    // units; maskBits filters what the ray can see, the same bits a shape
    // filters with.
    virtual RayHit castRay(const QPointF &, const QPointF &, quint64) const { return {}; }

    virtual JointHandle addJoint(const JointDesc &desc)
    {
        Q_UNUSED(desc);
        return kInvalidJoint;
    }
};

} // namespace physics
