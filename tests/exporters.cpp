// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ruslan Muhlinin. See LICENSE.
#include "CanvasScene.h"
#include "CircleItem.h"
#include "EngineRegistry.h"
#include "IPhysicsEngine.h"
#include "Joint.h"
#include "PhysicsBody.h"
#include "RectangleItem.h"
#include "Rule.h"
#include "SceneExporter.h"
#include "ShapeItem.h"

#include <QDir>
#include <QFile>
#include <QJsonObject>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <cmath>
#include <gtest/gtest.h>

// The three converters. The application knows no formats, so the only things
// that can be checked from here are the ones that are true of any of them: a
// converter runs, writes files, says nothing went wrong -- and the numbers it
// writes mean what they mean in the app. The last is not a formality: when a
// scale changed in the engines, all three went on writing the old one, and an
// exported scene ran fifty times faster than the scene it came from.

namespace {

QString convertersFolder()
{
    QDir dir(QStringLiteral(__FILE__));
    dir.cdUp();
    dir.cdUp();
    return dir.absoluteFilePath(QStringLiteral("exporters"));
}

QVector<SceneExporter::Converter> shipped()
{
    return SceneExporter::discover(convertersFolder());
}

// Everything a scene can hold, so a converter is handed the whole surface
// rather than a box on a plane.
void buildEverything(CanvasScene *scene)
{
    scene->setSimulationEngineName(QStringLiteral("Box2D"));
    scene->setPixelsPerMeter(1000.0);
    scene->world().params["gravityY"] = 9.81;
    scene->setEditorMode(EditorMode::Physics);

    auto *ground = new RectangleItem;
    ground->setRect(QRectF(0, 0, 800, 40));
    ground->setPos(-400, 300);
    ground->setName(QStringLiteral("ground"));
    scene->addItem(ground);

    auto *crate = new RectangleItem;
    crate->setRect(QRectF(0, 0, 40, 40));
    crate->setPos(-20, 0);
    crate->setName(QStringLiteral("crate"));
    scene->addItem(crate);

    auto *wheel = new CircleItem;
    wheel->setRect(QRectF(0, 0, 60, 60));
    wheel->setPos(120, 0);
    wheel->setName(QStringLiteral("wheel"));
    scene->addItem(wheel);
    scene->notifyShapesChanged();

    const auto bodyOf = [scene](ShapeItem *shape, physics::BodyType type, const QString &name) {
        scene->selectForPhysics(shape, true);
        PhysicsBody *body = scene->createBodyFromSelection();
        body->props().type = type;
        body->setName(name);
        scene->clearPhysicsSelection();
        return body;
    };
    bodyOf(ground, physics::BodyType::Static, QStringLiteral("groundBody"));
    PhysicsBody *crateBody = bodyOf(crate, physics::BodyType::Dynamic, QStringLiteral("crateBody"));
    PhysicsBody *wheelBody = bodyOf(wheel, physics::BodyType::Dynamic, QStringLiteral("wheelBody"));

    // A body that starts moving: the number this test is really about.
    crateBody->props().params["velocityX"] = 100.0;

    scene->createJoint(QStringLiteral("revolute"), crateBody, wheelBody, 1, {});

    Rule rule;
    rule.conditions[0].subjectName = Rule::world();
    rule.conditions[0].conditionKey = QStringLiteral("frame");
    rule.conditions[0].compare = Rule::Compare::Multiple;
    rule.conditions[0].conditionValue = 30;
    rule.actions[0].targetName = crateBody->name();
    rule.actions[0].actionId = QStringLiteral("pushAt");
    rule.actions[0].actionParams.insert(QStringLiteral("impulseX"), 5.0);

    // A rule on being hit, so that the world's hit threshold is a setting this
    // scene can be affected by rather than one no converter has any use for.
    Rule hit;
    hit.conditions[0].subjectName = crate->name();
    hit.conditions[0].eventId = QStringLiteral("contactHit");
    hit.actions[0].targetName = crateBody->name();
    hit.actions[0].actionId = Rule::initStateAction();

    // And one on touching, so contact tuning matters too.
    Rule touch;
    touch.conditions[0].subjectName = wheel->name();
    touch.conditions[0].eventId = QStringLiteral("contactBegin");
    touch.actions[0].targetName = wheelBody->name();
    touch.actions[0].actionId = Rule::initStateAction();

    // And one watching a reading change rather than compare, since that is the
    // only condition the generated code has to keep state of its own for.
    Rule changed;
    changed.conditions[0].subjectName = crateBody->name();
    changed.conditions[0].conditionKey = QStringLiteral("isAwake");
    changed.conditions[0].compare = Rule::Compare::ChangedTo;
    changed.conditions[0].conditionValue = false;
    changed.actions[0].targetName = crateBody->name();
    changed.actions[0].propertyKey = QStringLiteral("gravityScale");
    changed.actions[0].op = Rule::Op::Subtract;
    changed.actions[0].value = 0.125;

    scene->setRules({ rule, hit, touch, changed });
}

// A world setting a format has no answer for. Each is named with the reason,
// so a converter that quietly drops a setting it could have written fails
// rather than hiding among them. Where a converter says so itself through
// io.log(), it does not need to be in here at all.
const QSet<QString> kCannotExpress {
    // Planck is Box2D 2.4 and qml-box2d is 2.3: contacts are rigid in both, so
    // there is no stiffness or damping to set.
    QStringLiteral("planck-js/contactHertz"),
    QStringLiteral("planck-js/contactDampingRatio"),
    QStringLiteral("qml-box2d/contactHertz"),
    QStringLiteral("qml-box2d/contactDampingRatio"),
    // Neither has a world speed ceiling.
    QStringLiteral("planck-js/maximumLinearSpeed"),
    QStringLiteral("qml-box2d/maximumLinearSpeed"),
    // Nor a hit event, so there is nothing to set a threshold on.
    QStringLiteral("planck-js/hitEventThreshold"),
    // Neither has a push speed to cap either; both say so in their own words.
    QStringLiteral("planck-js/maxContactPushSpeed"),
    QStringLiteral("qml-box2d/maxContactPushSpeed"),
    // Planck fixes its restitution threshold in Settings; the converter says so.
    QStringLiteral("planck-js/restitutionThreshold"),
    // Nor a per-body sleep threshold: 2.3 and 2.4 decide it themselves.
    QStringLiteral("planck-js/sleepThreshold"),
    QStringLiteral("qml-box2d/sleepThreshold"),
    // Both inset their polygons to collide flush with what is drawn, and make
    // the mass up by adjusting the density -- so the number written is
    // deliberately not the one the app uses, and the mass is what matches.
    QStringLiteral("planck-js/density"),
    QStringLiteral("qml-box2d/density"),
    // Collision filters are written as literals, and in sixteen bits where the
    // format has only sixteen: never as the decimal the scene keeps.
    QStringLiteral("box2d-qt-project/categoryBits"),
    QStringLiteral("box2d-qt-project/maskBits"),
    QStringLiteral("planck-js/categoryBits"),
    QStringLiteral("planck-js/maskBits"),
    QStringLiteral("qml-box2d/categoryBits"),
    QStringLiteral("qml-box2d/maskBits"),
    // The contact margin is not written as a number: it becomes Box2D's length
    // unit, which TheSceneScaleReachesEveryExport checks on its own.
    QStringLiteral("box2d-qt-project/contactMargin"),
    QStringLiteral("planck-js/contactMargin"),
    QStringLiteral("qml-box2d/contactMargin"),
};

struct Output {
    bool ok = false;
    QString error;
    QStringList written;
    QStringList log;
    QString text;      // every file it wrote, run together
};

Output exportWith(const SceneExporter::Converter &converter, const CanvasScene *scene)
{
    Output out;
    QTemporaryDir folder;
    if (!folder.isValid()) {
        out.error = QStringLiteral("no temporary folder");
        return out;
    }
    out.ok = SceneExporter::run(converter, scene, folder.path(), QJsonObject(),
                                &out.error, &out.written, &out.log);
    for (const QString &name : std::as_const(out.written)) {
        QFile file(QDir(folder.path()).filePath(name));
        if (file.open(QIODevice::ReadOnly | QIODevice::Text))
            out.text += QString::fromUtf8(file.readAll());
    }
    return out;
}

} // namespace

