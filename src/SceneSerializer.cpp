#include "SceneSerializer.h"

#include "CanvasScene.h"
#include "ShapeItem.h"
#include "PhysicsBody.h"
#include "Joint.h"
#include "ExplosionItem.h"
#include "RayItem.h"
#include "RectangleItem.h"
#include "CircleItem.h"
#include "PolygonItem.h"
#include "EngineRegistry.h"
#include "Rule.h"

#include <QFile>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QSaveFile>

namespace {

constexpr char kFormatTag[] = "shape-editor-scene";

// --- small value helpers ---------------------------------------------------
// Enums are written as names, not numbers: a scene file should stay readable
// and stay valid if an enumerator is ever reordered.

QJsonObject toJson(const QPointF &p)
{
    return QJsonObject { {"x", p.x()}, {"y", p.y()} };
}

QPointF pointFromJson(const QJsonObject &o, const QPointF &fallback = QPointF())
{
    if (o.isEmpty())
        return fallback;
    return QPointF(o.value("x").toDouble(fallback.x()), o.value("y").toDouble(fallback.y()));
}

QJsonObject toJson(const QRectF &r)
{
    return QJsonObject { {"x", r.x()}, {"y", r.y()}, {"width", r.width()}, {"height", r.height()} };
}

QRectF rectFromJson(const QJsonObject &o)
{
    return QRectF(o.value("x").toDouble(), o.value("y").toDouble(),
                  o.value("width").toDouble(), o.value("height").toDouble());
}

QString toJson(const QColor &c)
{
    return c.name(QColor::HexArgb);
}

QColor colorFromJson(const QJsonValue &v, const QColor &fallback)
{
    const QColor c(v.toString());
    return c.isValid() ? c : fallback;
}

QString bodyTypeName(physics::BodyType type)
{
    switch (type) {
    case physics::BodyType::Static:    return QStringLiteral("static");
    case physics::BodyType::Kinematic: return QStringLiteral("kinematic");
    case physics::BodyType::Dynamic:   return QStringLiteral("dynamic");
    }
    return QStringLiteral("dynamic");
}

physics::BodyType bodyTypeFromName(const QString &name)
{
    if (name == QLatin1String("static"))
        return physics::BodyType::Static;
    if (name == QLatin1String("kinematic"))
        return physics::BodyType::Kinematic;
    return physics::BodyType::Dynamic;
}

QString capStyleName(Qt::PenCapStyle style)
{
    switch (style) {
    case Qt::FlatCap:   return QStringLiteral("flat");
    case Qt::SquareCap: return QStringLiteral("square");
    default:            return QStringLiteral("round");
    }
}

Qt::PenCapStyle capStyleFromName(const QString &name)
{
    if (name == QLatin1String("flat"))
        return Qt::FlatCap;
    if (name == QLatin1String("square"))
        return Qt::SquareCap;
    return Qt::RoundCap;
}

QString joinStyleName(Qt::PenJoinStyle style)
{
    switch (style) {
    case Qt::MiterJoin: return QStringLiteral("miter");
    case Qt::BevelJoin: return QStringLiteral("bevel");
    default:            return QStringLiteral("round");
    }
}

Qt::PenJoinStyle joinStyleFromName(const QString &name)
{
    if (name == QLatin1String("miter"))
        return Qt::MiterJoin;
    if (name == QLatin1String("bevel"))
        return Qt::BevelJoin;
    return Qt::RoundJoin;
}

// --- shape ------------------------------------------------------------------

quint64 bitsFromJson(const QJsonValue &v, quint64 fallback)
{
    QString text = v.toString().trimmed();
    if (text.isEmpty())
        return fallback;
    if (text.startsWith(QLatin1String("0x"), Qt::CaseInsensitive))
        text = text.mid(2);
    bool ok = false;
    const quint64 value = text.toULongLong(&ok, 16);
    return ok ? value : fallback;
}

QJsonObject partToJson(const physics::ShapePart &part)
{
    // Whatever the engine said a shape has, under the names it gave them. The
    // serializer neither knows nor checks what any of them mean.
    return QJsonObject::fromVariantMap(part.params);
}

void partFromJson(const QJsonObject &o, physics::ShapePart *part)
{
    part->params = o.toVariantMap();

    // Written before there was more than one engine, the collision bits were
    // hex text rather than numbers. The keys are the same either way.
    for (const char *bits : { "categoryBits", "maskBits" }) {
        const QString key = QLatin1String(bits);
        const QVariant value = part->params.value(key);
        if (value.userType() == QMetaType::QString)
            part->params.insert(key, double(bitsFromJson(o.value(key), 0)));
    }
}

QJsonObject shapeGeometryToJson(const ShapeItem *shape)
{
    QJsonObject o {
        {"name", shape->name()},
        {"rect", toJson(shape->rect())},
        {"origin", toJson(shape->origin())},
        {"pos", toJson(shape->pos())},
        {"rotation", shape->rotation()},
        {"bodyColor", toJson(shape->bodyColor())},
        {"borderColor", toJson(shape->borderColor())},
        {"borderWidth", shape->borderWidth()},
        {"cornerRadius", shape->cornerRadius()},
        {"smoothChain", shape->smoothChain()},
        {"outline", shape->preferOutline()},
        {"filled", shape->filled()},
        {"capStyle", capStyleName(shape->capStyle())},
        {"joinStyle", joinStyleName(shape->joinStyle())},
        {"physics", partToJson(shape->part())},
    };

    if (const auto *polygon = dynamic_cast<const PolygonItem *>(shape)) {
        o.insert("type", QStringLiteral("polygon"));
        o.insert("closed", polygon->isClosed());
        QJsonArray points;
        for (const QPointF &p : polygon->points())
            points.append(toJson(p));
        o.insert("points", points);
    } else if (dynamic_cast<const CircleItem *>(shape)) {
        o.insert("type", QStringLiteral("circle"));
    } else {
        o.insert("type", QStringLiteral("rectangle"));
    }
    return o;
}

ShapeItem *makeShape(const QJsonObject &o)
{
    const QString type = o.value("type").toString();

    if (type == QLatin1String("polygon")) {
        QPolygonF points;
        const QJsonArray array = o.value("points").toArray();
        for (const QJsonValue &v : array)
            points << pointFromJson(v.toObject());
        if (points.size() < 2)
            return nullptr;
        return new PolygonItem(points, o.value("closed").toBool(true));
    }
    if (type == QLatin1String("circle"))
        return new CircleItem;
    if (type == QLatin1String("rectangle"))
        return new RectangleItem;
    return nullptr;
}

void applyShapeProperties(const QJsonObject &o, ShapeItem *shape)
{
    shape->setName(o.value("name").toString(shape->name()));
    if (o.value("type").toString() != QLatin1String("polygon"))
        shape->setRect(rectFromJson(o.value("rect").toObject()));
    shape->setOrigin(pointFromJson(o.value("origin").toObject(), shape->origin()));
    // setOrigin compensates pos() to keep the shape visually put, so the saved
    // position has to be applied after it, not before.
    shape->setPos(pointFromJson(o.value("pos").toObject()));
    shape->setRotation(o.value("rotation").toDouble());
    shape->setBodyColor(colorFromJson(o.value("bodyColor"), shape->bodyColor()));
    shape->setBorderColor(colorFromJson(o.value("borderColor"), shape->borderColor()));
    shape->setBorderWidth(o.value("borderWidth").toDouble(shape->borderWidth()));
    shape->setCornerRadius(o.value("cornerRadius").toDouble(0.0));
    shape->setSmoothChain(o.value("smoothChain").toBool(false));
    shape->setPreferOutline(o.value("outline").toBool(false));
    shape->setFilled(o.value("filled").toBool(shape->filled()));
    shape->setCapStyle(capStyleFromName(o.value("capStyle").toString()));
    shape->setJoinStyle(joinStyleFromName(o.value("joinStyle").toString()));
    partFromJson(o.value("physics").toObject(), &shape->part());
}

} // namespace

