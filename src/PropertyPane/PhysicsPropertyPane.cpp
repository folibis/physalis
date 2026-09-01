#include "PhysicsPropertyPane.h"

#include <QHash>
#include "../CanvasScene.h"
#include "../ShapeItem.h"
#include "../PhysicsBody.h"

namespace {

QString bitsToText(quint64 bits)
{
    return QStringLiteral("0x%1").arg(bits, 16, 16, QLatin1Char('0'));
}

quint64 textToBits(const QString &text, quint64 fallback)
{
    QString trimmed = text.trimmed();
    if (trimmed.startsWith(QLatin1String("0x"), Qt::CaseInsensitive))
        trimmed = trimmed.mid(2);
    bool ok = false;
    const quint64 value = trimmed.toULongLong(&ok, 16);
    return ok ? value : fallback;
}

const QString &bodySection()
{
    static const QString s = QObject::tr("Body");
    return s;
}

const QString &shapeSection()
{
    static const QString s = QObject::tr("Shape");
    return s;
}

const QString &collisionSection()
{
    static const QString s = QObject::tr("Collision");
    return s;
}

} // namespace

std::vector<PropertyRow> PhysicsPropertyPane::rows(EditorMode mode) const
{
    std::vector<PropertyRow> result;
    if (!m_scene || mode == EditorMode::Edit)
        return result;

    const QVector<ShapeItem *> &selection = m_scene->physicsSelection();
    if (selection.isEmpty())
        return result;

    const auto append = [&result](std::vector<PropertyRow> rows) {
        for (PropertyRow &row : rows)
            result.push_back(std::move(row));
    };

    if (PhysicsBody *body = m_scene->commonSelectedBody()) {
        const auto notify = [body] { body->notifyPropertyChanged(); };
        result.push_back(bodyTypeRow(&body->props(), notify));
        append(bodyIdentityRows(body));
        append(bodyPropRows(&body->props(), notify));
    }

    ShapeItem *shape = selection.first();
    append(shapeIdentityRows(shape));
    append(shapePropRows(
        &shape->part(), [shape] { shape->notifyPropertyChanged(); },
        [pane = const_cast<PhysicsPropertyPane *>(this)] {
            // Queued: the rebuild deletes the very checkbox whose signal is
            // still on the stack.
            QMetaObject::invokeMethod(pane, [pane] { emit pane->rowsChanged(); },
                                      Qt::QueuedConnection);
        }));

    return result;
}

std::vector<PropertyRow> PhysicsPropertyPane::defaultRows(EditorMode mode) const
{
    std::vector<PropertyRow> result;
    if (mode == EditorMode::Edit)
        return result;

    static physics::BodyDesc pristineBody;
    static physics::ShapePart pristinePart;

    result.push_back(bodyTypeRow(&pristineBody, [] {}));
    for (PropertyRow &row : bodyPropRows(&pristineBody, [] {}))
        result.push_back(std::move(row));
    for (PropertyRow &row : shapePropRows(&pristinePart, [] {}))
        result.push_back(std::move(row));

    return result;
}

PropertyRow PhysicsPropertyPane::bodyTypeRow(physics::BodyDesc *props,
                                             const std::function<void()> &changed)
{
    static const physics::BodyType kBodyTypes[] = {
        physics::BodyType::Static, physics::BodyType::Kinematic, physics::BodyType::Dynamic
    };
    return {QObject::tr("Type"), PropertyFieldType::Choice,
        [props] {
            for (int i = 0; i < 3; ++i) {
                if (kBodyTypes[i] == props->type)
                    return i;
            }
            return 2;
        },
        [props, changed](const QVariant &v) { props->type = kBodyTypes[qBound(0, v.toInt(), 2)]; changed(); },
        -100000.0, 100000.0,
        {QObject::tr("Static"), QObject::tr("Kinematic"), QObject::tr("Dynamic")}, -1, 0.0,
        bodySection()};
}

std::vector<PropertyRow> PhysicsPropertyPane::bodyIdentityRows(PhysicsBody *body)
{
    std::vector<PropertyRow> result;
    const QString &section = bodySection();

    result.push_back({QObject::tr("Name"), PropertyFieldType::String,
        [body] { return body->name(); },
        [body](const QVariant &v) { body->setName(v.toString()); },
        -100000.0, 100000.0, {}, -1, 0.0, section});

    result.push_back({QObject::tr("Shapes"), PropertyFieldType::String,
        [body] { return QString::number(body->shapes().size()); },
        [](const QVariant &) {},   // read-only: membership is changed on the canvas
        -100000.0, 100000.0, {}, -1, 0.0, section});

    return result;
}

