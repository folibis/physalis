#include "SimulationController.h"

#include "CanvasScene.h"
#include "ExplosionItem.h"
#include "RayItem.h"
#include "ShapeItem.h"

#include <QSet>
#include "PhysicsBody.h"
#include "Joint.h"

#include <QHash>
#include "EngineRegistry.h"
#include "Naming.h"
#include "SceneSerializer.h"

#include <QTimer>
#include <QtMath>
#include <cmath>
#include <QElapsedTimer>
#include <QTransform>

using namespace physics;

SimulationController::SimulationController(CanvasScene *scene, QObject *parent)
    : QObject(parent)
    , m_scene(scene)
    , m_timer(new QTimer(this))
{
    const QStringList engines = EngineRegistry::availableEngines();
    m_engineName = engines.isEmpty() ? QString() : engines.first();

    m_timer->setTimerType(Qt::PreciseTimer);
    m_timer->setInterval(qMax(1, int(timeStep() * 1000.0) / 2));
    connect(m_timer, &QTimer::timeout, this, &SimulationController::stepOnce);
}

SimulationController::~SimulationController()
{
    m_timer->stop();
}

void SimulationController::setEngineName(const QString &name)
{
    if (isActive() || m_engineName == name)
        return;
    m_engineName = name;
    emit stateChanged();
}

void SimulationController::captureSnapshot()
{
    m_snapshot.clear();
    for (const BoundBody &bound : m_bound) {
        for (const BoundShape &boundShape : bound.shapes)
            m_snapshot.push_back({boundShape.shape, boundShape.shape->pos(), boundShape.shape->rotation()});
    }
}

void SimulationController::captureJointParams()
{
    m_jointParamSnapshot.clear();
    for (Joint *joint : m_scene->joints())
        m_jointParamSnapshot.insert(joint, joint->params());
}

void SimulationController::restoreJointParams()
{
    for (auto it = m_jointParamSnapshot.cbegin(); it != m_jointParamSnapshot.cend(); ++it) {
        if (!it.key())
            continue;
        if (it.key()->params() == it.value())
            continue;
        it.key()->params() = it.value();
        it.key()->notifyPropertyChanged();
    }
    m_jointParamSnapshot.clear();
}

void SimulationController::restoreSnapshot()
{
    for (const Snapshot &entry : m_snapshot) {
        entry.shape->setPos(entry.pos);
        entry.shape->setRotation(entry.rotation);
    }
    m_snapshot.clear();
}

void SimulationController::withSceneAsStarted(const std::function<void()> &use)
{
    if (!isActive()) {
        use();
        return;
    }

    // Everything restoreSnapshot, restoreJointParams and stop() put back, set
    // aside rather than thrown away, so the run carries on as it was.
    QVector<Snapshot> running;
    running.reserve(m_snapshot.size());
    for (const Snapshot &entry : m_snapshot) {
        running.push_back({ entry.shape, entry.shape->pos(), entry.shape->rotation() });
        entry.shape->setPos(entry.pos);
        entry.shape->setRotation(entry.rotation);
    }
    QHash<Joint *, QVariantMap> runningParams;
    for (auto it = m_jointParamSnapshot.cbegin(); it != m_jointParamSnapshot.cend(); ++it) {
        if (!it.key() || it.key()->params() == it.value())
            continue;
        runningParams.insert(it.key(), it.key()->params());
        it.key()->params() = it.value();
    }
    QVector<PhysicsBody *> removed;
    for (const BoundBody &bound : m_bound) {
        if (bound.body->isRemoved()) {
            removed.append(bound.body);
            bound.body->setRemoved(false);
        }
    }
    QVector<Joint *> broken;
    for (Joint *joint : m_scene->joints()) {
        if (joint->isBroken()) {
            broken.append(joint);
            joint->setBroken(false);
        }
    }

    use();

    for (Joint *joint : broken)
        joint->setBroken(true);
    for (PhysicsBody *body : removed)
        body->setRemoved(true);
    for (auto it = runningParams.cbegin(); it != runningParams.cend(); ++it)
        it.key()->params() = it.value();
    for (const Snapshot &entry : running) {
        entry.shape->setPos(entry.pos);
        entry.shape->setRotation(entry.rotation);
    }
}

void SimulationController::addFieldBounds(IPhysicsEngine *engine) const
{
    const QRectF field = m_scene->sceneRect();
    if (field.isEmpty())
        return;

    constexpr qreal kThickness = 40.0;

    const QVector<QRectF> walls {
        QRectF(field.left(), field.top() - kThickness, field.width(), kThickness),    // top
        QRectF(field.left(), field.bottom(), field.width(), kThickness),              // bottom
        QRectF(field.left() - kThickness, field.top(), kThickness, field.height()),   // left
        QRectF(field.right(), field.top(), kThickness, field.height()),               // right
    };

    for (const QRectF &wall : walls) {
        BodyDesc desc;
        desc.type = BodyType::Static;
        desc.name = QStringLiteral("field_bounds");
        desc.position = wall.center();

        ShapePart part;
        part.geometry.kind = GeometryKind::Box;
        part.geometry.halfExtents = QPointF(wall.width() / 2.0, wall.height() / 2.0);
        desc.parts.append(part);

        engine->addBody(desc);
    }
}

