// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ruslan Muhlinin. See LICENSE.
#pragma once

#include <QObject>
#include <QPointF>
#include <QElapsedTimer>
#include <QString>
#include <QStringList>
#include <QHash>
#include <QVector>
#include <functional>
#include <memory>

#include "IPhysicsEngine.h"
#include "Rule.h"

class CanvasScene;
class ShapeItem;
class Joint;
class PhysicsBody;
class QTimer;

class SimulationController : public QObject
{
    Q_OBJECT

public:
    SimulationController(CanvasScene *scene, QObject *parent = nullptr);
    ~SimulationController() override;

    enum class State { Stopped, Running, Stepping };
    State state() const { return m_state; }
    bool isActive() const { return m_state != State::Stopped; }
    bool isRunning() const { return m_state == State::Running; }

    QString engineName() const { return m_engineName; }

    // One readable property of one named object, in scene units -- the same
    // reading the rules do. The log uses it to show live values.
    QVariant readValue(const QString &name, const QString &key) const;
    // The same reading before a run: what the object starts with -- its mass,
    // its speed from the velocity it is given -- asked of a world built from
    // the scene and never stepped. During a run it is readValue.
    QVariant initialValue(const QString &name, const QString &key);
    void setEngineName(const QString &name);

    QStringList skippedBodies() const { return m_skippedBodies; }
    // What went wrong while it ran, in the engine's words: a body the solver
    // could not keep a number for, a step it could not finish.
    QStringList problems() const { return m_problems; }
    QStringList skippedJoints() const { return m_skippedJoints; }

public slots:
    void start();
    int stepsPerSecond() const { return m_stepsPerSecond; }
    void setStepsPerSecond(int stepsPerSecond);

    // How fast the run plays against the clock on the wall: 2.0 covers two
    // seconds of the world in one of ours, 0.5 covers half. The step the solver
    // is handed never changes -- a bigger one is a different simulation, not a
    // faster one -- so this only decides how many of them a tick is worth.
    qreal speed() const { return m_speed; }
    void setSpeed(qreal speed);

    // Feed the run this much wall-clock time. The timer hands over whatever
    // really passed; a test hands over whatever it likes.
    void advance(qreal wallSeconds);

    // A push through a body's centre of mass, in the units the engine's
    // impulse properties take -- what the slingshot hands over on release.
    void shoot(PhysicsBody *body, const QPointF &impulse);

    // Runs `use` with the scene as it stood when the run started -- every
    // shape where it was, joint settings a rule has since changed put back,
    // removed bodies and broken joints whole -- and the run's state back
    // afterwards. What an export mid-run has to see: the scene, not a moment
    // of the run. Stopped, it is just `use`.
    void withSceneAsStarted(const std::function<void()> &use);

    void pause();
    void resume();
    void stepFrame();
    // Tears the world down and restores the pre-simulation transforms.
    void stop();

signals:
    void stateChanged();
    // Emitted after each step, for readouts that follow a run.
    void stepped();

private:
    void stepOnce();
    void syncTransforms();
    void captureSnapshot();
    void captureJointParams();
    void restoreJointParams();
    void restoreSnapshot();
    void addFieldBounds(physics::IPhysicsEngine *engine) const;

    struct BoundShape {
        ShapeItem *shape = nullptr;
        QPointF localPivot;          // shape's pivot in body-local coordinates
        qreal localRotation = 0.0;   // shape's angle relative to the body's
    };

    struct BoundBody {
        PhysicsBody *body = nullptr;
        physics::BodyHandle handle = physics::kInvalidBody;
        QVector<BoundShape> shapes;
    };

    struct Snapshot {
        ShapeItem *shape = nullptr;
        QPointF pos;
        qreal rotation = 0.0;
    };

    // Builds the scene into an engine's fresh world: every enabled body, the
    // joints between them and the field's walls. False, with the world taken
    // down again, when there was no body to build.
    struct Built {
        QVector<BoundBody> bound;
        QHash<QString, physics::BodyHandle> bodyByName;
        QHash<QString, physics::JointHandle> jointByName;
        QStringList skippedBodies;
        QStringList skippedJoints;
    };
    bool buildWorld(physics::IPhysicsEngine *engine, Built *built) const;
    static QVariant readFrom(const physics::IPhysicsEngine *engine,
                             const QHash<QString, physics::BodyHandle> &bodies,
                             const QHash<QString, physics::JointHandle> &joints,
                             const QString &name, const QString &key);