std::vector<PropertyRow> PhysicsPropertyPane::shapeIdentityRows(ShapeItem *shape)
{
    std::vector<PropertyRow> result;
    const QString &section = shapeSection();

    result.push_back({QObject::tr("Name"), PropertyFieldType::String,
        [shape] { return shape->name(); },
        [shape](const QVariant &v) { shape->setName(v.toString()); },
        -100000.0, 100000.0, {}, -1, 0.0, section});

    PropertyRow body;
    body.label = QObject::tr("Body");
    body.type = PropertyFieldType::String;
    body.getter = [shape] { return shape->body() ? shape->body()->name() : QObject::tr("(none)"); };
    body.setter = [](const QVariant &) {};
    body.section = section;
    body.readOnly = true;   // grouping is done on the canvas, not by typing a name
    result.push_back(std::move(body));

    return result;
}

namespace {

// The engine's name for each row that has one. Only these can be logged: the
// log reads values back out of the running world, and a row the engine cannot
// name is a row it cannot read.
void tagEngineKeys(std::vector<PropertyRow> &rows)
{
    static const QHash<QString, QString> keys = {
        { QObject::tr("Enabled"),                  QStringLiteral("isEnabled") },
        { QObject::tr("Velocity X (m/s)"),         QStringLiteral("velocityX") },
        { QObject::tr("Velocity Y (m/s)"),         QStringLiteral("velocityY") },
        { QObject::tr("Angular Velocity (deg/s)"), QStringLiteral("angularVelocity") },
        { QObject::tr("Linear Damping"),           QStringLiteral("linearDamping") },
        { QObject::tr("Angular Damping"),          QStringLiteral("angularDamping") },
        { QObject::tr("Gravity Scale"),            QStringLiteral("gravityScale") },
        { QObject::tr("Fixed Rotation"),           QStringLiteral("fixedRotation") },
        { QObject::tr("Bullet (fast CCD)"),        QStringLiteral("isBullet") },
        { QObject::tr("Enable Sleep"),             QStringLiteral("enableSleep") },
        { QObject::tr("Density (kg/m²)"),      QStringLiteral("density") },
        { QObject::tr("Friction"),                 QStringLiteral("friction") },
        { QObject::tr("Restitution"),              QStringLiteral("restitution") },
    };
    for (PropertyRow &row : rows) {
        if (row.key.isEmpty())
            row.key = keys.value(row.label);
    }
}

} // namespace

