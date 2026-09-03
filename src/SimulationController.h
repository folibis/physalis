#pragma once

#include <QObject>
#include <QPointF>
#include <QElapsedTimer>
#include <QString>
#include <QStringList>
#include <QHash>
#include <QVector>
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
    void setEngineName(const QString &name);

    QStringList skippedBodies() const { return m_skippedBodies; }
    QStringList skippedJoints() const { return m_skippedJoints; }

public slots:
    void start();
    int stepsPerSecond() const { return m_stepsPerSecond; }
    void setStepsPerSecond(int stepsPerSecond);

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
    void addFieldBounds();

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
    bool evaluate(const Rule &rule,
                  const QHash<QString, QHash<QString, QStringList>> &raised,
                  QString *other) const;

    void applyAction(const Rule &rule);
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
    QStringList m_skippedJoints;

    QElapsedTimer m_clock;
    qreal m_owedTime = 0.0;

    int m_stepsPerSecond = 60;
    qreal timeStep() const { return 1.0 / m_stepsPerSecond; }
    static constexpr int kMaxStepsPerTick = 5;
};