    // The unstepped world initialValue reads. Kept for the rest of this pass of
    // the event loop, so a property table asking a dozen rows builds it once,
    // and dropped after, so it never answers for a scene that has changed.
    std::unique_ptr<physics::IPhysicsEngine> m_preview;
    Built m_previewBuilt;
    void dropPreview();

    CanvasScene *m_scene = nullptr;
    std::unique_ptr<physics::IPhysicsEngine> m_engine;
    QTimer *m_timer = nullptr;
    State m_state = State::Stopped;
    QString m_engineName;

    QVector<BoundBody> m_bound;

    QHash<QString, physics::BodyHandle> m_bodyByName;
    QHash<QString, physics::JointHandle> m_jointByName;
    QVector<QString> m_jointNames;
    QVector<QString> m_bodyNames;

    struct RuleState { bool wasTrue = false; bool fired = false; };
    QVector<RuleState> m_ruleState;

    void applyRules();
    // One verdict for the whole card: every condition judged, then joined with
    // all-of or any-of. `other` comes back as whichever condition named one.
    bool evaluate(const Rule &rule,
                  const QHash<QString, QHash<QString, QStringList>> &raised,
                  QString *other) const;
    // The scene's variables while a run is going: started from what each one
    // declares and gone when it stops, like every position on the canvas.
    QVariantMap m_variables;

    // What a "changed to" or "changed from" condition reads, this step and the
    // step before. Keyed by the property rather than by the rule, since the
    // value it had belongs to the property.
    static QString changeKey(const RuleCondition &condition);
    static bool sameValue(const QVariant &a, const QVariant &b);
    QHash<QString, QVariant> m_watchedNow;
    QHash<QString, QVariant> m_watchedBefore;

    bool evaluateOne(const RuleCondition &condition,
                     const QHash<QString, QHash<QString, QStringList>> &raised,
                     QString *other) const;

    // `subjectName` is what the rule watches, which is what an answer to a
    // removal means by "the other object".
    void applyAction(const RuleAction &action, const QString &subjectName);
    static QString firstSubject(const Rule &rule);
    bool resolveOther(RuleAction *action, const QString &other) const;
    // A rule is about to remove this body. Carries out every rule answering
    // "is about to be removed" and says whether there were any -- if so, the
    // removal does not happen. `remover` is the subject of the removing rule.
    bool removalHandled(PhysicsBody *body, const QString &remover);
    // Carries out the rules on the world's "starting simulation", once, with
    // the world built and before its first step.
    void applyStartRules();
    // "Init state": the body back where it stood when the run started, facing
    // the same way, and not moving.
    void initState(PhysicsBody *body);
    // "Clone": a copy of the body as the run found it, placed with its origin
    // at `at`, added to the canvas and to the world.
    void cloneBody(PhysicsBody *parent, const QPointF &at);
    // What cloneBody made, deleted when the run ends.
    QVector<PhysicsBody *> m_clones;
    void deleteClones();
    // Where each body stood, and which way it faced, as the run started.
    QHash<PhysicsBody *, QPair<QPointF, qreal>> m_startPoses;
    // Set while the answers are carried out, so an answer that removes the
    // body really removes it instead of asking again for ever.
    bool m_handlingRemoval = false;
    // Takes a body the engine no longer has off the canvas -- its shapes, its
    // axes, and the joints the engine destroyed along with it. Run state only;
    // stop() puts every bit of it back.
    void takeOutOfView(PhysicsBody *body);
    QVariantMap defaultsFor(const QString &actionId) const;
    void stepWorld(qreal dt);
    void syncRays();

    // Counted from the moment the run started, not from when the app did.
    qreal m_elapsedSeconds = 0.0;
    qint64 m_frameCount = 0;
    QVector<Snapshot> m_snapshot;
    // A joint's parameters as the document holds them, taken at the start of a
    // run. Rules write live values over the top so the property table follows
    // the run, and this puts the document back when the run ends -- otherwise
    // a rule's change outlives the run, poisons the next one, and gets saved.
    QHash<Joint *, QVariantMap> m_jointParamSnapshot;
    QStringList m_skippedBodies;
    QStringList m_problems;
    QStringList m_skippedJoints;

    QElapsedTimer m_clock;
    qreal m_owedTime = 0.0;

    // A rule asked for the run to end or to hold. It cannot happen where the
    // rule fires -- that is inside the step -- so it waits until the step is
    // over. Empty when nothing is pending.
    QString m_pendingRunAction;
    void applyPendingRunAction();

    int m_stepsPerSecond = 60;
    qreal m_speed = 1.0;
    qreal timeStep() const { return 1.0 / m_stepsPerSecond; }
    static constexpr int kMaxStepsPerTick = 5;
};