void SimulationController::start()
{
    if (isActive())
        return;
    dropPreview();

    m_engine = EngineRegistry::create(m_engineName);
    if (!m_engine)
        return;

    m_bound.clear();
    m_skippedBodies.clear();
    m_skippedJoints.clear();

    m_ruleState.clear();
    // No step before the first one, so nothing has changed yet.
    m_watchedNow.clear();
    m_watchedBefore.clear();
    m_problems.clear();
    m_pendingRunAction.clear();
    m_elapsedSeconds = 0.0;
    m_frameCount = 0;

    QSet<QString> contactSources;
    for (const Rule &rule : m_scene->rules()) {
        for (const RuleCondition &condition : rule.conditions) {
            if (condition.isEvent())
                contactSources.insert(condition.subjectName);
        }
    }
    if (!contactSources.isEmpty()) {
        for (PhysicsBody *body : m_scene->bodies()) {
            const bool wholeBody = contactSources.contains(body->name());
            for (ShapeItem *shape : body->shapes()) {
                if (!wholeBody && !contactSources.contains(shape->name()))
                    continue;
                // Whatever an engine needs switched on to report contacts,
                // it switches on itself: the editor only says which shapes a
                // rule is watching.
                shape->part().watchedByRules = true;
            }
        }
    }

    Built built;
    const bool anything = buildWorld(m_engine.get(), &built);
    m_skippedBodies = built.skippedBodies;
    m_skippedJoints = built.skippedJoints;
    if (!anything) {
        m_engine.reset();
        m_skippedBodies.clear();
        return;
    }
    m_bound = built.bound;
    m_bodyByName = built.bodyByName;
    m_jointByName = built.jointByName;
    m_bodyNames.clear();
    for (const BoundBody &bound : std::as_const(m_bound)) {
        if (m_bodyNames.size() <= bound.handle)
            m_bodyNames.resize(bound.handle + 1);
        m_bodyNames[bound.handle] = bound.body->name();
    }
    m_jointNames.clear();
    for (auto it = m_jointByName.cbegin(); it != m_jointByName.cend(); ++it) {
        if (m_jointNames.size() <= it.value())
            m_jointNames.resize(it.value() + 1);
        m_jointNames[it.value()] = it.key();
    }

    captureSnapshot();
    captureJointParams();

    m_state = State::Running;
    m_scene->setSimulationRunning(true);
    m_owedTime = 0.0;
    m_clock.start();
    m_timer->start();
    // Where every body stands as the run begins, for "Init state" -- taken
    // before the start rules move anything.
    m_startPoses.clear();
    for (const BoundBody &bound : std::as_const(m_bound)) {
        const physics::BodyDesc desc = bound.body->toBodyDesc();
        m_startPoses.insert(bound.body, qMakePair(desc.position, desc.rotationDegrees));
    }
    applyStartRules();
    emit stateChanged();
}

bool SimulationController::buildWorld(IPhysicsEngine *engine, Built *built) const
{
    engine->createWorld(m_scene->toWorldDesc());

    for (PhysicsBody *body : m_scene->bodies()) {
        if (body->isEmpty() || !body->props().isEnabled)
            continue;

        const BodyDesc desc = body->toBodyDesc();
        const BodyHandle handle = engine->addBody(desc);
        if (handle == kInvalidBody) {
            built->skippedBodies << body->name();
            continue;
        }

        BoundBody bound;
        bound.body = body;
        bound.handle = handle;

        QTransform bodyToScene;
        bodyToScene.translate(desc.position.x(), desc.position.y());
        bodyToScene.rotate(desc.rotationDegrees);
        const QTransform sceneToBody = bodyToScene.inverted();

        for (ShapeItem *shape : body->shapes()) {
            BoundShape boundShape;
            boundShape.shape = shape;
            boundShape.localPivot = sceneToBody.map(shape->pos() + shape->origin());
            boundShape.localRotation = shape->rotation() - desc.rotationDegrees;
            bound.shapes.append(boundShape);
        }

        built->bound.append(bound);
        built->bodyByName.insert(body->name(), handle);
    }

    if (built->bound.isEmpty()) {
        engine->destroyWorld();
        return false;
    }

    QHash<const PhysicsBody *, BodyHandle> handles;
    for (const BoundBody &bound : std::as_const(built->bound))
        handles.insert(bound.body, bound.handle);

    for (Joint *joint : m_scene->joints()) {
        const auto a = handles.constFind(joint->bodyA());
        if (a == handles.constEnd()) {
            built->skippedJoints << joint->name();
            continue;
        }
        // No second body means the joint holds this one to a point in the
        // world; the engine fills Box2D's other slot itself.
        int handleB = -1;
        if (joint->bodyB()) {
            const auto b = handles.constFind(joint->bodyB());
            if (b == handles.constEnd()) {
                built->skippedJoints << joint->name();
                continue;
            }
            handleB = *b;
        }

        const JointHandle handle = engine->addJoint(joint->toJointDesc(*a, handleB));
        if (handle == kInvalidJoint) {
            built->skippedJoints << joint->name();
            continue;
        }
        built->jointByName.insert(joint->name(), handle);
    }

    if (m_scene->fieldBoundsSolid())
        addFieldBounds(engine);
    return true;
}

QVariant SimulationController::initialValue(const QString &name, const QString &key)
{
    if (isActive())
        return readValue(name, key);

    if (name == Rule::world()) {
        if (key == QLatin1String("time") || key == QLatin1String("frame"))
            return 0.0;
    }
    if (m_scene->rayNamed(name))
        return {};

    if (!m_preview) {
        m_preview = EngineRegistry::create(m_engineName);
        if (!m_preview)
            return {};
        m_previewBuilt = Built();
        if (!buildWorld(m_preview.get(), &m_previewBuilt)) {
            m_preview.reset();
            return {};
        }
        QTimer::singleShot(0, this, &SimulationController::dropPreview);
    }
    return readFrom(m_preview.get(), m_previewBuilt.bodyByName, m_previewBuilt.jointByName, name, key);
}

void SimulationController::dropPreview()
{
    if (!m_preview)
        return;
    m_preview->destroyWorld();
    m_preview.reset();
    m_previewBuilt = Built();
}