// All three are found, and each one runs on a full scene and writes files. A
// converter that writes nothing has converted nothing, whatever it reports.
TEST(Exporters, EveryConverterRunsAndWritesSomething)
{
    const QVector<SceneExporter::Converter> converters = shipped();
    ASSERT_GE(converters.size(), 3) << "the three shipped converters were not found under "
                                    << convertersFolder().toStdString();

    CanvasScene scene;
    buildEverything(&scene);

    for (const SceneExporter::Converter &converter : converters) {
        const Output out = exportWith(converter, &scene);
        EXPECT_TRUE(out.ok) << converter.id.toStdString() << " failed: " << out.error.toStdString();
        EXPECT_FALSE(out.written.isEmpty())
            << converter.id.toStdString() << " reported success and wrote no files";
        EXPECT_FALSE(out.text.isEmpty())
            << converter.id.toStdString() << " wrote files with nothing in them";
    }
}

// Every named thing in the scene reaches every export. A body or a shape
// quietly left out is a scene that runs differently somewhere else.
TEST(Exporters, EveryBodyAndShapeIsInTheOutput)
{
    CanvasScene scene;
    buildEverything(&scene);

    for (const SceneExporter::Converter &converter : shipped()) {
        const Output out = exportWith(converter, &scene);
        ASSERT_TRUE(out.ok) << converter.id.toStdString() << ": " << out.error.toStdString();
        for (ShapeItem *shape : scene.shapes()) {
            EXPECT_TRUE(out.text.contains(shape->name()))
                << converter.id.toStdString() << " left " << shape->name().toStdString()
                << " out of what it wrote";
        }
        for (PhysicsBody *body : scene.bodies()) {
            EXPECT_TRUE(out.text.contains(body->name()))
                << converter.id.toStdString() << " left " << body->name().toStdString()
                << " out of what it wrote";
        }
    }
}

