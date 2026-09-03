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

#include <QTimer>
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

void SimulationController::addFieldBounds()
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

        m_engine->addBody(desc);
    }
}

void SimulationController::start()
{
    if (isActive())
        return;

    m_engine = EngineRegistry::create(m_engineName);
    if (!m_engine)
        return;

    m_bound.clear();
    m_skippedBodies.clear();
    m_skippedJoints.clear();

    m_ruleState.clear();
    m_elapsedSeconds = 0.0;
    m_frameCount = 0;

    m_engine->createWorld(m_scene->toWorldDesc());

    QSet<QString> contactSources;
    for (const Rule &rule : m_scene->rules()) {
        if (rule.isEvent())
            contactSources.insert(rule.subjectName);
    }
    if (!contactSources.isEmpty()) {
        for (PhysicsBody *body : m_scene->bodies()) {
            const bool wholeBody = contactSources.contains(body->name());
            for (ShapeItem *shape : body->shapes()) {
                if (!wholeBody && !contactSources.contains(shape->name()))
                    continue;
                shape->part().enableContactEvents = true;
                shape->part().enableHitEvents = true;
            }
        }
    }

    for (PhysicsBody *body : m_scene->bodies()) {
        if (body->isEmpty() || !body->props().isEnabled)
            continue;

        const BodyDesc desc = body->toBodyDesc();
        const BodyHandle handle = m_engine->addBody(desc);
        if (handle == kInvalidBody) {
            m_skippedBodies << body->name();
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

        m_bound.append(bound);
    }

    if (m_bound.isEmpty()) {
        m_engine->destroyWorld();
        m_engine.reset();
        m_skippedBodies.clear();
        return;
    }

    QHash<const PhysicsBody *, BodyHandle> handles;
    m_bodyByName.clear();
    m_bodyNames.clear();
    for (const BoundBody &bound : m_bound) {
        handles.insert(bound.body, bound.handle);
        m_bodyByName.insert(bound.body->name(), bound.handle);
        if (m_bodyNames.size() <= bound.handle)
            m_bodyNames.resize(bound.handle + 1);
        m_bodyNames[bound.handle] = bound.body->name();
    }

    m_jointByName.clear();
    m_jointNames.clear();

    for (Joint *joint : m_scene->joints()) {
        const auto a = handles.constFind(joint->bodyA());
        const auto b = handles.constFind(joint->bodyB());
        if (a == handles.constEnd() || b == handles.constEnd()) {
            m_skippedJoints << joint->name();
            continue;
        }

        const JointHandle handle = m_engine->addJoint(joint->toJointDesc(*a, *b));
        if (handle == kInvalidJoint) {
            m_skippedJoints << joint->name();
            continue;
        }
        m_jointByName.insert(joint->name(), handle);
        if (m_jointNames.size() <= handle)
            m_jointNames.resize(handle + 1);
        m_jointNames[handle] = joint->name();
    }

    if (m_scene->fieldBoundsSolid())
        addFieldBounds();

    captureSnapshot();
    captureJointParams();

    m_state = State::Running;
    m_scene->setSimulationRunning(true);
    m_owedTime = 0.0;
    m_clock.start();
    m_timer->start();
    emit stateChanged();
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
    m_state = State::Stopped;
    m_scene->setSimulationRunning(false);
    emit stateChanged();
}

void SimulationController::stepOnce()
{
    if (!m_engine)
        return;

    const qreal elapsed = m_clock.restart() / 1000.0;
    m_owedTime += elapsed;

    int stepsTaken = 0;
    while (m_owedTime >= timeStep() && stepsTaken < kMaxStepsPerTick) {
        stepWorld(timeStep());
        m_owedTime -= timeStep();
        ++stepsTaken;
    }
    if (stepsTaken == 0)
        return; // nothing moved; no point rewriting every transform
    if (m_owedTime > timeStep() * kMaxStepsPerTick)
        m_owedTime = 0.0; // too far behind to catch up; drop the backlog

    syncTransforms();
    emit stepped();
}


void SimulationController::stepWorld(qreal dt)
{
    m_engine->step(dt);
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

    m_ruleState.resize(rules.size());

    for (int i = 0; i < rules.size(); ++i) {
        const Rule &rule = rules.at(i);
        if (!rule.enabled || !rule.isValid())
            continue;
        if (rule.once && m_ruleState[i].fired)
            continue;

        QString other;
        const bool nowTrue = evaluate(rule, raised, &other);

        const bool rising = nowTrue && !m_ruleState[i].wasTrue;
        m_ruleState[i].wasTrue = nowTrue;
        if (!rising)
            continue;

        Rule resolved = rule;
        if (resolved.targetName == Rule::otherObject()
            || resolved.targetName == Rule::otherObjectBody()) {
            if (other.isEmpty())
                continue; // nothing on the other side to act on

            resolved.targetName = other;
            if (rule.targetName == Rule::otherObjectBody()) {
                for (ShapeItem *shape : m_scene->shapes()) {
                    if (shape->name() == other && shape->body()) {
                        resolved.targetName = shape->body()->name();
                        break;
                    }
                }
            }
        }
        applyAction(resolved);
        m_ruleState[i].fired = true;
    }
}

bool SimulationController::evaluate(
    const Rule &rule, const QHash<QString, QHash<QString, QStringList>> &raised,
    QString *other) const
{
    if (rule.isEvent()) {
        const auto subject = raised.constFind(rule.subjectName);
        if (subject == raised.constEnd())
            return false;
        const auto ids = subject->constFind(rule.eventId);
        if (ids == subject->constEnd())
            return false;

        const QString wanted = rule.conditionValue.toString();
        for (const QString &partner : *ids) {
            if (wanted.isEmpty() || partner == wanted) {
                *other = partner;
                return true;
            }
        }
        return false;
    }

    const QVariant current = readValue(rule.subjectName, rule.conditionKey);
    if (!current.isValid())
        return false; // nothing by that name, or nothing readable by that key

    if (current.typeId() == QMetaType::Bool) {
        const bool a = current.toBool();
        const bool b = rule.conditionValue.toBool();
        switch (rule.compare) {
        case Rule::Compare::Equal:    return a == b;
        case Rule::Compare::NotEqual: return a != b;
        default:                      return false;
        }
    }

    const double a = current.toDouble();
    const double b = rule.conditionValue.toDouble();
    switch (rule.compare) {
    case Rule::Compare::Equal:        return qFuzzyCompare(a + 1.0, b + 1.0);
    case Rule::Compare::NotEqual:     return !qFuzzyCompare(a + 1.0, b + 1.0);
    case Rule::Compare::Greater:      return a > b;
    case Rule::Compare::Less:         return a < b;
    case Rule::Compare::GreaterEqual: return a >= b;
    case Rule::Compare::LessEqual:    return a <= b;
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
        return m_engine->worldValue(key);
    }

    const auto joint = m_jointByName.constFind(name);
    if (joint != m_jointByName.constEnd())
        return m_engine->jointValue(*joint, key);

    const auto body = m_bodyByName.constFind(name);
    if (body != m_bodyByName.constEnd())
        return m_engine->bodyValue(*body, key);

    return m_engine->shapeValue(name, key);
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

void SimulationController::applyAction(const Rule &rule)
{
    // An action is performed on the named body rather than written to it.
    if (rule.isAction()) {
        if (!m_engine)
            return;

        // Whatever the rule stored, over the top of what the engine says the
        // action starts at. A rule written before the card could edit these
        // carried none at all, and performing a blast of radius zero looks
        // exactly like the rule not firing.
        QVariantMap params = defaultsFor(rule.actionId);
        for (auto it = rule.actionParams.constBegin();
             it != rule.actionParams.constEnd(); ++it)
            params.insert(it.key(), it.value());

        // A explosion is a bare coordinate -- it has no body to name.
        // An explosion carries its own settings; the rule only says when.
        if (ExplosionItem *explosion = m_scene->explosionNamed(rule.targetName)) {
            for (auto it = explosion->params().constBegin();
                 it != explosion->params().constEnd(); ++it)
                params.insert(it.key(), it.value());
            m_engine->performActionAt(rule.actionId, explosion->pos(), params);
            return;
        }
        // A joint has its own actions -- breaking is not something that can be
        // done to a body, and removing is not something that can be done to a
        // joint, so which list the id came from follows from what was named.
        const auto joint = m_jointByName.constFind(rule.targetName);
        if (joint != m_jointByName.constEnd()) {
            m_engine->performJointAction(rule.actionId, *joint, params);
            // The scene still holds the joint -- the document is not touched by
            // a run -- so it is marked instead, and stops being drawn until the
            // run ends.
            for (Joint *item : m_scene->joints()) {
                if (item->name() == rule.targetName)
                    item->setBroken(true);
            }
            return;
        }
        for (int i = 0; i < m_bodyNames.size(); ++i) {
            if (m_bodyNames[i] != rule.targetName)
                continue;
            m_engine->performAction(rule.actionId, static_cast<physics::BodyHandle>(i),
                                    params);
            return;
        }
        // A shape was named: the action lands on the body that owns it.
        for (ShapeItem *shape : m_scene->shapes()) {
            if (shape->name() != rule.targetName || !shape->body())
                continue;
            const int index = m_bodyNames.indexOf(shape->body()->name());
            if (index >= 0)
                m_engine->performAction(rule.actionId,
                                        static_cast<physics::BodyHandle>(index),
                                        params);
            return;
        }
        return;
    }

    bool isShapeProperty = false;
    if (m_engine) {
        for (const physics::JointParam &p : m_engine->shapeProperties())
            isShapeProperty = isShapeProperty || p.key == rule.propertyKey;
    }

    const QString target = rule.targetName;
    Q_UNUSED(isShapeProperty);

    // A literal, or whatever the named property reads right now plus an
    // offset. Read once per firing, so every branch below sees the same value.
    const QVariant applied =
        rule.usesSource()
            ? QVariant(readValue(rule.sourceObject, rule.sourceProperty).toDouble()
                       + rule.sourceOffset)
            : rule.value;

    const auto compute = [&rule, applied](const QVariant &current) {
        switch (rule.op) {
        case Rule::Op::Set:    return applied;
        case Rule::Op::Toggle: return QVariant(!current.toBool());
        case Rule::Op::Negate: return QVariant(-current.toDouble());
        case Rule::Op::Add:    return QVariant(current.toDouble() + applied.toDouble());
        }
        return applied;
    };

    // The world is not in the scene, so it is answered before anything is
    // looked up by name. Nothing of it is stored in the document: the change
    // lasts as long as the run does.
    if (target == Rule::world()) {
        m_engine->setWorldParam(rule.propertyKey,
                                compute(m_engine->worldValue(rule.propertyKey)));
        return;
    }

    for (Joint *joint : m_scene->joints()) {
        if (joint->name() != target)
            continue;
        const auto it = m_jointByName.constFind(target);
        if (it == m_jointByName.constEnd())
            return; // the joint exists but this run skipped it

        const QVariant updated = compute(joint->params().value(rule.propertyKey));
        joint->params().insert(rule.propertyKey, updated);
        m_engine->setJointParam(*it, rule.propertyKey, updated);
        return;
    }

    for (PhysicsBody *body : m_scene->bodies()) {
        if (body->name() != target)
            continue;
        const auto it = m_bodyByName.constFind(target);
        if (it == m_bodyByName.constEnd())
            return;

        const QVariant updated = compute(m_engine->bodyValue(*it, rule.propertyKey));
        m_engine->setBodyParam(*it, rule.propertyKey, updated);
        return;
    }

    for (ShapeItem *shape : m_scene->shapes()) {
        if (shape->name() != target)
            continue;
        const QVariant updated = compute(m_engine->shapeValue(shape->name(), rule.propertyKey));
        m_engine->setShapeParam(shape->name(), rule.propertyKey, updated);
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