std::vector<PropertyRow> PhysicsPropertyPane::bodyPropRows(physics::BodyDesc *props,
                                                           const std::function<void()> &changed)
{
    std::vector<PropertyRow> result;
    const QString &section = bodySection();

    // What the solver produces. Read-only -- moving a body mid-run is
    // teleporting it, which is a different operation -- but loggable, which is
    // the point: these are the values worth watching while a run is going.
    for (const auto &live : { qMakePair(QObject::tr("Position X"), QStringLiteral("positionX")),
                              qMakePair(QObject::tr("Position Y"), QStringLiteral("positionY")),
                              qMakePair(QObject::tr("Angle (deg)"), QStringLiteral("angle")),
                              qMakePair(QObject::tr("Speed"), QStringLiteral("speed")) }) {
        PropertyRow row;
        row.label = live.first;
        row.key = live.second;
        row.type = PropertyFieldType::Numeric;
        row.section = section;
        row.decimals = 2;
        row.getter = [] { return QVariant(); };   // filled by the engine while running
        row.setter = [](const QVariant &) {};
        row.readOnly = true;
        result.push_back(std::move(row));
    }

    result.push_back({QObject::tr("Enabled"), PropertyFieldType::Boolean,
        [props] { return props->isEnabled; },
        [props, changed](const QVariant &v) { props->isEnabled = v.toBool(); changed(); },
        -100000.0, 100000.0, {}, -1, 0.0, section});

    result.push_back({QObject::tr("Velocity X (m/s)"), PropertyFieldType::Numeric,
        [props] { return props->linearVelocity.x(); },
        [props, changed](const QVariant &v) { props->linearVelocity.setX(v.toDouble()); changed(); },
        -1000.0, 1000.0, {}, 2, 0.1, section});

    result.push_back({QObject::tr("Velocity Y (m/s)"), PropertyFieldType::Numeric,
        [props] { return props->linearVelocity.y(); },
        [props, changed](const QVariant &v) { props->linearVelocity.setY(v.toDouble()); changed(); },
        -1000.0, 1000.0, {}, 2, 0.1, section});

    result.push_back({QObject::tr("Angular Velocity (deg/s)"), PropertyFieldType::Numeric,
        [props] { return props->angularVelocityDegrees; },
        [props, changed](const QVariant &v) { props->angularVelocityDegrees = v.toDouble(); changed(); },
        -36000.0, 36000.0, {}, 1, 1.0, section});

    result.push_back({QObject::tr("Linear Damping"), PropertyFieldType::Numeric,
        [props] { return props->linearDamping; },
        [props, changed](const QVariant &v) { props->linearDamping = qMax(0.0, v.toDouble()); changed(); },
        0.0, 100.0, {}, 2, 0.05, section});

    result.push_back({QObject::tr("Angular Damping"), PropertyFieldType::Numeric,
        [props] { return props->angularDamping; },
        [props, changed](const QVariant &v) { props->angularDamping = qMax(0.0, v.toDouble()); changed(); },
        0.0, 100.0, {}, 2, 0.05, section});

    result.push_back({QObject::tr("Gravity Scale"), PropertyFieldType::Numeric,
        [props] { return props->gravityScale; },
        [props, changed](const QVariant &v) { props->gravityScale = v.toDouble(); changed(); },
        -100.0, 100.0, {}, 2, 0.1, section});

    result.push_back({QObject::tr("Fixed Rotation"), PropertyFieldType::Boolean,
        [props] { return props->fixedRotation; },
        [props, changed](const QVariant &v) { props->fixedRotation = v.toBool(); changed(); },
        -100000.0, 100000.0, {}, -1, 0.0, section});

    result.push_back({QObject::tr("Bullet (fast CCD)"), PropertyFieldType::Boolean,
        [props] { return props->isBullet; },
        [props, changed](const QVariant &v) { props->isBullet = v.toBool(); changed(); },
        -100000.0, 100000.0, {}, -1, 0.0, section});

    result.push_back({QObject::tr("Allow Fast Rotation"), PropertyFieldType::Boolean,
        [props] { return props->allowFastRotation; },
        [props, changed](const QVariant &v) { props->allowFastRotation = v.toBool(); changed(); },
        -100000.0, 100000.0, {}, -1, 0.0, section});

    result.push_back({QObject::tr("Enable Sleep"), PropertyFieldType::Boolean,
        [props] { return props->enableSleep; },
        [props, changed](const QVariant &v) { props->enableSleep = v.toBool(); changed(); },
        -100000.0, 100000.0, {}, -1, 0.0, section});

    result.push_back({QObject::tr("Start Awake"), PropertyFieldType::Boolean,
        [props] { return props->isAwake; },
        [props, changed](const QVariant &v) { props->isAwake = v.toBool(); changed(); },
        -100000.0, 100000.0, {}, -1, 0.0, section});

    result.push_back({QObject::tr("Sleep Threshold (m/s)"), PropertyFieldType::Numeric,
        [props] { return props->sleepThreshold; },
        [props, changed](const QVariant &v) { props->sleepThreshold = qMax(0.0, v.toDouble()); changed(); },
        0.0, 100.0, {}, 3, 0.01, section});


    tagEngineKeys(result);
    return result;
}