// A speed means the same in an export as it does in the app. The app divides a
// velocity by the scene's scale to reach metres a second; an export that uses
// any other factor starts the same scene at a different speed, and the two
// drift apart from the first step.
TEST(Exporters, AVelocityMeansTheSameInTheExportAsInTheApp)
{
    CanvasScene scene;
    buildEverything(&scene);

    // 100 scene units a second at 1000 units per metre: a tenth of a metre a
    // second, whoever is doing the arithmetic. (What it used to write was the
    // pace scale, 50/1000, giving 5 -- a hundred times too fast.)
    const double expected = 100.0 / 1000.0;

    for (const SceneExporter::Converter &converter : shipped()) {
        const Output out = exportWith(converter, &scene);
        ASSERT_TRUE(out.ok) << converter.id.toStdString() << ": " << out.error.toStdString();

        // Every line that mentions a linear velocity, whatever the format
        // spells it: a rule that sets one while the scene runs writes the same
        // words, so the first match is not necessarily the body's own. Looking
        // for the bare number anywhere in the file instead finds every other
        // 5 and 0.1 in it and proves nothing.
        QStringList velocityLines;
        for (const QString &line : out.text.split(QLatin1Char('\n'))) {
            if (line.contains(QStringLiteral("inearVelocity")))
                velocityLines << line.trimmed();
        }
        ASSERT_FALSE(velocityLines.isEmpty())
            << converter.id.toStdString() << " writes no starting velocity at all";
        const QString velocityLine = velocityLines.join(QStringLiteral(" | "));

        // Every number on those lines, and one of them has to be the velocity.
        bool found = false;
        static const QRegularExpression number(QStringLiteral("-?\\d+(?:\\.\\d+)?(?:e-?\\d+)?"));
        auto it = number.globalMatch(velocityLine);
        while (it.hasNext()) {
            const double value = it.next().captured().toDouble();
            if (qAbs(value - expected) < 1e-9)
                found = true;
        }
        EXPECT_TRUE(found)
            << converter.id.toStdString() << " starts the body at the wrong speed: the app uses "
            << expected << " and it wrote \"" << velocityLine.toStdString() << "\"";
    }
}

// The scene's own scale reaches every export: Box2D's tolerances are lengths,
// and an export set up at a different scale jams where the app does not.
TEST(Exporters, TheSceneScaleReachesEveryExport)
{
    CanvasScene scene;
    buildEverything(&scene);

    for (const SceneExporter::Converter &converter : shipped()) {
        const Output out = exportWith(converter, &scene);
        ASSERT_TRUE(out.ok) << converter.id.toStdString() << ": " << out.error.toStdString();
        // 2 px of contact margin at 1000 px per metre is a length unit of 0.1,
        // and a slop of 0.0005 m. Whichever way a format spells it, the line
        // that does it says so -- a bare "0.1" anywhere in the file is the
        // body's density or somebody's alpha.
        bool setsTheScale = false;
        for (const QString &line : out.text.split(QLatin1Char('\n'))) {
            const bool aboutScale = line.contains(QStringLiteral("engthUnit"), Qt::CaseInsensitive)
                                    || line.contains(QStringLiteral("linearSlop"), Qt::CaseInsensitive);
            if (aboutScale && (line.contains(QStringLiteral("0.1"))
                               || line.contains(QStringLiteral("0.0005")))) {
                setsTheScale = true;
                break;
            }
        }
        EXPECT_TRUE(setsTheScale)
            << converter.id.toStdString() << " writes nothing that sets Box2D's tolerances to"
            << " the scene's scale, so it will make contacts at a different distance to the app";
    }
}