QVariant SimulationController::readFrom(const IPhysicsEngine *engine,
                                        const QHash<QString, BodyHandle> &bodies,
                                        const QHash<QString, JointHandle> &joints,
                                        const QString &name, const QString &key)
{
    if (name == Rule::world())
        return engine->worldValue(key);

    const auto joint = joints.constFind(name);
    if (joint != joints.constEnd())
        return engine->jointValue(*joint, key);

    const auto body = bodies.constFind(name);
    if (body != bodies.constEnd())
        return engine->bodyValue(*body, key);

    return engine->shapeValue(name, key);
}

void SimulationController::initState(PhysicsBody *body)
{
    if (!m_engine || !body || body->isRemoved())
        return;
    const auto handle = m_bodyByName.constFind(body->name());
    const auto pose = m_startPoses.constFind(body);
    if (handle == m_bodyByName.constEnd() || pose == m_startPoses.constEnd())
        return;

    // Which properties place and move a body is the engine's to say.
    const physics::PropertyList properties = m_engine->bodyProperties();
    const auto set = [&](physics::PropertyRole role, const QVariant &value) {
        const QString key = physics::keyForRole(properties, role);
        if (!key.isEmpty())
            m_engine->setBodyParam(*handle, key, value);
    };
    set(physics::PropertyRole::PositionX, pose->first.x());
    set(physics::PropertyRole::PositionY, pose->first.y());
    set(physics::PropertyRole::Angle, pose->second);
    set(physics::PropertyRole::VelocityX, 0.0);
    set(physics::PropertyRole::VelocityY, 0.0);
    set(physics::PropertyRole::AngularVelocity, 0.0);
}

void SimulationController::cloneBody(PhysicsBody *parent, const QPointF &at)
{
    if (!m_engine || !parent || parent->isEmpty())
        return;

    // Where each shape stood as the run started. A body the run has moved, or
    // taken away, is cloned as it was drawn; a clone of a clone, which was
    // never in the snapshot, as it stands.
    QHash<ShapeItem *, const Snapshot *> startOf;
    for (const Snapshot &entry : std::as_const(m_snapshot))
        startOf.insert(entry.shape, &entry);

    PhysicsBody *body = m_scene->createEmptyBody(false);
    body->setRunOnly(true);
    // Straight into the props, not through setName: a name change is passed on
    // to every rule naming the old one, and the old one here is the parent's.
    const QString name = Naming::makeUnique(parent->name(), m_scene->takenNames(body));
    body->props() = parent->props();
    body->props().name = name;
    body->shot() = parent->shot();

    for (ShapeItem *original : parent->shapes()) {
        ShapeItem *copy = SceneSerializer::shapeFromJson(SceneSerializer::shapeToJson(original));
        if (!copy)
            continue;
        if (const Snapshot *start = startOf.value(original)) {
            copy->setPos(start->pos);
            copy->setRotation(start->rotation);
        } else {
            copy->setPos(original->pos());
            copy->setRotation(original->rotation());
        }
        copy->setVisible(true);
        copy->part().watchedByRules = original->part().watchedByRules;
        // Named before it joins the scene, for the reason the body's name went
        // straight into its props above: a shape renamed on the canvas passes
        // the new name to every rule and log row naming the old one, and the
        // old name here is the shape this was copied from. Renamed after
        // addItem, the first clone repointed the rules at itself -- and when
        // the run ended and the clones went, they named nothing at all.
        copy->setName(Naming::makeUnique(original->name(), m_scene->takenNames(copy)));
        m_scene->addItem(copy);
        body->addShape(copy);
    }
    if (body->isEmpty()) {
        m_scene->destroyBody(body, false);
        return;
    }

    // The body's origin onto the point asked for, every shape moved with it.
    const QPointF shift = at - body->originScenePos();
    for (ShapeItem *shape : body->shapes())
        shape->setPos(shape->pos() + shift);

    const physics::BodyDesc desc = body->toBodyDesc();
    const physics::BodyHandle handle = m_engine->addBody(desc);
    if (handle == physics::kInvalidBody) {
        m_problems << tr("%1 could not be cloned").arg(parent->name());
        const QVector<ShapeItem *> shapes = body->shapes();
        for (ShapeItem *shape : shapes) {
            body->removeShape(shape);
            delete shape;
        }
        m_scene->destroyBody(body, false);
        emit stateChanged();
        return;
    }

    BoundBody bound;
    bound.body = body;
    bound.handle = handle;
    QTransform bodyToScene;
    bodyToScene.translate(desc.position.x(), desc.position.y());
    bodyToScene.rotate(desc.rotationDegrees);
    const QTransform sceneToBody = bodyToScene.inverted();
    for (ShapeItem *shape : body->shapes()) {
        BoundShape boundShape;
        boundShape.shape = shape;
        boundShape.localPivot = sceneToBody.map(shape->pos() + shape->origin());
        boundShape.localRotation = shape->rotation() - desc.rotationDegrees;
        bound.shapes.append(boundShape);
    }
    m_bound.append(bound);
    m_bodyByName.insert(name, handle);
    if (m_bodyNames.size() <= handle)
        m_bodyNames.resize(handle + 1);
    m_bodyNames[handle] = name;
    // "Init state" on a clone puts it back where it was made.
    m_startPoses.insert(body, qMakePair(desc.position, desc.rotationDegrees));
    m_clones.append(body);
    m_scene->update();
}

