#include "SceneExporter.h"

#include "CanvasScene.h"
#include "EngineRegistry.h"
#include "Joint.h"
#include "PhysicsBody.h"
#include "SceneSerializer.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJSEngine>
#include <QJSValue>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QObject>
#include <QTextStream>

namespace SceneExporter {

namespace {

// The two files that make a folder a converter.
const char *kManifestFile = "manifest.json";
const char *kScriptFile = "export.js";

// Resolves `relative` against `base` and refuses anything that lands outside
// it. cleanPath collapses "..", so this catches the ways out without needing
// the file to exist yet -- which matters, because writes name files that do
// not.
bool resolveInside(const QString &base, const QString &relative, QString *resolved)
{
    const QString root = QDir::cleanPath(QDir(base).absolutePath());
    const QString full = QDir::cleanPath(QDir(base).absoluteFilePath(relative));
    if (full != root && !full.startsWith(root + QLatin1Char('/')))
        return false;
    *resolved = full;
    return true;
}

// What the script is given to reach the world with. Reading is scoped to the
// converter's own folder, writing to the folder the user chose; there is no
// third door, and QJSEngine opens none of its own.
class ExportIo : public QObject
{
    Q_OBJECT

public:
    ExportIo(QJSEngine *engine, QString from, QString to)
        : m_engine(engine)
        , m_from(std::move(from))
        , m_to(std::move(to))
    {
    }

    // A template, or anything else the converter shipped beside itself.
    Q_INVOKABLE QString read(const QString &relative)
    {
        QString path;
        if (!resolveInside(m_from, relative, &path)) {
            m_engine->throwError(QStringLiteral("read('%1') is outside the converter's folder")
                                     .arg(relative));
            return {};
        }
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            m_engine->throwError(QStringLiteral("read('%1'): %2")
                                     .arg(relative, file.errorString()));
            return {};
        }
        QTextStream in(&file);
        in.setEncoding(QStringConverter::Utf8);
        return in.readAll();
    }

    // One of the files being exported. Subdirectories are made as needed, so a
    // converter can lay out a whole project rather than a single file.
    Q_INVOKABLE bool write(const QString &relative, const QString &contents)
    {
        QString path;
        if (!resolveInside(m_to, relative, &path)) {
            m_engine->throwError(QStringLiteral("write('%1') is outside the output folder")
                                     .arg(relative));
            return false;
        }
        const QFileInfo info(path);
        if (!QDir().mkpath(info.absolutePath())) {
            m_engine->throwError(QStringLiteral("write('%1'): cannot make %2")
                                     .arg(relative, info.absolutePath()));
            return false;
        }
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
            m_engine->throwError(QStringLiteral("write('%1'): %2")
                                     .arg(relative, file.errorString()));
            return false;
        }
        QTextStream out(&file);
        out.setEncoding(QStringConverter::Utf8);
        out << contents;
        m_written << relative;
        return true;
    }

    Q_INVOKABLE void log(const QString &message) { m_messages << message; }

    QStringList written() const { return m_written; }
    QStringList messages() const { return m_messages; }

private:
    QJSEngine *m_engine = nullptr;
    QString m_from;
    QString m_to;
    QStringList m_written;
    QStringList m_messages;
};