namespace {

// How the app scales a property on its way to the solver. Everything the
// engines do falls into three: a number that means the same to Box2D as it
// does here, a length (and now a speed) divided by the scene's scale, and the
// handful of quantities quoted at the reference scale of fifty units to the
// metre -- gravity and the thresholds that answer to it. A converter has to
// use the one the app uses, so the test says which that is rather than
// accepting whichever of them makes the number turn up.
enum class Scaled { Raw, ByScale, AtTheReferencePace };

Scaled scalingOf(const QString &key)
{
    static const QSet<QString> pace {
        QStringLiteral("gravityX"), QStringLiteral("gravityY"),
        QStringLiteral("maximumLinearSpeed"), QStringLiteral("maxContactPushSpeed"),
        QStringLiteral("restitutionThreshold"), QStringLiteral("hitEventThreshold"),
        QStringLiteral("sleepThreshold"),
    };
    static const QSet<QString> byScale {
        QStringLiteral("velocityX"), QStringLiteral("velocityY"),
        QStringLiteral("tangentSpeed"),
    };
    if (pace.contains(key))
        return Scaled::AtTheReferencePace;
    if (byScale.contains(key))
        return Scaled::ByScale;
    return Scaled::Raw;
}

// The number the app itself hands the solver for this property.
double asTheAppUsesIt(const QString &key, double value, double pixelsPerMeter)
{
    switch (scalingOf(key)) {
    case Scaled::ByScale:
        return value / pixelsPerMeter;
    case Scaled::AtTheReferencePace:
        return value * (physics::kReferencePixelsPerMeter / pixelsPerMeter);
    case Scaled::Raw:
        break;
    }
    return value;
}

bool carriesTheNumber(const QString &text, double wanted)
{
    for (int digits = 6; digits >= 3; --digits) {
        if (text.contains(QString::number(wanted, 'g', digits)))
            return true;
    }
    return false;
}

} // namespace

// Every property the scene keeps is written into the export with the value the
// app itself uses. Not "something changed" -- the number. This is the test the
// exports never had: the old suite checked that every event and action was
// present, and a converter could write a body's friction as somebody else's
// default for years without a word.
TEST(Exporters, EveryPropertyIsExportedWithTheValueTheAppUses)
{
    auto engine = physics::EngineRegistry::create(QStringLiteral("Box2D"));
    ASSERT_TRUE(engine);
    const double ppm = 1000.0;      // what buildEverything draws at

    struct Where { const char *what; physics::PropertyList list; };
    const QVector<Where> groups {
        { "world", engine->worldProperties() },
        { "body",  engine->bodyProperties() },
        { "shape", engine->shapeProperties() },
    };

    for (const Where &group : groups) {
        for (const physics::JointParam &property : group.list) {
            if (!property.stored || property.type == physics::ParamType::Bool
                || property.type == physics::ParamType::Choice) {
                continue;   // flags and choices carry no number to look for
            }
            // A value nothing else in the scene happens to be, so finding it
            // in the output means it came from here.
            double wanted = qBound(property.minValue, 37.25, property.maxValue);
            if (property.decimals == 0)
                wanted = std::round(wanted);   // a count is a count on both sides
            if (qFuzzyCompare(wanted, property.defaultValue.toDouble()))
                continue;

            CanvasScene scene;
            buildEverything(&scene);
            if (group.what == QLatin1String("world"))
                scene.world().params[property.key] = wanted;
            else if (group.what == QLatin1String("body"))
                scene.bodies().first()->props().params[property.key] = wanted;
            else
                scene.shapes().first()->part().params[property.key] = wanted;

            for (const SceneExporter::Converter &converter : shipped()) {
                const Output out = exportWith(converter, &scene);
                ASSERT_TRUE(out.ok) << converter.id.toStdString() << ": "
                                    << out.error.toStdString();

                const QString said = out.log.join(QLatin1Char('\n'));
                const bool saidItCannot = said.contains(property.label, Qt::CaseInsensitive)
                                          || said.contains(property.key, Qt::CaseInsensitive);
                const bool knownGap =
                    kCannotExpress.contains(converter.id + QLatin1Char('/') + property.key);
                if (saidItCannot || knownGap)
                    continue;

                const double appUses = asTheAppUsesIt(property.key, wanted, ppm);
                EXPECT_TRUE(carriesTheNumber(out.text, appUses))
                    << converter.id.toStdString() << " writes the " << group.what << "'s "
                    << property.key.toStdString() << " as something other than the " << appUses
                    << " the app hands the solver (" << wanted
                    << " in scene units), and never said it could not write it";
            }
        }
    }
}