void SimulationController::deleteClones()
{
    for (PhysicsBody *body : std::as_const(m_clones)) {
        m_startPoses.remove(body);
        m_bodyByName.remove(body->name());
        const QVector<ShapeItem *> shapes = body->shapes();
        for (ShapeItem *shape : shapes) {
            body->removeShape(shape);
            delete shape;
        }
        m_scene->destroyBody(body, false);
    }
    m_clones.clear();
}

void SimulationController::applyStartRules()
{
    const QVector<Rule> &rules = m_scene->rules();
    m_ruleState.resize(rules.size());

    // The one event the application raises here, phrased the way the engine's
    // own events are, so a rule joining it to a reading -- "at the start, if the
    // ball is above the line" -- is judged by the same code as any other.
    QHash<QString, QHash<QString, QStringList>> raised;
    raised[Rule::world()][Rule::runStartedEvent()] << QString();

    bool carried = false;
    for (int i = 0; i < rules.size(); ++i) {
        const Rule &rule = rules.at(i);
        if (!rule.enabled || !rule.isValid())
            continue;
        // Only rules that actually name the start. Anything else whose
        // conditions happen to be true as the run begins waits for a step, the
        // way it always has.
        bool namesStart = false;
        for (const RuleCondition &condition : rule.conditions) {
            namesStart = namesStart || (condition.subjectName == Rule::world()
                                        && condition.eventId == Rule::runStartedEvent());
        }
        if (!namesStart)
            continue;

        QString other;
        if (!evaluate(rule, raised, &other))
            continue;

        // True now, so it does not fire again on the first step for having
        // become true between here and there.
        m_ruleState[i].wasTrue = true;
        for (const RuleAction &action : rule.actions)
            applyAction(action, firstSubject(rule));
        m_ruleState[i].fired = true;
        carried = true;
    }
    // Whatever the rules moved is drawn where they put it before anything
    // steps; the snapshot Stop restores was taken before they ran.
    if (carried && m_engine)
        syncTransforms();
    // A start rule may stop the run or hold it.
    applyPendingRunAction();
}

void SimulationController::setStepsPerSecond(int stepsPerSecond)
{
    stepsPerSecond = qBound(15, stepsPerSecond, 480);
    if (m_stepsPerSecond == stepsPerSecond)
        return;
    m_stepsPerSecond = stepsPerSecond;

    m_timer->setInterval(qMax(1, int(timeStep() * 1000.0) / 2));
    emit stateChanged();
}

void SimulationController::setSpeed(qreal speed)
{
    speed = qBound(0.1, speed, 8.0);
    if (qFuzzyCompare(m_speed, speed))
        return;
    m_speed = speed;
    // Whatever was owed was owed at the old pace; starting the new one clean
    // keeps a change of speed from spending a backlog all at once.
    m_owedTime = 0.0;
    if (m_clock.isValid())
        m_clock.restart();
    emit stateChanged();
}

void SimulationController::pause()
{
    if (m_state != State::Running)
        return;
    m_timer->stop();
    m_state = State::Stepping;
    emit stateChanged();
}

void SimulationController::resume()
{
    if (m_state != State::Stepping)
        return;
    m_owedTime = 0.0;
    m_clock.restart();
    m_timer->start();
    m_state = State::Running;
    emit stateChanged();
}

void SimulationController::stepFrame()
{
    if (m_state == State::Stopped) {
        start();
        if (m_state == State::Stopped)
            return; // nothing to simulate
    }

    m_timer->stop();
    m_state = State::Stepping;

    stepWorld(timeStep());
    syncTransforms();

    m_owedTime = 0.0;
    m_clock.restart();
    emit stateChanged();
    applyPendingRunAction();
}

void SimulationController::stop()
{
    if (!isActive())
        return;

    m_timer->stop();
    restoreSnapshot();
    restoreJointParams();

    // A reading only means something while a world exists.
    for (RayItem *ray : m_scene->rays())
        ray->clearReading();

    if (m_engine)
        m_engine->destroyWorld();
    m_engine.reset();

    for (const BoundBody &bound : m_bound) {
        bound.body->setAsleep(false);
        // A body a rule removed was only removed from the world; the scene
        // still has it, and the run is over.
        bound.body->setRemoved(false);
    }
    for (Joint *joint : m_scene->joints())
        joint->setBroken(false);

    m_bound.clear();
    deleteClones();
    m_state = State::Stopped;
    m_scene->setSimulationRunning(false);
    emit stateChanged();
}

void SimulationController::stepOnce()
{
    advance(m_clock.restart() / 1000.0);
}

void SimulationController::advance(qreal wallSeconds)
{
    if (!m_engine)
        return;
    // Wall-clock time only moves a run that is running. Paused, the timer is
    // stopped and this is never called -- but a paused run that steps when
    // somebody hands it the clock is wrong on its own terms, and Step is what
    // moves a paused run, one step at a time.
    if (m_state != State::Running)
        return;

    // Played at twice the speed, a second of ours is two of the world's, so it
    // is the time that is multiplied and never the step: the solver is handed
    // the same slice it always was, just more often.
    m_owedTime += wallSeconds * m_speed;

    // The ceiling on catching up rises with the speed for the same reason --
    // at x4 a tick is worth four times the steps, and a fixed five would cap
    // the run at a quarter of what was asked for.
    const int maxSteps = qMax(1, qCeil(kMaxStepsPerTick * m_speed));

    int stepsTaken = 0;
    while (m_owedTime >= timeStep() && stepsTaken < maxSteps) {
        stepWorld(timeStep());
        m_owedTime -= timeStep();
        ++stepsTaken;
    }
    if (stepsTaken == 0)
        return; // nothing moved; no point rewriting every transform
    if (m_owedTime > timeStep() * maxSteps)
        m_owedTime = 0.0; // too far behind to catch up; drop the backlog

    syncTransforms();
    emit stepped();
    applyPendingRunAction();
}

