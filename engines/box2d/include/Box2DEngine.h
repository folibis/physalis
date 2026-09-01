#pragma once

#include "IPhysicsEngine.h"
#include <box2d/box2d.h>
#include <vector>

namespace physics {

// Box2D v3 backend. The only translation unit in the project that includes a
// Box2D header -- everything above it speaks the structs in PhysicsTypes.h.
//
// Coordinate mapping: scene-unit lengths are divided by
// WorldDesc::pixelsPerMeter; quantities that are already SI (gravity,
// velocity, density) go through untouched. No Y flip either way. Box2D is handedness-
// agnostic, and identifying its frame with the scene's y-down frame makes both
// axes and the rotation direction line up with Qt's clockwise-positive
// setRotation() without any sign juggling.
class Box2DEngine : public IPhysicsEngine
{
public:
    ~Box2DEngine() override;

    QString name() const override { return QStringLiteral("Box2D"); }

    void createWorld(const WorldDesc &desc) override;
    void destroyWorld() override;
    BodyHandle addBody(const BodyDesc &desc) override;
    void step(qreal dt) override;
    BodyState bodyState(BodyHandle handle) const override;

    // Declared here, defined in Box2DJointTypes.cpp -- the whole catalogue of
    // joints this backend offers lives in that one file.
    QVector<JointType> jointTypes() const override;

    QVariant bodyValue(BodyHandle handle, const QString &key) const override;
    QVariant jointValue(JointHandle handle, const QString &key) const override;
    // Declared here, defined in Box2DCatalogue.cpp -- everything this backend
    // publishes about bodies and shapes lives in that one file, the way the
    // joint catalogue lives in Box2DJointTypes.cpp.
    PropertyList bodyProperties() const override;
    QVector<ActionType> bodyActions() const override;
    void performAction(const QString &id, BodyHandle target,
                       const QVariantMap &params) override;
    void performActionAt(const QString &id, const QPointF &position,
                         const QVariantMap &params) override;
    PropertyList shapeProperties() const override;
    PropertyList jointReadables(const QString &typeId) const override;
    QVector<EventType> bodyEvents() const override;
    QVector<EventType> shapeEvents() const override;

    void setShapeParam(const QString &name, const QString &key, const QVariant &value) override;
    QVariant shapeValue(const QString &name, const QString &key) const override;
    JointHandle addJoint(const JointDesc &desc) override;

    // Defined in Box2DJoints.cpp alongside addJoint(), since both are the
    // per-joint-type switch and belong together.
    void setJointParam(JointHandle handle, const QString &key, const QVariant &value) override;
    void setBodyParam(BodyHandle handle, const QString &key, const QVariant &value) override;
    QVector<EngineEvent> pollEvents() override;
    RayHit castRay(const QPointF &origin, const QPointF &translation,
                   quint64 maskBits) const override;

    // Compares each limited joint against its bounds and queues an event on
    // arrival. Called from step(), after the solver has run.
    void detectLimitEvents();
    // Turns Box2D's begin/end touch events into ours, once per body involved
    // so that either side of a contact can be the one a rule triggers on.
    void collectContactEvents();
    // The handle a b2BodyId stands for, or kInvalidBody. Box2D carries it in
    // the body's user data, set when the body is added.
    BodyHandle handleOf(b2BodyId body) const;

private:
    b2Vec2 toMeters(const QPointF &scenePoint) const;
    QPointF toScene(b2Vec2 meters) const;

    // Builds the Box2D shape for one part and attaches it to `bodyId`. Needs
    // the body type as well as the part, because whether an outline can be
    // represented depends on it: an edge chain has no area, so it works as
    // static terrain but leaves a dynamic body massless. Returns false if the
    // part can't be represented.
    bool attachShape(b2BodyId bodyId, const ShapePart &part, BodyType bodyType,
                     const b2ShapeDef &shapeDef) const;

    // One b2Chain across the whole outline: the joins between edges are
    // smoothed, at the cost of the surface being one-sided.
    void explodeAt(const b2Vec2 &position, const QVariantMap &params) const;

    bool attachSmoothChain(b2BodyId bodyId, const Geometry &geometry,
                           const b2ShapeDef &shapeDef) const;

    // Attaches one two-sided b2Segment per edge of `points`, closing the
    // loop if `closed`. Returns false if there aren't enough points.
    bool attachEdgeChain(b2BodyId bodyId, const QVector<QPointF> &points, bool closed,
                         const b2ShapeDef &shapeDef) const;

    b2WorldId m_worldId = b2_nullWorldId;
    std::vector<b2BodyId> m_bodies;

    // Whether each joint was sitting against its lower/upper limit after the
    // previous step. Limit events are edge-triggered off this: a rule that
    // reverses a motor has to fire on arrival, not once per step for as long
    // as the joint stays there.
    struct LimitState {
        bool atLower = false;
        bool atUpper = false;
        // Whether this joint has been looked at yet. The first sample only
        // records where the joint is; a joint created already sitting against
        // a limit has not *arrived* there, and raising an event for it would
        // fire every rule once before the world had moved at all.
        bool sampled = false;
    };
    std::vector<LimitState> m_jointLimits;
    QVector<EngineEvent> m_pendingEvents;

    // The editor's name for each shape handed to Box2D, indexed by what is
    // stashed in that shape's user data. Contacts are reported per shape, so
    // this is how an event says which one it happened to.
    QVector<QString> m_shapeNames;
    // Where a named shape ended up, so a rule can change one while running.
    QHash<QString, b2ShapeId> m_shapesByName;
    // Scene units per metre, straight from WorldDesc: what the solver is told
    // the shapes measure.
    qreal m_pixelsPerMeter = kReferencePixelsPerMeter;
    // kReferencePixelsPerMeter / m_pixelsPerMeter. Applied to gravity and
    // velocity so that changing the scale changes the sizes the solver sees
    // without changing anything on screen.
    qreal m_motionScale = 1.0;

    // Joints, indexed by the JointHandle handed back.
    QVector<b2JointId> m_joints;

    // Box2D's solver iteration count per step. 4 is the value Box2D's own
    // samples use; higher trades speed for stiffer stacks.
    static constexpr int kSubStepCount = 4;
};

} // namespace physics