// A rule is code in every export, so both of its lists have to reach all three
// converters -- and what a converter cannot express it has to say, rather than
// writing half a rule. A scene that runs one way here and another way there is
// the failure this guards against.
TEST(Exporters, CompoundRulesAreWrittenOrReported)
{
    const QVector<SceneExporter::Converter> converters = shipped();
    ASSERT_GE(converters.size(), 3);

    const auto pastFrame = [](int n) {
        RuleCondition condition;
        condition.subjectName = Rule::world();
        condition.conditionKey = QStringLiteral("frame");
        condition.compare = Rule::Compare::Greater;
        condition.conditionValue = n;
        return condition;
    };

    CanvasScene scene;
    buildEverything(&scene);

    // Two readings joined, and two actions: ordinary boolean logic and two
    // statements, which every converter can write.
    Rule writable;
    writable.name = QStringLiteral("compound");
    writable.join = Rule::Join::All;
    writable.conditions = { pastFrame(5), pastFrame(10) };
    writable.actions.resize(2);
    writable.actions[0].targetName = QStringLiteral("crateBody");
    writable.actions[0].propertyKey = QStringLiteral("gravityScale");
    writable.actions[0].value = 0.25;
    writable.actions[1].targetName = QStringLiteral("crateBody");
    writable.actions[1].propertyKey = QStringLiteral("linearDamping");
    writable.actions[1].value = 0.75;

    // An unfinished rule never reaches a converter at all.
    Rule unfinished;
    unfinished.name = QStringLiteral("unfinishedRuleMarker");
    unfinished.conditions[0] = pastFrame(5);
    unfinished.conditions[0].subjectName.clear();

    scene.setRules({ writable, unfinished });

    for (const SceneExporter::Converter &converter : converters) {
        const Output out = exportWith(converter, &scene);
        ASSERT_TRUE(out.ok) << converter.id.toStdString() << ": " << out.error.toStdString();

        EXPECT_TRUE(out.text.contains(QStringLiteral("0.25")))
            << converter.id.toStdString() << " left the first action out";
        EXPECT_TRUE(out.text.contains(QStringLiteral("0.75")))
            << converter.id.toStdString() << " left the second action out";
        EXPECT_FALSE(out.text.contains(unfinished.name))
            << converter.id.toStdString() << " wrote an unfinished rule";
        for (const QString &line : std::as_const(out.log)) {
            EXPECT_FALSE(line.contains(QStringLiteral("was not exported")))
                << converter.id.toStdString() << ": " << line.toStdString();
        }
    }

    // Two events in one rule have no single loop to live in. That is allowed
    // to be unsupported; it is not allowed to be silent.
    Rule twoEvents;
    twoEvents.name = QStringLiteral("twoEvents");
    twoEvents.conditions.resize(2);
    twoEvents.conditions[0].subjectName = QStringLiteral("crate");
    twoEvents.conditions[0].eventId = QStringLiteral("contactBegin");
    twoEvents.conditions[1].subjectName = QStringLiteral("crate");
    twoEvents.conditions[1].eventId = QStringLiteral("contactEnd");
    twoEvents.actions[0].targetName = QStringLiteral("crateBody");
    twoEvents.actions[0].propertyKey = QStringLiteral("gravityScale");
    twoEvents.actions[0].value = 0.5;
    scene.setRules({ twoEvents });

    for (const SceneExporter::Converter &converter : converters) {
        const Output out = exportWith(converter, &scene);
        ASSERT_TRUE(out.ok) << converter.id.toStdString() << ": " << out.error.toStdString();
        bool said = false;
        for (const QString &line : std::as_const(out.log))
            said = said || line.contains(QStringLiteral("was not exported"));
        EXPECT_TRUE(said) << converter.id.toStdString()
                          << " wrote a rule it cannot express without saying so";
    }
}