void SimulationController::applyPendingRunAction()
{
    const QString action = m_pendingRunAction;
    m_pendingRunAction.clear();
    if (action == Rule::stopRunAction())
        stop();
    else if (action == Rule::holdRunAction())
        pause();
}

void SimulationController::shoot(PhysicsBody *body, const QPointF &impulse)
{
    if (!m_engine || !body || body->isRemoved())
        return;
    const auto handle = m_bodyByName.constFind(body->name());
    if (handle == m_bodyByName.constEnd())
        return; // not in this run: disabled, or skipped for having no mass

    // Which properties push is the engine's to say. One that offers no way to
    // push a body has nothing for the slingshot, and the shot is not taken.
    const physics::PropertyList properties = m_engine->bodyProperties();
    const QString keyX = physics::keyForRole(properties, physics::PropertyRole::ImpulseX);
    const QString keyY = physics::keyForRole(properties, physics::PropertyRole::ImpulseY);
    if (!keyX.isEmpty() && !qFuzzyIsNull(impulse.x()))
        m_engine->setBodyParam(*handle, keyX, impulse.x());
    if (!keyY.isEmpty() && !qFuzzyIsNull(impulse.y()))
        m_engine->setBodyParam(*handle, keyY, impulse.y());
}


void SimulationController::stepWorld(qreal dt)
{
    m_engine->step(dt);
    // Anything the engine could not do; kept for the window to show. A scene
    // can ask a solver for the impossible, and being told is better than
    // watching a body vanish without a word.
    const QStringList problems = m_engine->takeProblems();
    if (!problems.isEmpty()) {
        m_problems += problems;
        emit stateChanged();
    }
    syncRays();
    m_elapsedSeconds += dt;
    ++m_frameCount;
    applyRules();
}

void SimulationController::applyRules()
{
    const QVector<Rule> &rules = m_scene->rules();
    if (rules.isEmpty())
        return;

    QHash<QString, QHash<QString, QStringList>> raised;
    for (const EngineEvent &event : m_engine->pollEvents()) {
        const QString other = event.otherShape.isEmpty()
                                  ? (event.otherBody != kInvalidBody
                                     && event.otherBody < m_bodyNames.size()
                                         ? m_bodyNames[event.otherBody]
                                         : QString())
                                  : event.otherShape;

        if (!event.subjectShape.isEmpty())
            raised[event.subjectShape][event.eventId] << other;
        if (event.body != kInvalidBody && event.body < m_bodyNames.size())
            raised[m_bodyNames[event.body]][event.eventId] << other;
        if (event.joint >= 0 && event.joint < m_jointNames.size())
            raised[m_jointNames[event.joint]][event.eventId] << other;
    }

    // What each ray is looking at, phrased as an event so a rule can name the
    // shape it cares about -- the same way a contact rule names the other side.
    for (RayItem *ray : m_scene->rays()) {
        if (!ray->hitName().isEmpty())
            raised[ray->name()][QStringLiteral("rayDetects")] << ray->hitName();
    }

    // What every "changed to" and "changed from" condition is looking at, read
    // once before any rule fires. One snapshot for the whole step, so every
    // condition sees the same value however far down the list it sits and
    // whatever the rules above it have already done.
    m_watchedNow.clear();
    for (const Rule &rule : rules) {
        if (!rule.enabled || !rule.isValid())
            continue;
        for (const RuleCondition &condition : rule.conditions) {
            if (!condition.watchesChange())
                continue;
            const QString key = changeKey(condition);
            if (!m_watchedNow.contains(key))
                m_watchedNow.insert(key, readValue(condition.subjectName, condition.conditionKey));
        }
    }

    m_ruleState.resize(rules.size());

    for (int i = 0; i < rules.size(); ++i) {
        const Rule &rule = rules.at(i);
        if (!rule.enabled || !rule.isValid())
            continue;
        if (rule.once && m_ruleState[i].fired)
            continue;

        QString other;
        const bool nowTrue = evaluate(rule, raised, &other);

        // One verdict for the whole card, so a rule with several conditions
        // fires as the combination becomes true rather than once per condition.
        const bool rising = nowTrue && !m_ruleState[i].wasTrue;
        m_ruleState[i].wasTrue = nowTrue;
        if (!rising)
            continue;

        // Every action, in the order they are listed. One that cannot be
        // resolved -- it names the other object and there was none -- is passed
        // over on its own; the rest still happen.
        bool carried = false;
        for (const RuleAction &action : rule.actions) {
            RuleAction resolved = action;
            if (!resolveOther(&resolved, other))
                continue;
            applyAction(resolved, firstSubject(rule));
            carried = true;
        }
        if (carried)
            m_ruleState[i].fired = true;
    }

    // This step's readings are the step before's from here on. Anything no
    // longer watched drops out with them.
    m_watchedBefore = m_watchedNow;
}

QString SimulationController::changeKey(const RuleCondition &condition)
{
    // The previous value belongs to the property, not to the rule reading it,
    // so two rules watching the same thing share one.
    return condition.subjectName + QLatin1Char('\n') + condition.conditionKey;
}

// Equality as the conditions mean it: a flag against a flag, anything else as
// a number. The same test "equals" uses, so "changed to true" and "equals
// true" agree about what true is.
bool SimulationController::sameValue(const QVariant &a, const QVariant &b)
{
    if (a.userType() == QMetaType::Bool || b.userType() == QMetaType::Bool)
        return a.toBool() == b.toBool();
    return qFuzzyCompare(a.toDouble() + 1.0, b.toDouble() + 1.0);
}