// Everything the loaded engine will say about itself, as data. A converter
// needs it to know what a key means -- which units `motorSpeed` is in, what
// `upperTranslation` defaults to, which joint types exist at all -- and the
// application has no business interpreting any of it either.
QJsonObject engineCatalogue(const QString &engineName)
{
    auto engine = physics::EngineRegistry::create(engineName);
    if (!engine)
        return {};

    const auto properties = [](const physics::PropertyList &list) {
        QJsonArray out;
        for (const physics::JointParam &p : list) {
            out.append(QJsonObject {
                {"key", p.key},
                {"label", p.label},
                {"section", p.section},
                {"type", int(p.type)},
                {"default", QJsonValue::fromVariant(p.defaultValue)},
                {"min", p.minValue},
                {"max", p.maxValue},
                {"decimals", p.decimals},
                {"choices", QJsonArray::fromStringList(p.choices)},
                {"readable", p.liveReadable},
                {"settable", p.liveSettable},
                {"tooltip", p.tooltip},
            });
        }
        return out;
    };
    const auto events = [](const QVector<physics::EventType> &list) {
        QJsonArray out;
        for (const physics::EventType &e : list)
            out.append(QJsonObject {{"id", e.id}, {"label", e.label},
                                    {"description", e.description}});
        return out;
    };
    const auto actions = [&properties](const QVector<physics::ActionType> &list) {
        QJsonArray out;
        for (const physics::ActionType &a : list)
            out.append(QJsonObject {{"id", a.id}, {"label", a.label},
                                    {"description", a.description},
                                    {"params", properties(a.params)}});
        return out;
    };

    QJsonArray joints;
    for (const physics::JointType &t : engine->jointTypes()) {
        joints.append(QJsonObject {
            {"id", t.id},
            {"label", t.label},
            {"description", t.description},
            {"anchorCount", t.anchorCount},
            {"needsAxis", t.needsAxis},
            {"params", properties(t.params)},
            {"events", events(t.events)},
            {"readables", properties(engine->jointReadables(t.id))},
        });
    }

    return QJsonObject {
        {"name", engine->name()},
        {"jointTypes", joints},
        {"bodyProperties", properties(engine->bodyProperties())},
        {"shapeProperties", properties(engine->shapeProperties())},
        {"worldProperties", properties(engine->worldProperties())},
        {"bodyEvents", events(engine->bodyEvents())},
        {"shapeEvents", events(engine->shapeEvents())},
        {"bodyActions", actions(engine->bodyActions())},
        {"jointActions", actions(engine->jointActions())},
    };
}

QJsonObject pointToJson(const QPointF &point)
{
    return QJsonObject {{"x", point.x()}, {"y", point.y()}};
}

QJsonObject partToJson(const physics::ShapePart &part)
{
    const physics::Geometry &g = part.geometry;
    QJsonArray points;
    for (const QPointF &p : g.points)
        points.append(pointToJson(p));

    static const char *kinds[] = {"box", "circle", "polygon", "chain"};
    return QJsonObject {
        {"name", part.name},
        {"kind", kinds[int(g.kind)]},
        {"center", pointToJson(g.center)},
        {"rotation", g.rotationDegrees},
        {"halfExtents", pointToJson(g.halfExtents)},
        {"radius", g.radius},
        {"points", points},
        {"closed", g.closed},
        {"cornerRadius", g.cornerRadius},
        {"smoothChain", g.smoothChain},
        {"density", part.density},
        {"friction", part.material.friction},
        {"restitution", part.material.restitution},
        {"rollingResistance", part.material.rollingResistance},
        {"tangentSpeed", part.material.tangentSpeed},
        {"categoryBits", QString::number(part.filter.categoryBits)},
        {"maskBits", QString::number(part.filter.maskBits)},
        {"groupIndex", part.filter.groupIndex},
        {"isSensor", part.isSensor},
        {"enableSensorEvents", part.enableSensorEvents},
        {"enableContactEvents", part.enableContactEvents},
        {"enableHitEvents", part.enableHitEvents},
        {"enablePreSolveEvents", part.enablePreSolveEvents},
    };
}

