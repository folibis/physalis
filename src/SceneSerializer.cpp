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

QJsonObject partToJson(const physics::ShapePart &part)
{
    return QJsonObject {
        {"density", part.density},
        {"friction", part.material.friction},
        {"restitution", part.material.restitution},
        {"rollingResistance", part.material.rollingResistance},
        {"tangentSpeed", part.material.tangentSpeed},
        {"isSensor", part.isSensor},
        {"categoryBits", QStringLiteral("0x%1").arg(part.filter.categoryBits, 16, 16, QLatin1Char('0'))},
        {"maskBits", QStringLiteral("0x%1").arg(part.filter.maskBits, 16, 16, QLatin1Char('0'))},
        {"groupIndex", part.filter.groupIndex},
        {"enableContactEvents", part.enableContactEvents},
        {"enableSensorEvents", part.enableSensorEvents},
        {"enableHitEvents", part.enableHitEvents},
        {"enablePreSolveEvents", part.enablePreSolveEvents},
    };
}

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

void partFromJson(const QJsonObject &o, physics::ShapePart *part)
{
    const physics::ShapePart d;
    part->density = o.value("density").toDouble(d.density);
    part->material.friction = o.value("friction").toDouble(d.material.friction);
    part->material.restitution = o.value("restitution").toDouble(d.material.restitution);
    part->material.rollingResistance = o.value("rollingResistance").toDouble(d.material.rollingResistance);
    part->material.tangentSpeed = o.value("tangentSpeed").toDouble(d.material.tangentSpeed);
    part->isSensor = o.value("isSensor").toBool(d.isSensor);
    part->filter.categoryBits = bitsFromJson(o.value("categoryBits"), d.filter.categoryBits);
    part->filter.maskBits = bitsFromJson(o.value("maskBits"), d.filter.maskBits);
    part->filter.groupIndex = o.value("groupIndex").toInt(d.filter.groupIndex);
    part->enableContactEvents = o.value("enableContactEvents").toBool(d.enableContactEvents);
    part->enableSensorEvents = o.value("enableSensorEvents").toBool(d.enableSensorEvents);
    part->enableHitEvents = o.value("enableHitEvents").toBool(d.enableHitEvents);
    part->enablePreSolveEvents = o.value("enablePreSolveEvents").toBool(d.enablePreSolveEvents);
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
    };

    document.insert("field", QJsonObject {
        {"width", scene->fieldWidth()},
        {"height", scene->fieldHeight()},
        {"backgroundColor", toJson(scene->backgroundColor())},
        {"showGrid", scene->showGrid()},
        {"gridCellSize", scene->gridCellSize()},
        {"gridColor", toJson(scene->gridColor())},
    });

    const physics::WorldDesc &world = scene->world();
    document.insert("world", QJsonObject {
        {"gravity", toJson(world.gravity)},
        {"pixelsPerMeter", world.pixelsPerMeter},
        {"solidBounds", scene->fieldBoundsSolid()},
        {"restitutionThreshold", world.restitutionThreshold},
        {"hitEventThreshold", world.hitEventThreshold},
        {"contactHertz", world.contactHertz},
        {"contactDampingRatio", world.contactDampingRatio},
        {"maxContactPushSpeed", world.maxContactPushSpeed},
        {"maximumLinearSpeed", world.maximumLinearSpeed},
        {"subStepCount", world.subStepCount},
        {"enableSleep", world.enableSleep},
        {"enableContinuous", world.enableContinuous},
    });

    // Ids are handed out here and only exist so bodies can name their shapes.
    QHash<const ShapeItem *, int> ids;
    QJsonArray shapes;
    int nextId = 1;
    const QList<QGraphicsItem *> all = scene->items(Qt::AscendingOrder);
    for (QGraphicsItem *item : all) {
        auto *shape = qgraphicsitem_cast<ShapeItem *>(item);
        if (!shape)
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
        const physics::BodyDesc &p = body->props();
        QJsonArray members;
        for (const ShapeItem *shape : body->shapes()) {
            if (ids.contains(shape))
                members.append(ids.value(shape));
        }
        bodies.append(QJsonObject {
            {"name", body->name()},
            {"type", bodyTypeName(p.type)},
            {"isEnabled", p.isEnabled},
            {"linearVelocity", toJson(p.linearVelocity)},
            {"angularVelocity", p.angularVelocityDegrees},
            {"linearDamping", p.linearDamping},
            {"angularDamping", p.angularDamping},
            {"gravityScale", p.gravityScale},
            {"fixedRotation", p.fixedRotation},
            {"isBullet", p.isBullet},
            {"allowFastRotation", p.allowFastRotation},
            {"enableSleep", p.enableSleep},
            {"isAwake", p.isAwake},
            {"sleepThreshold", p.sleepThreshold},
            {"shapes", members},
        });
    }
    document.insert("bodies", bodies);

    QJsonArray joints;
    for (const Joint *joint : scene->joints()) {
        if (!joint->bodyA() || !joint->bodyB())
            continue;

        QJsonObject params;
        for (auto it = joint->params().constBegin(); it != joint->params().constEnd(); ++it)
            params.insert(it.key(), QJsonValue::fromVariant(it.value()));

        joints.append(QJsonObject {
            {"name", joint->name()},
            {"type", joint->typeId()},
            {"bodyA", joint->bodyA()->name()},
            {"bodyB", joint->bodyB()->name()},
            {"anchorA", toJson(joint->anchorScenePos(Joint::End::A))},
            {"anchorB", toJson(joint->anchorScenePos(Joint::End::B))},
            {"axis", toJson(joint->axisScene())},
            {"collideConnected", joint->collideConnected()},
            {"params", params},
        });
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

    scene->clearContents();

    const QJsonObject field = document.value("field").toObject();
    scene->setFieldSize(field.value("width").toDouble(scene->fieldWidth()),
                        field.value("height").toDouble(scene->fieldHeight()));
    scene->setBackgroundColor(colorFromJson(field.value("backgroundColor"), scene->backgroundColor()));
    scene->setShowGrid(field.value("showGrid").toBool(scene->showGrid()));
    scene->setGridCellSize(field.value("gridCellSize").toDouble(scene->gridCellSize()));
    scene->setGridColor(colorFromJson(field.value("gridColor"), scene->gridColor()));

    const QJsonObject world = document.value("world").toObject();
    scene->setGravity(pointFromJson(world.value("gravity").toObject(), scene->gravity()));
    scene->setPixelsPerMeter(world.value("pixelsPerMeter").toDouble(scene->pixelsPerMeter()));

    // Absent keys keep the default, so a scene written before these existed
    // still loads with Box2D's own tuning.
    physics::WorldDesc &tuning = scene->world();
    const physics::WorldDesc factory;
    tuning.restitutionThreshold =
        world.value("restitutionThreshold").toDouble(factory.restitutionThreshold);
    tuning.hitEventThreshold =
        world.value("hitEventThreshold").toDouble(factory.hitEventThreshold);
    tuning.contactHertz = world.value("contactHertz").toDouble(factory.contactHertz);
    tuning.contactDampingRatio =
        world.value("contactDampingRatio").toDouble(factory.contactDampingRatio);
    tuning.maxContactPushSpeed =
        world.value("maxContactPushSpeed").toDouble(factory.maxContactPushSpeed);
    tuning.maximumLinearSpeed =
        world.value("maximumLinearSpeed").toDouble(factory.maximumLinearSpeed);
    tuning.subStepCount = world.value("subStepCount").toInt(factory.subStepCount);
    tuning.enableSleep = world.value("enableSleep").toBool(factory.enableSleep);
    tuning.enableContinuous = world.value("enableContinuous").toBool(factory.enableContinuous);
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
        p.linearVelocity = pointFromJson(o.value("linearVelocity").toObject(), d.linearVelocity);
        p.angularVelocityDegrees = o.value("angularVelocity").toDouble(d.angularVelocityDegrees);
        p.linearDamping = o.value("linearDamping").toDouble(d.linearDamping);
        p.angularDamping = o.value("angularDamping").toDouble(d.angularDamping);
        p.gravityScale = o.value("gravityScale").toDouble(d.gravityScale);
        p.fixedRotation = o.value("fixedRotation").toBool(d.fixedRotation);
        p.isBullet = o.value("isBullet").toBool(d.isBullet);
        p.allowFastRotation = o.value("allowFastRotation").toBool(d.allowFastRotation);
        p.enableSleep = o.value("enableSleep").toBool(d.enableSleep);
        p.isAwake = o.value("isAwake").toBool(d.isAwake);
        p.sleepThreshold = o.value("sleepThreshold").toDouble(d.sleepThreshold);

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
        if (!bodyA || !bodyB || bodyA == bodyB)
            continue; // a joint missing an end holds nothing

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