// The name a rule's actions answer to when one of them removes a body: the
// first thing the rule watches, which is what "the other object" means to
// whatever is being taken away.
QString SimulationController::firstSubject(const Rule &rule)
{
    return rule.conditions.isEmpty() ? QString() : rule.conditions.first().subjectName;
}

// Turns "@other" into the name the conditions actually found. False when the
// action wanted one and there is none, which is not an error -- an event that
// names nobody simply cannot drive an action aimed at somebody.
bool SimulationController::resolveOther(RuleAction *action, const QString &other) const
{
    if (action->targetName != Rule::otherObject()
        && action->targetName != Rule::otherObjectBody()) {
        return true;
    }
    if (other.isEmpty())
        return false;

    const bool wantsBody = action->targetName == Rule::otherObjectBody();
    action->targetName = other;
    if (wantsBody) {
        for (ShapeItem *shape : m_scene->shapes()) {
            if (shape->name() == other && shape->body()) {
                action->targetName = shape->body()->name();
                break;
            }
        }
    }
    return true;
}

bool SimulationController::evaluate(
    const Rule &rule, const QHash<QString, QHash<QString, QStringList>> &raised,
    QString *other) const
{
    if (rule.conditions.isEmpty())
        return false;

    // Every condition is judged, none skipped, even once the answer is settled:
    // the object an action calls "the other" comes from whichever condition
    // found one, and short-circuiting past a true event would lose it.
    bool anyTrue = false;
    bool allTrue = true;
    for (const RuleCondition &condition : rule.conditions) {
        QString partner;
        const bool met = evaluateOne(condition, raised, &partner);
        anyTrue = anyTrue || met;
        allTrue = allTrue && met;
        // The first event that both happened and named somebody. A reading
        // names nobody, so it never claims the slot.
        if (met && other->isEmpty() && !partner.isEmpty())
            *other = partner;
    }

    return rule.join == Rule::Join::Any ? anyTrue : allTrue;
}

bool SimulationController::evaluateOne(
    const RuleCondition &condition, const QHash<QString, QHash<QString, QStringList>> &raised,
    QString *other) const
{
    if (condition.isEvent()) {
        const auto subject = raised.constFind(condition.subjectName);
        if (subject == raised.constEnd())
            return false;
        const auto ids = subject->constFind(condition.eventId);
        if (ids == subject->constEnd())
            return false;

        const QString wanted = condition.conditionValue.toString();
        for (const QString &partner : *ids) {
            if (wanted.isEmpty() || partner == wanted) {
                *other = partner;
                return true;
            }
        }
        return false;
    }

    if (condition.watchesChange()) {
        const QString key = changeKey(condition);
        const auto before = m_watchedBefore.constFind(key);
        // No step before this one, so nothing has changed yet. A run does not
        // begin by firing every rule that watches a change.
        if (before == m_watchedBefore.constEnd())
            return false;

        const QVariant now = m_watchedNow.value(key);
        if (!now.isValid() || sameValue(*before, now))
            return false;

        return condition.compare == Rule::Compare::ChangedTo
                   ? sameValue(now, condition.conditionValue)
                   : sameValue(*before, condition.conditionValue);
    }

    const QVariant current = readValue(condition.subjectName, condition.conditionKey);
    if (!current.isValid())
        return false; // nothing by that name, or nothing readable by that key

    if (current.userType() == QMetaType::Bool) {
        const bool a = current.toBool();
        const bool b = condition.conditionValue.toBool();
        switch (condition.compare) {
        case Rule::Compare::Equal:    return a == b;
        case Rule::Compare::NotEqual: return a != b;
        default:                      return false;
        }
    }

    const double a = current.toDouble();
    const double b = condition.conditionValue.toDouble();
    switch (condition.compare) {
    case Rule::Compare::Equal:        return qFuzzyCompare(a + 1.0, b + 1.0);
    case Rule::Compare::NotEqual:     return !qFuzzyCompare(a + 1.0, b + 1.0);
    case Rule::Compare::Greater:      return a > b;
    case Rule::Compare::Less:         return a < b;
    case Rule::Compare::GreaterEqual: return a >= b;
    case Rule::Compare::LessEqual:    return a <= b;
    case Rule::Compare::Multiple: {
        const qint64 step = qRound64(b);
        const qint64 whole = qRound64(a);
        return step != 0 && whole != 0 && whole % step == 0;
    }
    case Rule::Compare::ChangedTo:
    case Rule::Compare::ChangedFrom:
        break; // answered above, where the step before is in reach
    }
    return false;
}

QVariant SimulationController::readValue(const QString &name, const QString &key) const
{
    // The run itself. Answered before any lookup, since nothing in the scene
    // is called this.
    if (RayItem *ray = m_scene->rayNamed(name)) {
        if (key == QLatin1String("distance"))
            return ray->distance();
        if (key == QLatin1String("hit"))
            return ray->hasHit();
        if (key == QLatin1String("hitName"))
            return ray->hitName();
        if (key == QLatin1String("hitX"))
            return ray->mapToScene(ray->hitPoint()).x();
        if (key == QLatin1String("hitY"))
            return ray->mapToScene(ray->hitPoint()).y();
        return {};
    }

    if (name == Rule::world()) {
        if (key == QLatin1String("time"))
            return m_elapsedSeconds;
        if (key == QLatin1String("frame"))
            return double(m_frameCount);
        // Everything else the world can be asked is the engine's to answer.
    }
    return readFrom(m_engine.get(), m_bodyByName, m_jointByName, name, key);
}

QVariantMap SimulationController::defaultsFor(const QString &actionId) const
{
    QVariantMap params;
    if (!m_engine)
        return params;
    for (const physics::ActionType &action : m_engine->bodyActions() + m_engine->jointActions()) {
        if (action.id != actionId)
            continue;
        for (const physics::JointParam &param : action.params)
            params.insert(param.key, param.defaultValue);
    }
    return params;
}