// The scene as the engine is handed it, alongside the scene as it is saved.
//
// A .phys file keeps shapes in scene coordinates and works out each body's
// transform when a run starts; a converter would otherwise have to reproduce
// that derivation, which is the application's business and nobody else's. So
// this is the same BodyDesc and JointDesc a plugin receives -- body-local
// geometry, resolved anchors, everything already worked out.
QJsonObject simulationView(const CanvasScene *scene)
{
    QJsonArray bodies;
    QHash<const PhysicsBody *, int> indices;
    for (PhysicsBody *body : scene->bodies()) {
        if (body->isEmpty())
            continue;
        indices.insert(body, bodies.size());

        const physics::BodyDesc desc = body->toBodyDesc();
        QJsonArray parts;
        for (const physics::ShapePart &part : desc.parts)
            parts.append(partToJson(part));

        static const char *types[] = {"static", "kinematic", "dynamic"};
        bodies.append(QJsonObject {
            {"name", desc.name},
            {"type", types[int(desc.type)]},
            {"position", pointToJson(desc.position)},
            {"rotation", desc.rotationDegrees},
            {"linearVelocity", pointToJson(desc.linearVelocity)},
            {"angularVelocity", desc.angularVelocityDegrees},
            {"linearDamping", desc.linearDamping},
            {"angularDamping", desc.angularDamping},
            {"gravityScale", desc.gravityScale},
            {"enableSleep", desc.enableSleep},
            {"isAwake", desc.isAwake},
            {"sleepThreshold", desc.sleepThreshold},
            {"fixedRotation", desc.fixedRotation},
            {"isBullet", desc.isBullet},
            {"allowFastRotation", desc.allowFastRotation},
            {"isEnabled", desc.isEnabled},
            {"parts", parts},
        });
    }

    QJsonArray joints;
    for (Joint *joint : scene->joints()) {
        const auto a = indices.constFind(joint->bodyA());
        const auto b = indices.constFind(joint->bodyB());
        if (a == indices.constEnd() || b == indices.constEnd())
            continue;

        const physics::JointDesc desc = joint->toJointDesc(*a, *b);
        QJsonArray anchors;
        for (const QPointF &anchor : desc.anchors)
            anchors.append(pointToJson(anchor));

        joints.append(QJsonObject {
            {"name", desc.name},
            {"type", desc.typeId},
            {"bodyA", desc.bodyA},
            {"bodyB", desc.bodyB},
            {"anchors", anchors},
            {"axis", pointToJson(desc.axis)},
            {"collideConnected", desc.collideConnected},
            {"params", QJsonObject::fromVariantMap(desc.params)},
        });
    }

    return QJsonObject {{"bodies", bodies}, {"joints", joints}};
}

} // namespace

QVector<Converter> discover(const QString &root)
{
    QVector<Converter> found;
    if (root.trimmed().isEmpty())
        return found;

    const QDir dir(root);
    if (!dir.exists())
        return found;

    const QStringList folders =
        dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QString &name : folders) {
        const QDir candidate(dir.absoluteFilePath(name));
        if (!candidate.exists(QLatin1String(kScriptFile)))
            continue;

        QFile manifest(candidate.absoluteFilePath(QLatin1String(kManifestFile)));
        if (!manifest.open(QIODevice::ReadOnly))
            continue;

        // Read, never run. Building the menu must not execute a converter --
        // opening the File menu is not consent to run anybody's code.
        const QJsonObject described =
            QJsonDocument::fromJson(manifest.readAll()).object();
        if (described.isEmpty())
            continue;

        Converter converter;
        converter.id = name;
        converter.folder = candidate.absolutePath();
        converter.name = described.value(QStringLiteral("name")).toString(name);
        converter.description = described.value(QStringLiteral("description")).toString();

        // Whatever it wants asked. Declared in the manifest rather than in the
        // script, because the options page has to be built without running
        // anybody's code -- the same reason the name is there.
        for (const QJsonValue &value :
             described.value(QStringLiteral("settings")).toArray()) {
            const QJsonObject o = value.toObject();
            const QString key = o.value(QStringLiteral("key")).toString();
            if (key.isEmpty())
                continue;

            ConverterSetting setting;
            setting.key = key;
            setting.label = o.value(QStringLiteral("label")).toString(key);
            setting.tooltip = o.value(QStringLiteral("tooltip")).toString();
            setting.type = o.value(QStringLiteral("type")).toString(QStringLiteral("string"));
            setting.defaultValue = o.value(QStringLiteral("default")).toVariant();
            setting.minValue = o.value(QStringLiteral("min")).toDouble(0.0);
            setting.maxValue = o.value(QStringLiteral("max")).toDouble(0.0);
            setting.decimals = o.value(QStringLiteral("decimals")).toInt(2);
            for (const QJsonValue &choice : o.value(QStringLiteral("choices")).toArray())
                setting.choices << choice.toString();
            converter.settings.append(setting);
        }
        found.append(converter);
    }
    return found;
}