namespace SceneSerializer {

QJsonObject shapeToJson(const ShapeItem *shape)
{
    return shapeGeometryToJson(shape);
}

ShapeItem *shapeFromJson(const QJsonObject &object)
{
    ShapeItem *shape = makeShape(object);
    if (shape)
        applyShapeProperties(object, shape);
    return shape;
}

QJsonObject save(const CanvasScene *scene)
{
    QJsonObject document {
        {"format", QLatin1String(kFormatTag)},
        {"version", kFormatVersion},
        // Which engine the scene was built for. Its joints, and half its
        // properties, only mean anything to that one.
        {"engine", scene->simulationEngineName()},
    };

    document.insert("field", QJsonObject {
        {"width", scene->fieldWidth()},
        {"height", scene->fieldHeight()},
        {"backgroundColor", toJson(scene->backgroundColor())},
        {"showGrid", scene->showGrid()},
        {"gridCellSize", scene->gridCellSize()},
        {"gridColor", toJson(scene->gridColor())},
    });

    document.insert("world", QJsonObject {
        {"pixelsPerMeter", scene->world().pixelsPerMeter},
        {"solidBounds", scene->fieldBoundsSolid()},
        // Gravity, the solver's tuning, whether bodies may sleep: the engine
        // named every one of these, and they are written back as they came.
        {"physics", QJsonObject::fromVariantMap(scene->world().params)},
    });

    // Ids are handed out here and only exist so bodies can name their shapes.
    QHash<const ShapeItem *, int> ids;
    QJsonArray shapes;
    int nextId = 1;
    const QList<QGraphicsItem *> all = scene->items(Qt::AscendingOrder);
    for (QGraphicsItem *item : all) {
        auto *shape = qgraphicsitem_cast<ShapeItem *>(item);
        if (!shape || (shape->body() && shape->body()->isRunOnly()))
            continue;
        const int id = nextId++;
        ids.insert(shape, id);
        QJsonObject entry = shapeGeometryToJson(shape);
        entry.insert("id", id);
        shapes.append(entry);
    }
    document.insert("shapes", shapes);

    QJsonArray bodies;
    for (const PhysicsBody *body : scene->bodies()) {
        if (body->isRunOnly())
            continue;
        const physics::BodyDesc &p = body->props();
        QJsonArray members;
        for (const ShapeItem *shape : body->shapes()) {
            if (ids.contains(shape))
                members.append(ids.value(shape));
        }
        QJsonObject entry {
            {"name", body->name()},
            {"type", bodyTypeName(p.type)},
            {"isEnabled", p.isEnabled},
            // Everything else a body has is the engine's, and is kept under
            // the names it published.
            {"physics", QJsonObject::fromVariantMap(p.params)},
            {"shapes", members},
        };
        const ShotSettings &shot = body->shot();
        const ShotSettings untouched;
        if (shot.enabled != untouched.enabled || shot.fullImpulse != untouched.fullImpulse
            || shot.maxPull != untouched.maxPull) {
            entry.insert("shot", QJsonObject {
                {"enabled", shot.enabled},
                {"fullImpulse", shot.fullImpulse},
                {"maxPull", shot.maxPull},
            });
        }
        bodies.append(entry);
    }
    document.insert("bodies", bodies);

    QJsonArray joints;
    for (const Joint *joint : scene->joints()) {
        if (!joint->bodyA())
            continue;

        QJsonObject params;
        for (auto it = joint->params().constBegin(); it != joint->params().constEnd(); ++it)
            params.insert(it.key(), QJsonValue::fromVariant(it.value()));

        QJsonObject entry {
            {"name", joint->name()},
            {"type", joint->typeId()},
            {"bodyA", joint->bodyA()->name()},
            {"anchorA", toJson(joint->anchorScenePos(Joint::End::A))},
            {"anchorB", toJson(joint->anchorScenePos(Joint::End::B))},
            {"axis", toJson(joint->axisScene())},
            {"collideConnected", joint->collideConnected()},
            {"params", params},
        };
        // Absent rather than empty when the joint holds a body to a point in
        // the world: there is no second body, and naming one that is not there
        // would be read back as a joint that had lost an end.
        if (joint->bodyB())
            entry.insert(QStringLiteral("bodyB"), joint->bodyB()->name());
        joints.append(entry);
    }
    document.insert("joints", joints);

    QJsonArray explosions;
    for (ExplosionItem *explosion : scene->explosions()) {
        explosions.append(QJsonObject {
            {"name", explosion->name()},
            {"x", explosion->pos().x()},
            {"y", explosion->pos().y()},
            {"params", QJsonObject::fromVariantMap(explosion->params())},
        });
    }
    if (!explosions.isEmpty())
        document.insert("explosions", explosions);

    QJsonArray rays;
    for (RayItem *ray : scene->rays()) {
        rays.append(QJsonObject {
            {"name", ray->name()},
            {"x", ray->pos().x()},
            {"y", ray->pos().y()},
            {"angle", ray->angleDegrees()},
            {"length", ray->length()},
            {"maskBits", QString::number(ray->maskBits(), 16)},
        });
    }
    if (!rays.isEmpty())
        document.insert("rays", rays);

    QJsonArray rules;
    for (const Rule &rule : scene->rules()) {
        if (!rule.isValid())
            continue; // a half-filled row in the editor is not worth saving
        QJsonObject o;
        if (!rule.name.isEmpty())
            o.insert("name", rule.name);
        o.insert("subject", rule.subjectName);
        if (rule.isEvent()) {
            o.insert("event", rule.eventId);
        } else {
            o.insert("compare", Rule::compareName(rule.compare));
            o.insert("watch", rule.conditionKey);
        }
        if (rule.conditionValue.isValid())
            o.insert("when", QJsonValue::fromVariant(rule.conditionValue));
        o.insert("target", rule.targetName);
        if (rule.isAction()) {
            o.insert("action", rule.actionId);
            o.insert("actionParams", QJsonObject::fromVariantMap(rule.actionParams));
        }
        if (rule.usesSource()) {
            o.insert("sourceObject", rule.sourceObject);
            o.insert("sourceProperty", rule.sourceProperty);
            o.insert("sourceOffset", rule.sourceOffset);
        }
        o.insert("property", rule.propertyKey);
        o.insert("op", Rule::opName(rule.op));
        if (Rule::usesValue(rule.op))
            o.insert("value", QJsonValue::fromVariant(rule.value));
        if (!rule.enabled)
            o.insert("enabled", false);
        if (rule.once)
            o.insert("once", true);
        rules.append(o);
    }
    if (!rules.isEmpty())
        document.insert("rules", rules);

    QJsonArray watches;
    for (const CanvasScene::Watch &watch : scene->watches()) {
        watches.append(QJsonObject {
            {"object", watch.objectName},
            {"property", watch.propertyKey},
            {"label", watch.label},
        });
    }
    if (!watches.isEmpty())
        document.insert("log", watches);

    return document;
}

bool load(CanvasScene *scene, const QJsonObject &document, QString *error)
{
    const auto fail = [error](const QString &message) {
        if (error)
            *error = message;
        return false;
    };

    if (document.value("format").toString() != QLatin1String(kFormatTag))
        return fail(QObject::tr("Not a scene file."));

    const int version = document.value("version").toInt();
    if (version > kFormatVersion) {
        return fail(QObject::tr("This scene was saved by a newer version of the editor"
                                " (format %1, this build understands %2).")
                        .arg(version).arg(kFormatVersion));
    }

    // Refused before anything is touched: a scene half-loaded without the
    // engine its joints were written for is worse than one not loaded at all.
    const QString engineName = document.value("engine").toString();
    if (!engineName.isEmpty()
        && !physics::EngineRegistry::availableEngines().contains(engineName)) {
        return fail(QObject::tr("This scene was built for the %1 engine, which is not"
                                " installed. Its plugin has to sit beside the"
                                " application.").arg(engineName));
    }

    scene->clearContents();
    // Before the joints: how many anchors a joint has is its engine's answer.
    if (!engineName.isEmpty())
        scene->setSimulationEngineName(engineName);

    const QJsonObject field = document.value("field").toObject();
    scene->setFieldSize(field.value("width").toDouble(scene->fieldWidth()),
                        field.value("height").toDouble(scene->fieldHeight()));
    scene->setBackgroundColor(colorFromJson(field.value("backgroundColor"), scene->backgroundColor()));
    scene->setShowGrid(field.value("showGrid").toBool(scene->showGrid()));
    scene->setGridCellSize(field.value("gridCellSize").toDouble(scene->gridCellSize()));
    scene->setGridColor(colorFromJson(field.value("gridColor"), scene->gridColor()));

    const QJsonObject world = document.value("world").toObject();
    scene->setPixelsPerMeter(world.value("pixelsPerMeter").toDouble(scene->pixelsPerMeter()));
    scene->world().params = world.value("physics").toObject().toVariantMap();

    // A scene written before the engine described its own world kept those
    // settings loose in the world object, under the names Box2D uses -- which
    // are the names its catalogue publishes, so they carry straight over.
    if (!world.contains("physics")) {
        QVariantMap &params = scene->world().params;
        const QJsonObject gravity = world.value("gravity").toObject();
        if (!gravity.isEmpty()) {
            params.insert(QStringLiteral("gravityX"), gravity.value("x").toDouble());
            params.insert(QStringLiteral("gravityY"), gravity.value("y").toDouble());
        }
        for (const char *key : { "restitutionThreshold", "hitEventThreshold", "contactHertz",
                                 "contactDampingRatio", "maxContactPushSpeed",
                                 "maximumLinearSpeed", "subStepCount", "enableSleep",
                                 "enableContinuous" }) {
            const QJsonValue value = world.value(QLatin1String(key));
            if (!value.isUndefined())
                params.insert(QLatin1String(key), value.toVariant());
        }
    }
    scene->setFieldBoundsSolid(world.value("solidBounds").toBool(scene->fieldBoundsSolid()));

    QHash<int, ShapeItem *> byId;
    const QJsonArray shapes = document.value("shapes").toArray();
    for (const QJsonValue &v : shapes) {
        const QJsonObject o = v.toObject();
        ShapeItem *shape = makeShape(o);
        if (!shape)
            continue;

        applyShapeProperties(o, shape);
        scene->addItem(shape);
        scene->notifyShapesChanged();
        const int id = o.value("id").toInt(-1);
        if (id >= 0)
            byId.insert(id, shape);
    }

    const QJsonArray bodies = document.value("bodies").toArray();
    for (const QJsonValue &v : bodies) {
        const QJsonObject o = v.toObject();

        QVector<ShapeItem *> members;
        for (const QJsonValue &idValue : o.value("shapes").toArray()) {
            if (ShapeItem *shape = byId.value(idValue.toInt(-1), nullptr))
                members.append(shape);
        }
        if (members.isEmpty())
            continue;

        PhysicsBody *body = scene->createEmptyBody();
        body->setName(scene->uniqueName(o.value("name").toString(body->name()), body));

        physics::BodyDesc &p = body->props();
        const physics::BodyDesc d;
        p.type = bodyTypeFromName(o.value("type").toString());
        p.isEnabled = o.value("isEnabled").toBool(d.isEnabled);
        p.params = o.value("physics").toObject().toVariantMap();

        const QJsonObject shot = o.value("shot").toObject();
        const ShotSettings untouched;
        body->shot().enabled = shot.value("enabled").toBool(untouched.enabled);
        body->shot().fullImpulse = shot.value("fullImpulse").toDouble(untouched.fullImpulse);
        body->shot().maxPull = shot.value("maxPull").toDouble(untouched.maxPull);

        // Older scenes kept the engine's settings loose in the body object,
        // under Box2D's names -- which are the names its catalogue publishes.
        if (!o.contains("physics")) {
            const QJsonObject velocity = o.value("linearVelocity").toObject();
            if (!velocity.isEmpty()) {
                p.params.insert(QStringLiteral("velocityX"), velocity.value("x").toDouble());
                p.params.insert(QStringLiteral("velocityY"), velocity.value("y").toDouble());
            }
            for (const char *key : { "angularVelocity", "linearDamping", "angularDamping",
                                     "gravityScale", "fixedRotation", "isBullet",
                                     "allowFastRotation", "enableSleep", "isAwake",
                                     "sleepThreshold" }) {
                const QJsonValue value = o.value(QLatin1String(key));
                if (!value.isUndefined())
                    p.params.insert(QLatin1String(key), value.toVariant());
            }
        }

        for (ShapeItem *shape : members)
            body->addShape(shape);
    }

    // Joints last: both ends must exist before one can be attached.
    QHash<QString, PhysicsBody *> bodiesByName;
    for (PhysicsBody *body : scene->bodies())
        bodiesByName.insert(body->name(), body);

    for (const QJsonValue &v : document.value("explosions").toArray()) {
        const QJsonObject o = v.toObject();
        ExplosionItem *explosion = scene->addExplosion(QPointF(o.value("x").toDouble(),
                                                      o.value("y").toDouble()));
        explosion->setName(o.value("name").toString(explosion->name()));
        explosion->params() = o.value("params").toObject().toVariantMap();
    }

    for (const QJsonValue &v : document.value("rays").toArray()) {
        const QJsonObject o = v.toObject();
        RayItem *ray = scene->addRay(QPointF(o.value("x").toDouble(),
                                             o.value("y").toDouble()));
        ray->setName(o.value("name").toString(ray->name()));
        ray->setAngleDegrees(o.value("angle").toDouble());
        ray->setLength(o.value("length").toDouble(300.0));
        bool ok = false;
        const quint64 bits = o.value("maskBits").toString().toULongLong(&ok, 16);
        if (ok)
            ray->setMaskBits(bits);
    }

    for (const QJsonValue &v : document.value("joints").toArray()) {
        const QJsonObject o = v.toObject();
        PhysicsBody *bodyA = bodiesByName.value(o.value("bodyA").toString(), nullptr);
        PhysicsBody *bodyB = bodiesByName.value(o.value("bodyB").toString(), nullptr);
        // No "bodyB" at all is a joint to a point in the world; a "bodyB" that
        // names a body no longer here is a joint that has lost an end.
        if (!bodyA || bodyA == bodyB)
            continue;
        if (o.contains(QStringLiteral("bodyB")) && !bodyB)
            continue;

        const QString typeId = o.value("type").toString();
        int anchorCount = 2;
        if (auto engine = physics::EngineRegistry::create(scene->simulationEngineName())) {
            for (const physics::JointType &type : engine->jointTypes()) {
                if (type.id == typeId)
                    anchorCount = type.anchorCount;
            }
        }

        Joint *joint = scene->createJoint(typeId, bodyA, bodyB, anchorCount,
                                          o.value("params").toObject().toVariantMap());
        if (!joint)
            continue;

        joint->setName(scene->uniqueName(o.value("name").toString(joint->name()), joint));
        joint->setAnchorScenePos(Joint::End::A, pointFromJson(o.value("anchorA").toObject()));
        if (anchorCount > 1)
            joint->setAnchorScenePos(Joint::End::B, pointFromJson(o.value("anchorB").toObject()));
        joint->setAxisScene(pointFromJson(o.value("axis").toObject(), QPointF(1.0, 0.0)));
        joint->setCollideConnected(o.value("collideConnected").toBool());
    }

    QVector<Rule> rules;
    for (const QJsonValue &v : document.value("rules").toArray()) {
        const QJsonObject o = v.toObject();
        Rule rule;
        rule.name = o.value("name").toString();
        rule.subjectName = o.value("subject").toString();
        rule.eventId = o.value("event").toString();
        rule.compare = Rule::compareFromName(o.value("compare").toString());
        rule.conditionKey = o.value("watch").toString();
        rule.conditionValue = o.value("when").toVariant();
        rule.targetName = o.value("target").toString();
        rule.actionId = o.value("action").toString();
        rule.actionParams = o.value("actionParams").toObject().toVariantMap();
        rule.sourceObject = o.value("sourceObject").toString();
        rule.sourceProperty = o.value("sourceProperty").toString();
        rule.sourceOffset = o.value("sourceOffset").toDouble();
        rule.propertyKey = o.value("property").toString();
        rule.op = Rule::opFromName(o.value("op").toString());
        rule.value = o.value("value").toVariant();
        rule.enabled = o.value("enabled").toBool(true);
        rule.once = o.value("once").toBool(false);
        if (rule.isValid())
            rules.append(rule);
    }
    scene->setRules(rules);

    QVector<CanvasScene::Watch> watches;
    for (const QJsonValue &v : document.value("log").toArray()) {
        const QJsonObject o = v.toObject();
        CanvasScene::Watch watch;
        watch.objectName = o.value("object").toString();
        watch.propertyKey = o.value("property").toString();
        watch.label = o.value("label").toString();
        if (!watch.objectName.isEmpty() && !watch.propertyKey.isEmpty())
            watches.append(watch);
    }
    scene->setWatches(watches);

    return true;
}

bool saveToFile(const CanvasScene *scene, const QString &path, QString *error)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (error)
            *error = file.errorString();
        return false;
    }

    file.write(QJsonDocument(save(scene)).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        if (error)
            *error = file.errorString();
        return false;
    }
    return true;
}

bool loadFromFile(CanvasScene *scene, const QString &path, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (error)
            *error = file.errorString();
        return false;
    }

    QJsonParseError parseError {};
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        if (error) {
            *error = QObject::tr("%1 (at offset %2)")
                         .arg(parseError.errorString()).arg(parseError.offset);
        }
        return false;
    }
    if (!document.isObject()) {
        if (error)
            *error = QObject::tr("Not a scene file.");
        return false;
    }

    return load(scene, document.object(), error);
}

} // namespace SceneSerializer