void SimulationController::takeOutOfView(PhysicsBody *body)
{
    if (!body || body->isRemoved())
        return;

    // The body and its shapes first -- setRemoved() hides those.
    body->setRemoved(true);

    // Then the joints. The engine destroyed them along with the body, and
    // nothing in the scene would know: a joint left attached to a body that
    // has just vanished goes on being drawn between it and thin air.
    for (Joint *joint : m_scene->joints()) {
        if ((joint->bodyA() && joint->bodyA() == body)
            || (joint->bodyB() && joint->bodyB() == body))
            joint->setBroken(true);
    }

    // The axes are painted in the foreground, which no shape's own repaint
    // covers.
    m_scene->update();
}

bool SimulationController::removalHandled(PhysicsBody *body, const QString &remover)
{
    if (m_handlingRemoval || !body)
        return false;

    const QVector<Rule> &rules = m_scene->rules();
    m_ruleState.resize(rules.size());

    // A rule can watch the body or any of its shapes.
    QStringList names { body->name() };
    for (ShapeItem *shape : body->shapes())
        names << shape->name();
    // "The body that touched it", for an answer aimed at the remover's body.
    QString removerBody = remover;
    for (ShapeItem *shape : m_scene->shapes()) {
        if (shape->name() == remover && shape->body()) {
            removerBody = shape->body()->name();
            break;
        }
    }

    QVector<int> answers;
    for (int i = 0; i < rules.size(); ++i) {
        const Rule &rule = rules.at(i);
        if (!rule.enabled || !rule.isValid())
            continue;
        if (rule.once && m_ruleState[i].fired)
            continue;
        // Any condition watching one of these names for the removal is an
        // answer. The other conditions are not weighed: a body is being taken
        // away this instant, and there is no step in which to judge a reading.
        bool answers_ = false;
        for (const RuleCondition &condition : rule.conditions) {
            answers_ = answers_ || (condition.eventId == Rule::aboutToBeRemovedEvent()
                                    && names.contains(condition.subjectName));
        }
        if (answers_)
            answers.append(i);
    }
    if (answers.isEmpty())
        return false;

    m_handlingRemoval = true;
    for (int i : answers) {
        const Rule &rule = rules.at(i);
        for (const RuleAction &action : rule.actions) {
            RuleAction resolved = action;
            if (resolved.targetName == Rule::otherObject())
                resolved.targetName = remover;
            else if (resolved.targetName == Rule::otherObjectBody())
                resolved.targetName = removerBody;
            applyAction(resolved, firstSubject(rule));
        }
        m_ruleState[i].fired = true;
    }
    m_handlingRemoval = false;
    return true;
}