bool run(const Converter &converter, const CanvasScene *scene,
         const QString &outputFolder, const QJsonObject &settings,
         QString *error, QStringList *written, QStringList *log)
{
    const auto fail = [error](const QString &message) {
        if (error)
            *error = message;
        return false;
    };

    if (!scene)
        return fail(QObject::tr("There is no scene to export."));
    if (!QDir().mkpath(outputFolder))
        return fail(QObject::tr("Cannot write to %1.").arg(outputFolder));

    QFile script(QDir(converter.folder).absoluteFilePath(QLatin1String(kScriptFile)));
    if (!script.open(QIODevice::ReadOnly | QIODevice::Text))
        return fail(QObject::tr("Cannot read %1: %2").arg(kScriptFile, script.errorString()));
    const QString source = QString::fromUtf8(script.readAll());

    QJSEngine js;
    // console.log, so a converter can be developed the way any script is.
    js.installExtensions(QJSEngine::ConsoleExtension);

    ExportIo io(&js, converter.folder, outputFolder);
    const QJSValue ioValue = js.newQObject(&io);
    js.setObjectOwnership(&io, QJSEngine::CppOwnership);

    const QJSValue result = js.evaluate(source, script.fileName());
    if (result.isError()) {
        return fail(QObject::tr("%1 line %2: %3")
                        .arg(kScriptFile)
                        .arg(result.property(QStringLiteral("lineNumber")).toInt())
                        .arg(result.toString()));
    }

    const QJSValue entry = js.globalObject().property(QStringLiteral("exportScene"));
    if (!entry.isCallable())
        return fail(QObject::tr("%1 defines no exportScene(scene, io) function.").arg(kScriptFile));

    // The whole scene, exactly as it would be saved, plus what the engine says
    // about itself. One argument, and everything is in it.
    QJsonObject document = SceneSerializer::save(scene);
    document.insert(QStringLiteral("engine"),
                    engineCatalogue(scene->simulationEngineName()));
    document.insert(QStringLiteral("simulation"), simulationView(scene));
    document.insert(QStringLiteral("settings"), settings);

    // The converter's own settings, resolved: whatever the user chose over
    // whatever the manifest asked for. It could dig these out of `settings`
    // itself, but every converter would then have to know where the
    // application files them, which is not its business.
    QJsonObject own;
    const QJsonObject stored = settings.value(QStringLiteral("Export")).toObject()
                                   .value(converter.id).toObject();
    for (const ConverterSetting &setting : converter.settings) {
        own.insert(setting.key,
                   stored.contains(setting.key)
                       ? stored.value(setting.key)
                       : QJsonValue::fromVariant(setting.defaultValue));
    }
    document.insert(QStringLiteral("converterSettings"), own);

    const QJSValue answer =
        entry.call({ js.toScriptValue(document.toVariantMap()), ioValue });
    if (answer.isError()) {
        return fail(QObject::tr("%1 line %2: %3")
                        .arg(kScriptFile)
                        .arg(answer.property(QStringLiteral("lineNumber")).toInt())
                        .arg(answer.toString()));
    }

    // Whatever it had to say, even if it then failed: a converter that logs
    // its way to an error has said the most useful part already.
    if (log)
        *log = io.messages();

    if (io.written().isEmpty())
        return fail(QObject::tr("%1 wrote no files.").arg(converter.name));

    if (written)
        *written = io.written();
    return true;
}

} // namespace SceneExporter

#include "SceneExporter.moc"