std::vector<PropertyRow> PhysicsPropertyPane::shapePropRows(physics::ShapePart *part,
                                                            const std::function<void()> &changed,
                                                            const std::function<void()> &relayout)
{
    std::vector<PropertyRow> result;
    const QString &section = shapeSection();

    result.push_back({QObject::tr("Density (kg/m²)"), PropertyFieldType::Numeric,
        [part] { return part->density; },
        [part, changed](const QVariant &v) { part->density = qMax(0.0, v.toDouble()); changed(); },
        0.0, 10000.0, {}, 2, 0.1, section});

    // Friction, bounce and the rest act on a contact response, and a sensor
    // never produces one -- so they are not offered for one. Density stays:
    // Box2D takes mass from every shape with a density, sensor or not.
    if (!part->isSensor) {
        result.push_back({QObject::tr("Friction"), PropertyFieldType::Numeric,
            [part] { return part->material.friction; },
            [part, changed](const QVariant &v) { part->material.friction = qMax(0.0, v.toDouble()); changed(); },
            0.0, 10.0, {}, 2, 0.05, section});

        result.push_back({QObject::tr("Restitution"), PropertyFieldType::Numeric,
            [part] { return part->material.restitution; },
            [part, changed](const QVariant &v) { part->material.restitution = qMax(0.0, v.toDouble()); changed(); },
            0.0, 10.0, {}, 2, 0.05, section});

        result.push_back({QObject::tr("Rolling Resistance"), PropertyFieldType::Numeric,
            [part] { return part->material.rollingResistance; },
            [part, changed](const QVariant &v) {
                part->material.rollingResistance = qMax(0.0, v.toDouble());
                changed();
            },
            0.0, 10.0, {}, 2, 0.05, section});

        result.push_back({QObject::tr("Tangent Speed (m/s)"), PropertyFieldType::Numeric,
            [part] { return part->material.tangentSpeed; },
            [part, changed](const QVariant &v) { part->material.tangentSpeed = v.toDouble(); changed(); },
            -1000.0, 1000.0, {}, 2, 0.1, section});
    }

    const QString &collision = collisionSection();

    result.push_back({QObject::tr("Sensor"), PropertyFieldType::Boolean,
        [part] { return part->isSensor; },
        [part, changed, relayout](const QVariant &v) {
            part->isSensor = v.toBool();
            changed();
            // Friction and the rest are only offered to a shape that collides,
            // so the rows themselves change with this flag.
            if (relayout)
                relayout();
        },
        -100000.0, 100000.0, {}, -1, 0.0, collision});

    result.push_back({QObject::tr("Category Bits"), PropertyFieldType::String,
        [part] { return bitsToText(part->filter.categoryBits); },
        [part, changed](const QVariant &v) {
            part->filter.categoryBits = textToBits(v.toString(), part->filter.categoryBits);
            changed();
        },
        -100000.0, 100000.0, {}, -1, 0.0, collision});

    result.push_back({QObject::tr("Mask Bits"), PropertyFieldType::String,
        [part] { return bitsToText(part->filter.maskBits); },
        [part, changed](const QVariant &v) {
            part->filter.maskBits = textToBits(v.toString(), part->filter.maskBits);
            changed();
        },
        -100000.0, 100000.0, {}, -1, 0.0, collision});

    result.push_back({QObject::tr("Group Index"), PropertyFieldType::Numeric,
        [part] { return part->filter.groupIndex; },
        [part, changed](const QVariant &v) { part->filter.groupIndex = v.toInt(); changed(); },
        -32768.0, 32767.0, {}, 0, 1.0, collision});

    // Event reporting is off by default in Box2D because it costs time.

    // A sensor raises overlap events, never contact ones, so the contact and
    // hit switches would do nothing on it.
    if (!part->isSensor) {
        result.push_back({QObject::tr("Contact Events"), PropertyFieldType::Boolean,
            [part] { return part->enableContactEvents; },
            [part, changed](const QVariant &v) { part->enableContactEvents = v.toBool(); changed(); },
            -100000.0, 100000.0, {}, -1, 0.0, collision});
    }

    result.push_back({QObject::tr("Sensor Events"), PropertyFieldType::Boolean,
        [part] { return part->enableSensorEvents; },
        [part, changed](const QVariant &v) { part->enableSensorEvents = v.toBool(); changed(); },
        -100000.0, 100000.0, {}, -1, 0.0, collision});

    if (!part->isSensor) {
        result.push_back({QObject::tr("Hit Events"), PropertyFieldType::Boolean,
            [part] { return part->enableHitEvents; },
            [part, changed](const QVariant &v) { part->enableHitEvents = v.toBool(); changed(); },
            -100000.0, 100000.0, {}, -1, 0.0, collision});
    }

    result.push_back({QObject::tr("Pre-Solve Events"), PropertyFieldType::Boolean,
        [part] { return part->enablePreSolveEvents; },
        [part, changed](const QVariant &v) { part->enablePreSolveEvents = v.toBool(); changed(); },
        -100000.0, 100000.0, {}, -1, 0.0, collision});


    tagEngineKeys(result);
    return result;
}

void PhysicsPropertyPane::attach(QObject *target)
{
    if (m_scene)
        disconnect(m_scene, nullptr, this, nullptr);

    m_scene = qobject_cast<CanvasScene *>(target);

    if (m_scene) {
        connect(m_scene, &CanvasScene::physicsSelectionChanged, this, &PropertyPane::rowsChanged);
        connect(m_scene, &CanvasScene::bodiesChanged, this, &PropertyPane::rowsChanged);
    }

    emit rowsChanged();
}