void SimulationController::applyAction(const RuleAction &action,
                                       const QString &subjectName)
{
    // Ending or holding the run is not something the engine can do -- and it
    // cannot be done here either, in the middle of a step, with the solver on
    // the stack. It is remembered and carried out once the step is finished.
    if (Rule::isRunAction(action)) {
        m_pendingRunAction = action.actionId;
        return;
    }

    // So is "Clone": no engine knows the canvas the copy has to appear on.
    if (action.actionId == Rule::cloneAction()) {
        PhysicsBody *parent = nullptr;
        for (PhysicsBody *body : m_scene->bodies()) {
            if (body->name() == action.targetName)
                parent = body;
        }
        for (ShapeItem *shape : m_scene->shapes()) {
            if (!parent && shape->name() == action.targetName)
                parent = shape->body();
        }
        cloneBody(parent, QPointF(action.actionParams.value(Rule::cloneXParam()).toDouble(),
                                  action.actionParams.value(Rule::cloneYParam()).toDouble()));
        return;
    }

    // "Init state" is the application's: it knows where the run started.
    if (action.actionId == Rule::initStateAction()) {
        PhysicsBody *target = nullptr;
        for (PhysicsBody *body : m_scene->bodies()) {
            if (body->name() == action.targetName)
                target = body;
        }
        for (ShapeItem *shape : m_scene->shapes()) {
            if (!target && shape->name() == action.targetName)
                target = shape->body();
        }
        initState(target);
        return;
    }

    // An action is performed on the named body rather than written to it.
    if (action.isAction()) {
        if (!m_engine)
            return;

        // Whatever the rule stored, over the top of what the engine says the
        // action starts at. A rule written before the card could edit these
        // carried none at all, and performing a blast of radius zero looks
        // exactly like the rule not firing.
        QVariantMap params = defaultsFor(action.actionId);
        for (auto it = action.actionParams.constBegin();
             it != action.actionParams.constEnd(); ++it)
            params.insert(it.key(), it.value());

        // A explosion is a bare coordinate -- it has no body to name.
        // An explosion carries its own settings; the rule only says when.
        if (ExplosionItem *explosion = m_scene->explosionNamed(action.targetName)) {
            for (auto it = explosion->params().constBegin();
                 it != explosion->params().constEnd(); ++it)
                params.insert(it.key(), it.value());
            m_engine->performActionAt(action.actionId, explosion->pos(), params);
            return;
        }
        // A joint has its own actions -- breaking is not something that can be
        // done to a body, and removing is not something that can be done to a
        // joint, so which list the id came from follows from what was named.
        const auto joint = m_jointByName.constFind(action.targetName);
        if (joint != m_jointByName.constEnd()) {
            m_engine->performJointAction(action.actionId, *joint, params);
            // The scene still holds the joint -- the document is not touched by
            // a run -- so it is marked instead, and stops being drawn until the
            // run ends.
            for (Joint *item : m_scene->joints()) {
                if (item->name() == action.targetName)
                    item->setBroken(true);
            }
            return;
        }
        // About to take a body away: a rule answering "is about to be removed"
        // is carried out instead, and the body stays. Which actions remove a
        // body is the engine's to say.
        bool removes = false;
        for (const physics::ActionType &candidate : m_engine->bodyActions())
            removes = removes || (candidate.id == action.actionId && candidate.removesBody);
        if (removes) {
            PhysicsBody *victim = nullptr;
            for (PhysicsBody *body : m_scene->bodies()) {
                if (body->name() == action.targetName)
                    victim = body;
            }
            for (ShapeItem *shape : m_scene->shapes()) {
                if (!victim && shape->name() == action.targetName)
                    victim = shape->body();
            }
            if (victim && removalHandled(victim, subjectName))
                return;
        }

        for (int i = 0; i < m_bodyNames.size(); ++i) {
            if (m_bodyNames[i] != action.targetName)
                continue;
            m_engine->performAction(action.actionId, static_cast<physics::BodyHandle>(i),
                                    params);
            return;
        }
        // A shape was named: the action lands on the body that owns it.
        for (ShapeItem *shape : m_scene->shapes()) {
            if (shape->name() != action.targetName || !shape->body())
                continue;
            const int index = m_bodyNames.indexOf(shape->body()->name());
            if (index >= 0)
                m_engine->performAction(action.actionId,
                                        static_cast<physics::BodyHandle>(index),
                                        params);
            return;
        }
        return;
    }

    bool isShapeProperty = false;
    if (m_engine) {
        for (const physics::JointParam &p : m_engine->shapeProperties())
            isShapeProperty = isShapeProperty || p.key == action.propertyKey;
    }

    const QString target = action.targetName;
    Q_UNUSED(isShapeProperty);

    // A literal, or whatever the named property reads right now plus an
    // offset. Read once per firing, so every branch below sees the same value.
    const QVariant applied =
        action.usesSource()
            ? QVariant(readValue(action.sourceObject, action.sourceProperty).toDouble()
                       + action.sourceOffset)
            : action.value;

    const auto compute = [&action, applied](const QVariant &current) {
        switch (action.op) {
        case Rule::Op::Set:    return applied;
        case Rule::Op::Toggle: return QVariant(!current.toBool());
        case Rule::Op::Negate: return QVariant(-current.toDouble());
        case Rule::Op::Add:    return QVariant(current.toDouble() + applied.toDouble());
        case Rule::Op::Subtract:
            return QVariant(current.toDouble() - applied.toDouble());
        }
        return applied;
    };

    // The world is not in the scene, so it is answered before anything is
    // looked up by name. Nothing of it is stored in the document: the change
    // lasts as long as the run does.
    if (target == Rule::world()) {
        m_engine->setWorldParam(action.propertyKey,
                                compute(m_engine->worldValue(action.propertyKey)));
        return;
    }

    for (Joint *joint : m_scene->joints()) {
        if (joint->name() != target)
            continue;
        const auto it = m_jointByName.constFind(target);
        if (it == m_jointByName.constEnd())
            return; // the joint exists but this run skipped it

        const QVariant updated = compute(joint->params().value(action.propertyKey));
        joint->params().insert(action.propertyKey, updated);
        m_engine->setJointParam(*it, action.propertyKey, updated);
        return;
    }

    for (PhysicsBody *body : m_scene->bodies()) {
        if (body->name() != target)
            continue;
        const auto it = m_bodyByName.constFind(target);
        if (it == m_bodyByName.constEnd())
            return;

        const QVariant updated = compute(m_engine->bodyValue(*it, action.propertyKey));
        m_engine->setBodyParam(*it, action.propertyKey, updated);
        return;
    }

    for (ShapeItem *shape : m_scene->shapes()) {
        if (shape->name() != target)
            continue;
        const QVariant updated = compute(m_engine->shapeValue(shape->name(), action.propertyKey));
        m_engine->setShapeParam(shape->name(), action.propertyKey, updated);
        return;
    }
}

void SimulationController::syncRays()
{
    for (RayItem *ray : m_scene->rays()) {
        const physics::RayHit found =
            m_engine->castRay(ray->pos(), ray->reach(), ray->maskBits());
        ray->setReading(found.hit, found.hit ? ray->mapFromScene(found.point) : QPointF(),
                        found.distance, found.shapeName);
    }
}

void SimulationController::syncTransforms()
{
    for (const BoundBody &bound : m_bound) {
        const BodyState state = m_engine->bodyState(bound.handle);
        // Gone from the world. Asking the engine rather than watching for a
        // particular action means anything that ends up removing a body is
        // taken off the canvas the same way.
        if (!state.exists) {
            takeOutOfView(bound.body);
            continue;
        }
        // A body the solver lost the arithmetic for comes back as a position
        // that is not a number. Writing that onto a QGraphicsItem paints
        // nothing and fills the log with NaN warnings from every path built
        // out of it, so the wreck is left where it was last seen -- the engine
        // has already taken it out of the world and said so.
        if (!std::isfinite(state.position.x()) || !std::isfinite(state.position.y())
            || !std::isfinite(state.rotationDegrees))
            continue;
        bound.body->setAsleep(!state.awake);

        QTransform bodyToScene;
        bodyToScene.translate(state.position.x(), state.position.y());
        bodyToScene.rotate(state.rotationDegrees);

        for (const BoundShape &boundShape : bound.shapes) {
            ShapeItem *shape = boundShape.shape;
            const QPointF pivotScene = bodyToScene.map(boundShape.localPivot);
            shape->setRotation(state.rotationDegrees + boundShape.localRotation);
            shape->setPos(pivotScene - shape->origin());
        }
    }
}
