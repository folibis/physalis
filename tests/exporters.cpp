// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ruslan Muhlinin. See LICENSE.
#include "CanvasScene.h"
#include "CircleItem.h"
#include "EngineRegistry.h"
#include "IPhysicsEngine.h"
#include "Joint.h"
#include "PhysicsBody.h"
#include "PolygonItem.h"
#include "RayItem.h"
#include "RectangleItem.h"
#include "Rule.h"
#include "SceneVariable.h"
#include "SceneExporter.h"
#include "ShapeItem.h"

#include <QDir>
#include <QFile>
#include <QJSEngine>
#include <QJsonObject>
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
    // Rounded to half its shorter side, which is a capsule: the shape a
    // converter is most likely to flatten back into a rectangle.
    crate->setCornerRadius(20.0);
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

    // Named after the helper the C++ export defines for hatching a sensor, so
    // the converter is made to move one of the two out of the way.
    Joint *joint = scene->createJoint(QStringLiteral("revolute"), crateBody, wheelBody, 1, {});
    if (joint)
        joint->setName(QStringLiteral("hatch"));

    // A joint whose reading is a length, driven from another object's position
    // with an offset: the one shape of action that makes a converter write a
    // bare length rather than a point, which is where the web one went wrong.
    // Its spring is on, so the page has a joint it must draw as a coil as well
    // as one it draws as a rod -- the turn count is worked out at export time
    // and written into the drawing list.
    Joint *pull = scene->createJoint(QStringLiteral("distance"), crateBody, wheelBody, 1,
                                     {{QStringLiteral("enableSpring"), true},
                                      {QStringLiteral("hertz"), 2.0}});
    if (pull)
        pull->setName(QStringLiteral("pull"));

    Rule follow;
    follow.conditions[0].subjectName = Rule::world();
    follow.conditions[0].conditionKey = QStringLiteral("frame");
    follow.conditions[0].compare = Rule::Compare::Multiple;
    follow.conditions[0].conditionValue = 5;
    follow.actions[0].targetName = QStringLiteral("pull");
    follow.actions[0].propertyKey = QStringLiteral("length");
    follow.actions[0].op = Rule::Op::Set;
    follow.actions[0].sourceObject = crateBody->name();
    follow.actions[0].sourceProperty = QStringLiteral("positionX");
    follow.actions[0].sourceOffset = 213.0;

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

    // A variable, counted by one rule and read by another, so the generated
    // code has to declare it, reset it and both read and write it.
    SceneVariable score;
    score.name = QStringLiteral("score");
    score.type = SceneVariable::Type::Integer;
    score.initial = 4;
    // A timer too, so every converter is made to declare one, move it on each
    // step and turn its verbs into code. It counts in milliseconds and is the
    // one variable whose ops are not value writes.
    SceneVariable clock;
    clock.name = QStringLiteral("clock");
    clock.type = SceneVariable::Type::Timer;
    clock.initial = 250;
    scene->setVariables({ score, clock });

    Rule counts;
    counts.conditions[0].subjectName = crate->name();
    counts.conditions[0].eventId = QStringLiteral("contactBegin");
    counts.actions[0].targetName = Rule::variables();
    counts.actions[0].propertyKey = QStringLiteral("score");
    counts.actions[0].op = Rule::Op::Add;
    counts.actions[0].value = 1;

    Rule reads;
    reads.conditions[0].subjectName = Rule::variables();
    reads.conditions[0].conditionKey = QStringLiteral("score");
    reads.conditions[0].compare = Rule::Compare::Greater;
    reads.conditions[0].conditionValue = 9;
    reads.actions[0].targetName = crateBody->name();
    reads.actions[0].actionId = Rule::initStateAction();

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

    // The four timer verbs and a condition reading the count, so no converter
    // can pass by leaving one of them out.
    Rule startsClock;
    startsClock.conditions[0].subjectName = Rule::world();
    startsClock.conditions[0].eventId = Rule::runStartedEvent();
    startsClock.actions.resize(4);
    const Rule::Op verbs[4] = { Rule::Op::TimerStart, Rule::Op::TimerPause,
                                Rule::Op::TimerStop, Rule::Op::TimerReset };
    for (int v = 0; v < 4; ++v) {
        startsClock.actions[v].targetName = Rule::variables();
        startsClock.actions[v].propertyKey = QStringLiteral("clock");
        startsClock.actions[v].op = verbs[v];
    }

    Rule readsClock;
    readsClock.conditions[0].subjectName = Rule::variables();
    readsClock.conditions[0].conditionKey = QStringLiteral("clock");
    readsClock.conditions[0].compare = Rule::Compare::Greater;
    readsClock.conditions[0].conditionValue = 2000;
    readsClock.actions[0].targetName = crateBody->name();
    readsClock.actions[0].actionId = Rule::initStateAction();

    // A value rolled between two numbers, and a condition that only passes some
    // of the time: both have to reach every converter, and the seed with them.
    scene->setRandomSeed(20260102u);

    Rule rolled;
    rolled.conditions[0].subjectName = Rule::world();
    rolled.conditions[0].conditionKey = QStringLiteral("chance");
    rolled.conditions[0].compare = Rule::Compare::Less;
    rolled.conditions[0].conditionValue = 25.0;
    rolled.actions[0].targetName = crateBody->name();
    rolled.actions[0].propertyKey = QStringLiteral("velocityY");
    rolled.actions[0].op = Rule::Op::Set;
    rolled.actions[0].value = RuleNumber::range(-300.0, -100.0, 0.0);

    scene->setRules({ rule, follow, hit, touch, changed, counts, reads, startsClock, readsClock,
                      rolled });

    // What the editor shows in its corner readout. An angle is the one worth
    // watching here: converting it to degrees is the only thing that reaches for
    // pi, and pi is a name JavaScript does not have under Box2D's spelling.
    CanvasScene::Watch turned;
    turned.objectName = QStringLiteral("hatch");
    turned.propertyKey = QStringLiteral("angle");
    turned.label = QStringLiteral("Angle");
    scene->addWatch(turned);

    CanvasScene::Watch counted;
    counted.objectName = Rule::variables();
    counted.propertyKey = QStringLiteral("score");
    counted.label = QStringLiteral("score");
    scene->addWatch(counted);
}

// A world setting a format has no answer for. Each is named with the reason,
// so a converter that quietly drops a setting it could have written fails
// rather than hiding among them. Where a converter says so itself through
// io.log(), it does not need to be in here at all.
const QSet<QString> kCannotExpress {
    // Collision filters are written as literals, and in sixteen bits where the
    // format has only sixteen: never as the decimal the scene keeps.
    QStringLiteral("box2d-qt-project/categoryBits"),
    QStringLiteral("box2d-qt-project/maskBits"),
    QStringLiteral("box2d3wasm/categoryBits"),
    QStringLiteral("box2d3wasm/maskBits"),
    // The contact margin is not written as a number: it becomes Box2D's length
    // unit, which TheSceneScaleReachesEveryExport checks on its own.
    QStringLiteral("box2d-qt-project/contactMargin"),
    QStringLiteral("box2d3wasm/contactMargin"),
};


struct Output {
    bool ok = false;
    QString error;
    QStringList written;
    QStringList log;
    QString text;      // every file it wrote, run together
};

Output exportWith(const SceneExporter::Converter &converter, const CanvasScene *scene,
                  const QJsonObject &converterSettings = QJsonObject())
{
    Output out;
    QTemporaryDir folder;
    if (!folder.isValid()) {
        out.error = QStringLiteral("no temporary folder");
        return out;
    }
    // A converter's own settings reach it through the application's, filed
    // under its folder name: that is where the Options page puts them.
    QJsonObject settings;
    if (!converterSettings.isEmpty()) {
        QJsonObject exports;
        exports.insert(converter.id, converterSettings);
        settings.insert(QStringLiteral("Export"), exports);
    }
    out.ok = SceneExporter::run(converter, scene, folder.path(), settings,
                                &out.error, &out.written, &out.log);
    for (const QString &name : std::as_const(out.written)) {
        QFile file(QDir(folder.path()).filePath(name));
        if (file.open(QIODevice::ReadOnly | QIODevice::Text))
            out.text += QString::fromUtf8(file.readAll());
    }
    return out;
}


// The metre a converter chose to work at. It is not always the scene's: Box2D's
// tolerances are fixed lengths in metres and box2d3wasm cannot be told what a
// metre is, so that converter picks the metre at which those tolerances already
// are the contact margin the scene asked for. Everything it writes is in that
// metre, and the numbers below have to be read the same way.
double metreOf(const Output &out, double sceneScale)
{
    static const QRegularExpression declared(
        QStringLiteral(R"rx(PIXELS_PER_METER\s*=\s*([0-9.]+))rx"));
    const QRegularExpressionMatch found = declared.match(out.text);
    return found.hasMatch() ? found.captured(1).toDouble() : sceneScale;
}

} // namespace

// All three are found, and each one runs on a full scene and writes files. A
// converter that writes nothing has converted nothing, whatever it reports.
TEST(Exporters, EveryConverterRunsAndWritesSomething)
{
    const QVector<SceneExporter::Converter> converters = shipped();
    ASSERT_GE(converters.size(), 1) << "the shipped converters were not found under "
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

        // Nothing may reach the output with a placeholder still in it. A
        // template's own {{...}} is reported by the converter, but a
        // placeholder written in some other spelling is not a placeholder to
        // it at all: it is copied through as text and only the compiler ever
        // complains. Checking that the generated code *contains* the line
        // wanted does not catch that -- the broken line contains it too.
        static const QRegularExpression leftover(
            QStringLiteral("\\{\\{[A-Z0-9_]+\\}\\}|%[A-Z0-9_]+%"));
        const QRegularExpressionMatch found = leftover.match(out.text);
        EXPECT_FALSE(found.hasMatch())
            << converter.id.toStdString() << " wrote an unfilled placeholder: "
            << found.captured().toStdString();

        // The scene carries a timer, so the output has to move it on each step
        // and turn its four verbs into code. A verb the converter cannot write
        // falls through to "not exported", which is what this catches: the
        // timer would otherwise export as a number that never counts. The name
        // has to be the timer's own, not a placeholder standing in for it.
        // An identifier in front of it, not a placeholder and not a bare line:
        // the name itself may have been moved out of the way of something the
        // generated program already calls "clock".
        static const QRegularExpression tick(
            QStringLiteral("[A-Za-z_][A-Za-z0-9_]* \\+= dt \\* 1000"));
        EXPECT_TRUE(tick.match(out.text).hasMatch())
            << converter.id.toStdString() << " never moves the timer on by name";
        EXPECT_TRUE(out.text.contains(QStringLiteral("250")))
            << converter.id.toStdString() << " left the timer's starting value out";
        for (const QString &line : std::as_const(out.log)) {
            EXPECT_FALSE(line.contains(QStringLiteral("was not exported")))
                << converter.id.toStdString() << ": " << line.toStdString();
        }

        // A name that is perfectly reasonable in the editor can be one the
        // generated program already uses for something of its own -- a timer
        // called "clock" against clock() from <time.h>, a joint called "hatch"
        // against the helper that hatches a sensor. The converter has to move
        // its own names out of the way, and nothing but a compiler notices when
        // it does not, so the two known cases are checked by name here.
        if (converter.id == QStringLiteral("box2d-qt-project")) {
            EXPECT_FALSE(out.text.contains(QStringLiteral("float clock;")))
                << "a timer called clock was declared over clock() from <time.h>";
            EXPECT_FALSE(out.text.contains(QStringLiteral("b2JointId hatch,"))
                         || out.text.contains(QStringLiteral("b2JointId hatch;")))
                << "a joint called hatch was declared over the sensor-hatching helper";
        }
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

    for (const SceneExporter::Converter &converter : shipped()) {
        const Output out = exportWith(converter, &scene);
        ASSERT_TRUE(out.ok) << converter.id.toStdString() << ": " << out.error.toStdString();

        // 100 scene units a second, in whatever metre this converter works at:
        // a tenth of a metre a second at the scene's own, and the same motion
        // at any other. (What it used to write was the pace scale, 50/1000,
        // giving 5 -- a hundred times too fast.)
        const double expected = 100.0 / metreOf(out, 1000.0);

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
    const double ppm = scene.pixelsPerMeter();
    const double margin = scene.world().params.value(QStringLiteral("contactMargin"), 2.0).toDouble();

    for (const SceneExporter::Converter &converter : shipped()) {
        const Output out = exportWith(converter, &scene);
        ASSERT_TRUE(out.ok) << converter.id.toStdString() << ": " << out.error.toStdString();

        // Box2D's tolerances are fixed lengths in metres -- 5 mm of slop, a
        // contact made at four times that -- and a scene drawn at a thousand
        // units to the metre needs them brought down or everything touches far
        // too early. There are two ways to land them on the scene's contact
        // margin and an export has to do one of them.
        //
        // Telling Box2D what a metre is: 2 px of margin at 1000 px per metre is
        // a length unit of 0.1 and a slop of 0.0005 m. The line that does it
        // says so -- a bare "0.1" anywhere in the file is a density or an alpha.
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

        // Or choosing the metre at which they already are: box2d3wasm exposes
        // no b2SetLengthUnitsPerMeter, so its converter picks the scale instead
        // and declares it. 4 x 0.005 m has to come out as the scene's margin.
        const double metre = metreOf(out, ppm);
        const bool choseTheScale = qAbs(4.0 * 0.005 * metre - margin) < 0.05;

        EXPECT_TRUE(setsTheScale || choseTheScale)
            << converter.id.toStdString() << " neither tells Box2D what a metre is nor works at"
            << " one where its fixed tolerances are this scene's " << margin
            << " units of contact margin: it declares " << metre
            << " units to the metre, which makes contacts " << (4.0 * 0.005 * metre)
            << " units before shapes touch";
    }
}

// The editor draws every body's axes where its shapes balance, whatever kind of
// body it is. The page asked the solver for that point instead, and a static
// body has no mass, so Box2D handed back its origin -- putting the axes of every
// wall and floor somewhere the editor never draws them.
TEST(Exporters, BodyAxesAreWhereTheEditorDrawsThem)
{
    CanvasScene scene;
    buildEverything(&scene);

    for (const SceneExporter::Converter &converter : shipped()) {
        if (converter.id != QStringLiteral("box2d3wasm"))
            continue;              // the C++ export is painted by Box2D itself
        const Output out = exportWith(converter, &scene);
        ASSERT_TRUE(out.ok) << out.error.toStdString();

        bool sawAStaticOne = false;
        for (PhysicsBody *body : scene.bodies()) {
            const QRegularExpression record(
                QStringLiteral(R"rx(drawn\.push\(\{ id: %1, type: "(\w+)", centre: \[(-?[0-9.]+), (-?[0-9.]+)\])rx")
                    .arg(QRegularExpression::escape(body->name())));
            ASSERT_TRUE(record.isValid());
            const QRegularExpressionMatch found = record.match(out.text);
            ASSERT_TRUE(found.hasMatch())
                << "no drawing record for " << body->name().toStdString();

            // What the editor draws, brought back into the body's own frame.
            const QPointF offset = body->centerOfMassScenePos() - body->originScenePos();
            QTransform intoBody;
            intoBody.rotate(-body->rotationDegrees());
            const QPointF wanted = intoBody.map(offset);

            EXPECT_NEAR(found.captured(2).toDouble(), wanted.x(), 0.5)
                << body->name().toStdString() << "'s axes are drawn at the wrong x";
            EXPECT_NEAR(found.captured(3).toDouble(), wanted.y(), 0.5)
                << body->name().toStdString() << "'s axes are drawn at the wrong y";
            if (found.captured(1) == QLatin1String("static")
                && !qFuzzyIsNull(wanted.x() * wanted.x() + wanted.y() * wanted.y()))
                sawAStaticOne = true;
        }
        EXPECT_TRUE(sawAStaticOne)
            << "no static body whose shapes sit away from its origin, so this scene cannot"
               " tell a centre of mass from an origin and the test proves nothing";
    }
}

// A rounded box is a different shape from the rectangle its corners describe,
// and at half the shorter side it is a capsule. It has to reach both sides of a
// converter: the shape the solver collides with, and the one the page paints.
// The web page kept its own list of what to draw and dropped the radius from it,
// so every capsule in every exported scene came out square.
TEST(Exporters, ARoundedBoxIsRoundedInTheExport)
{
    CanvasScene scene;
    buildEverything(&scene);

    for (const SceneExporter::Converter &converter : shipped()) {
        const Output out = exportWith(converter, &scene);
        ASSERT_TRUE(out.ok) << converter.id.toStdString() << ": " << out.error.toStdString();

        EXPECT_TRUE(out.text.contains(QStringLiteral("RoundedBox")))
            << converter.id.toStdString() << " builds the crate as a plain box, so the solver"
            << " collides with a rectangle where the editor drew a capsule";

        if (converter.id != QStringLiteral("box2d3wasm"))
            continue;              // the C++ export is painted by Box2D itself
        const QRegularExpression round(QStringLiteral(R"rx(kind: "polygon"[^}]*radius: [0-9])rx"));
        ASSERT_TRUE(round.isValid());
        EXPECT_TRUE(round.match(out.text).hasMatch())
            << "the page draws the crate from four sharp corners, so the capsule it collides"
               " with is painted as a rectangle";
    }
}

// Two things the editor does that the page has to do as well, and did not.
//
// A clone is built from the copy the export already carries, never from the body
// in the world -- the editor clones a body a rule has taken away, which is the
// whole point of being able to clone one. The page wrapped the call in "if the
// original still exists", so a scene that removes its own objects made a few
// copies and then silently stopped making any.
//
// And stopping the run cannot be done where the rule fires, so it is left as a
// request for after the step. The page wrote the request and nothing ever read
// it back, so "stop the simulation" did nothing at all.
TEST(Exporters, CloningOutlivesItsSubjectAndStoppingTheRunIsActedOn)
{
    CanvasScene scene;
    buildEverything(&scene);

    Rule copies;                       // every 60 frames, clone the crate
    copies.conditions[0].subjectName = Rule::world();
    copies.conditions[0].conditionKey = QStringLiteral("frame");
    copies.conditions[0].compare = Rule::Compare::Multiple;
    copies.conditions[0].conditionValue = 60;
    copies.actions[0].targetName = QStringLiteral("crateBody");
    copies.actions[0].actionId = Rule::cloneAction();
    copies.actions[0].actionParams.insert(QStringLiteral("x"), 0.0);
    copies.actions[0].actionParams.insert(QStringLiteral("y"), -300.0);

    Rule halt;                         // and after a while, stop
    halt.conditions[0].subjectName = Rule::world();
    halt.conditions[0].conditionKey = QStringLiteral("frame");
    halt.conditions[0].compare = Rule::Compare::Greater;
    halt.conditions[0].conditionValue = 600;
    halt.actions[0].targetName = Rule::world();
    halt.actions[0].actionId = Rule::stopRunAction();

    // And something that takes a body away: only then does the export start
    // guarding what it writes against the body having gone, which is the guard
    // that must not reach the clone.
    Rule clears;
    clears.conditions[0].subjectName = Rule::world();
    clears.conditions[0].conditionKey = QStringLiteral("frame");
    clears.conditions[0].compare = Rule::Compare::Greater;
    clears.conditions[0].conditionValue = 300;
    clears.actions[0].targetName = QStringLiteral("wheelBody");
    clears.actions[0].actionId = QStringLiteral("removeBody");

    scene.setRules({ copies, halt, clears });

    for (const SceneExporter::Converter &converter : shipped()) {
        if (converter.id != QStringLiteral("box2d3wasm"))
            continue;
        const Output out = exportWith(converter, &scene);
        ASSERT_TRUE(out.ok) << out.error.toStdString();

        const QStringList lines = out.text.split(QLatin1Char('\n'));

        // Every place the page calls a clone helper, as opposed to defining one.
        int calls = 0;
        for (int i = 0; i < lines.size(); ++i) {
            const QString line = lines.at(i).trimmed();
            if (!line.startsWith(QStringLiteral("cloneOf_")))
                continue;              // the definition says "function cloneOf_..."
            ++calls;
            // Whatever opens the block this call sits in must not be asking
            // whether the body being copied is still in the world.
            for (int back = i - 1; back >= 0; --back) {
                const QString before = lines.at(back).trimmed();
                if (before.isEmpty() || before.startsWith(QStringLiteral("//")))
                    continue;
                EXPECT_FALSE(before.contains(QStringLiteral("IsValid")))
                    << "cloning is wrapped in \"" << before.toStdString()
                    << "\", so a scene that removes its own objects stops making copies";
                break;
            }
        }
        EXPECT_GT(calls, 0) << "the page never clones anything";

        // The request to stop has to be written and read back. Before, the rule
        // wrote it and nothing anywhere looked at it again.
        int writes = 0;
        int reads = 0;
        for (const QString &line : lines) {
            if (!line.contains(QStringLiteral("pendingRun")))
                continue;
            const QString trimmed = line.trimmed();
            if (trimmed.startsWith(QStringLiteral("pendingRun ="))
                || trimmed.startsWith(QStringLiteral("var pendingRun =")))
                ++writes;
            else
                ++reads;
        }
        EXPECT_GT(writes, 0) << "the rule asking the run to stop writes nothing";
        EXPECT_GT(reads, 0)
            << "the page asks for the run to stop and nothing ever reads the request,"
               " so stopping the simulation does nothing";
    }
}

// A card can watch several rays at once -- "this one or that one sees something"
// -- and the converter has to read every one of them. A ray is not an event the
// step hands out, it is a cast the converter makes itself, so there was no loop
// for the others to be folded into and only the first was ever used. The second
// ray was still cast and still drawn, so the page looked right and half the
// detections never happened.
TEST(Exporters, ARuleWatchingSeveralRaysReadsThemAll)
{
    CanvasScene scene;
    buildEverything(&scene);

    RayItem *left = scene.addRay(QPointF(-400, 0));
    ASSERT_NE(left, nullptr);
    left->setName(QStringLiteral("leftEye"));
    RayItem *right = scene.addRay(QPointF(400, 0));
    ASSERT_NE(right, nullptr);
    right->setName(QStringLiteral("rightEye"));

    Rule seen;
    seen.conditions[0].subjectName = left->name();
    seen.conditions[0].eventId = QStringLiteral("rayDetects");
    seen.conditions.append(RuleCondition {});
    seen.conditions[1].subjectName = right->name();
    seen.conditions[1].eventId = QStringLiteral("rayDetects");
    seen.join = Rule::Join::Any;
    seen.actions[0].targetName = Rule::otherObjectBody();
    seen.actions[0].actionId = QStringLiteral("removeBody");
    scene.setRules({ seen });

    for (const SceneExporter::Converter &converter : shipped()) {
        if (converter.id != QStringLiteral("box2d3wasm"))
            continue;
        const Output out = exportWith(converter, &scene);
        ASSERT_TRUE(out.ok) << out.error.toStdString();

        // The rule's own test has to mention both of them. Casting and drawing
        // a ray is not reading it: those happen whether a rule names it or not.
        QString test;
        const QStringList lines = out.text.split(QLatin1Char('\n'));
        for (int i = 0; i < lines.size(); ++i) {
            if (!lines.at(i).contains(QStringLiteral("var seen")) 
                && !lines.at(i).contains(QStringLiteral("Rule =")))
                continue;
        }
        for (const QString &line : lines) {
            const QString trimmed = line.trimmed();
            if (trimmed.contains(QStringLiteral(".hit"))
                && trimmed.startsWith(QStringLiteral("var "))) {
                test = trimmed;
                break;
            }
        }
        ASSERT_FALSE(test.isEmpty()) << "the page never tests a ray at all";
        EXPECT_TRUE(test.contains(QStringLiteral("leftEye")) || test.contains(QStringLiteral("ray")))
            << "the rule tests no ray: " << test.toStdString();

        // Both handles, whatever they ended up being called.
        int mentioned = 0;
        for (const QString &eye : { QStringLiteral("leftEye"), QStringLiteral("rightEye") }) {
            if (test.contains(eye))
                ++mentioned;
        }
        EXPECT_EQ(mentioned, 2)
            << "the rule watches two rays and its test reads " << mentioned << " of them: "
            << test.toStdString();
    }
}
// An outline encloses nothing. Box2D is given it as a row of segments and the
// editor draws a line along it -- so the page has to stroke it, not fill it. A
// closed one fell through to the solid-polygon path and came out as a block of
// colour over a shape the solver had as segments.
TEST(Exporters, AClosedOutlineIsStrokedAndNotFilled)
{
    CanvasScene scene;
    buildEverything(&scene);

    // Seven points and not convex, so it is an outline on both sides of the
    // converter rather than something it can hand Box2D as one polygon.
    QPolygonF outline;
    outline << QPointF(-400, -100) << QPointF(-200, -30) << QPointF(-20, -60)
            << QPointF(200, -30) << QPointF(400, -60) << QPointF(400, 20)
            << QPointF(-400, 20);
    auto *ramp = new PolygonItem(outline, true);
    ramp->setName(QStringLiteral("ramp"));
    scene.addItem(ramp);
    scene.notifyShapesChanged();
    scene.selectForPhysics(ramp, true);
    PhysicsBody *body = scene.createBodyFromSelection();
    ASSERT_NE(body, nullptr);
    body->props().type = physics::BodyType::Static;
    body->setName(QStringLiteral("rampBody"));
    scene.clearPhysicsSelection();

    for (const SceneExporter::Converter &converter : shipped()) {
        if (converter.id != QStringLiteral("box2d3wasm"))
            continue;
        const Output out = exportWith(converter, &scene);
        ASSERT_TRUE(out.ok) << out.error.toStdString();

        // The solver has it as segments ...
        EXPECT_TRUE(out.text.contains(QStringLiteral("b2CreateSegmentShape")))
            << "the outline was not built as segments at all";

        // ... so the drawing record must say segments too. Find the record for
        // the body holding it and read the kind back.
        QString record;
        for (const QString &line : out.text.split(QLatin1Char('\n'))) {
            if (line.contains(QStringLiteral("drawn.push"))
                && line.contains(QStringLiteral("rampBody")))
                record = line.trimmed();
        }
        ASSERT_FALSE(record.isEmpty()) << "no drawing record for the outline";
        EXPECT_TRUE(record.contains(QStringLiteral(R"rx(kind: "segments")rx")))
            << "a closed outline is painted as a filled shape: " << record.toStdString();
    }
}

// The editor keeps a readout in a corner of the canvas while a run is going,
// one line per property the scene was told to watch. An exported page showed
// none of it: the scene carries the list and the converter never looked.
TEST(Exporters, WhatTheSceneWatchesIsShownOnThePage)
{
    CanvasScene scene;
    buildEverything(&scene);

    CanvasScene::Watch watch;
    watch.objectName = Rule::variables();
    watch.propertyKey = QStringLiteral("score");
    watch.label = QStringLiteral("score");
    scene.addWatch(watch);

    for (const SceneExporter::Converter &converter : shipped()) {
        if (converter.id != QStringLiteral("box2d3wasm"))
            continue;
        const Output out = exportWith(converter, &scene);
        ASSERT_TRUE(out.ok) << out.error.toStdString();

        EXPECT_TRUE(out.text.contains(QStringLiteral("drawLog()")))
            << "the scene watches a property and the page draws no readout";
        EXPECT_TRUE(out.text.contains(QStringLiteral("score")))
            << "the readout does not name what it is showing";
    }
}
// The slingshot: a body marked Can Be Shot is flung with the mouse during a
// run, pulled back and let go. The whole gesture was missing from the exported
// page -- the body carried its settings, the page offered nothing to press on.
TEST(Exporters, ABodyThatCanBeShotCanBeShotOnThePage)
{
    CanvasScene scene;
    buildEverything(&scene);

    PhysicsBody *crate = nullptr;
    for (PhysicsBody *body : scene.bodies()) {
        if (body->name() == QStringLiteral("crateBody"))
            crate = body;
    }
    ASSERT_NE(crate, nullptr);
    crate->shot().enabled = true;
    crate->shot().fullImpulse = 5.0;
    crate->shot().maxPull = 300.0;

    for (const SceneExporter::Converter &converter : shipped()) {
        if (converter.id != QStringLiteral("box2d3wasm"))
            continue;
        const Output out = exportWith(converter, &scene);
        ASSERT_TRUE(out.ok) << out.error.toStdString();

        // The body is offered, with the two numbers that decide how hard it goes.
        EXPECT_TRUE(out.text.contains(QStringLiteral("shootable.push({ body: crateBody")))
            << "the page lists no body it can fling";

        // The spring on the scene's distance joint reaches the drawing list as
        // a turn count, and the revolute beside it as a zero: the page draws a
        // coil for one and a plain shaft for the other, and knows what a spring
        // is only from this number.
        EXPECT_TRUE(out.text.contains(QStringLiteral("[pull, "))
                    && !out.text.contains(QStringLiteral("[pull, 0]")))
            << "the spring joint reached the page with no turns, so it is drawn as a rod";
        EXPECT_TRUE(out.text.contains(QStringLiteral("jointsDrawn[j][0], jointsDrawn[j][1]")))
            << "the page does not pass the turn count to drawJoint";
        EXPECT_TRUE(out.text.contains(QStringLiteral("maxPull: 300")))
            << "the pull distance did not reach the page";
        EXPECT_TRUE(out.text.contains(QStringLiteral("fullImpulse: 5")))
            << "the shot strength did not reach the page";

        // Filled where the bodies are made: at the top of the file they do not
        // exist yet, and the list would hold nothing but undefined.
        const int listed = out.text.indexOf(QStringLiteral("shootable.push"));
        const int made = out.text.indexOf(QStringLiteral("function createWorld"));
        const int drawing = out.text.indexOf(QStringLiteral("--- drawing"));
        ASSERT_GE(made, 0);
        ASSERT_GE(drawing, 0);
        EXPECT_TRUE(listed > made && listed < drawing)
            << "the shootable list is built before the bodies exist, so it holds nothing";

        // And the gesture: pressing, pulling, letting go, and the line drawn
        // while aiming.
        // And the dotted ring just inside its outline: on a table of balls that
        // is how the cue ball is told apart.
        EXPECT_TRUE(out.text.contains(QStringLiteral("shootable: true")))
            << "the drawing record does not say the body can be flung, so it wears no mark";
        for (const QString &needed : { QStringLiteral("function beginShot"),
                                      QStringLiteral("function aimShot"),
                                      QStringLiteral("function releaseShot"),
                                      QStringLiteral("function drawShot"),
                                      QStringLiteral("function markShootable"),
                                      QStringLiteral("b2Body_ApplyLinearImpulseToCenter") }) {
            EXPECT_TRUE(out.text.contains(needed))
                << "the page has no " << needed.toStdString();
        }
    }
}

// And a scene with nothing to fling must not mention the gesture at all: the
// helpers would not be there, and naming one throws the moment a key is pressed.
TEST(Exporters, APageWithNothingToFlingNamesNoShotHelpers)
{
    CanvasScene scene;
    buildEverything(&scene);          // no body has Can Be Shot ticked

    for (const SceneExporter::Converter &converter : shipped()) {
        if (converter.id != QStringLiteral("box2d3wasm"))
            continue;
        const Output out = exportWith(converter, &scene);
        ASSERT_TRUE(out.ok) << out.error.toStdString();
        // The gesture, not the drawing: markShootable and the record's flag are
        // part of painting and are always there, doing nothing when no body
        // carries the mark.
        for (const QString &absent : { QStringLiteral("cancelShot"), QStringLiteral("beginShot"),
                                      QStringLiteral("aimShot"), QStringLiteral("releaseShot"),
                                      QStringLiteral("drawShot"), QStringLiteral("shootable.push") }) {
            EXPECT_FALSE(out.text.contains(absent))
                << "the page names " << absent.toStdString()
                << " though nothing in the scene can be flung";
        }
    }
}
// The page it writes has to be JavaScript. Everything else here reads the
// output for particular mistakes; this reads it the way a browser does. Two
// bugs got past the lot of them -- a C++ float suffix in "180.0f" and a C++
// brace-initialised list -- and each one stopped the whole scene script from
// parsing, so the page came up blank with one line in the console.
//
// Compiled, not run: wrapping it in a function expression gets the parser over
// every line without calling anything, so Box2D and the document do not have to
// be there.
TEST(Exporters, TheGeneratedPageIsValidJavaScript)
{
    CanvasScene scene;
    buildEverything(&scene);

    // Everything the shared fixture does not carry: a body to fling, an outline,
    // and a sensor several of which one rule watches.
    PhysicsBody *crate = nullptr;
    for (PhysicsBody *body : scene.bodies()) {
        if (body->name() == QStringLiteral("crateBody"))
            crate = body;
    }
    ASSERT_NE(crate, nullptr);
    crate->shot().enabled = true;

    for (const SceneExporter::Converter &converter : shipped()) {
        if (converter.id != QStringLiteral("box2d3wasm"))
            continue;
        const Output out = exportWith(converter, &scene);
        ASSERT_TRUE(out.ok) << out.error.toStdString();

        // The scene's own script is the last one on the page; the others are the
        // engine and its wasm.
        static const QRegularExpression block(
            QStringLiteral(R"rx(<script(?![^>]*\bid=)[^>]*>([\s\S]*?)</script>)rx"));
        ASSERT_TRUE(block.isValid());
        QString script;
        auto found = block.globalMatch(out.text);
        while (found.hasNext())
            script = found.next().captured(1);
        ASSERT_FALSE(script.isEmpty()) << "the page carries no script at all";

        QJSEngine js;
        const QJSValue result = js.evaluate(QStringLiteral("(function () {\n") + script
                                            + QStringLiteral("\n})"));
        const bool broken = result.isError()
                            && result.property(QStringLiteral("name")).toString()
                                   == QStringLiteral("SyntaxError");
        EXPECT_FALSE(broken)
            << "the page is not valid JavaScript, so nothing on it runs: "
            << result.property(QStringLiteral("message")).toString().toStdString()
            << " at line "
            << result.property(QStringLiteral("lineNumber")).toInt();
    }
}
// Six pockets watching for a ball, acting on whatever fell in. The converter
// refused every rule with more than one subject that acts on "the other one",
// because for a contact either shape of the pair could be the subject and there
// is no single answer. A sensor event names its two sides, so where every
// subject is a sensor the other one is always the visitor -- and a pool table
// that swallowed nothing was the cost of not saying so.
TEST(Exporters, SeveralSensorsInOneRuleActOnWhatEnteredThem)
{
    CanvasScene scene;
    buildEverything(&scene);

    QStringList pockets;
    for (ShapeItem *shape : scene.shapes()) {
        if (pockets.size() >= 2)
            break;
        if (shape->name() == QStringLiteral("ground"))
            continue;
        shape->part().params[QStringLiteral("isSensor")] = true;
        pockets << shape->name();
    }
    ASSERT_EQ(pockets.size(), 2) << "the scene has too few shapes to make two sensors";

    Rule swallowed;
    swallowed.conditions[0].subjectName = pockets.at(0);
    swallowed.conditions[0].eventId = QStringLiteral("sensorBegin");
    swallowed.conditions.append(RuleCondition {});
    swallowed.conditions[1].subjectName = pockets.at(1);
    swallowed.conditions[1].eventId = QStringLiteral("sensorBegin");
    swallowed.join = Rule::Join::Any;
    swallowed.actions[0].targetName = Rule::otherObjectBody();
    swallowed.actions[0].actionId = QStringLiteral("removeBody");
    scene.setRules({ swallowed });

    for (const SceneExporter::Converter &converter : shipped()) {
        if (converter.id != QStringLiteral("box2d3wasm"))
            continue;
        const Output out = exportWith(converter, &scene);
        ASSERT_TRUE(out.ok) << out.error.toStdString();

        EXPECT_FALSE(out.text.contains(QStringLiteral("Not exported")))
            << "the rule was refused: " << out.log.join(QStringLiteral(" | ")).toStdString();
        // What fell in is the visitor, named plainly rather than worked out from
        // whichever side of the pair the subject turned out to be.
        EXPECT_TRUE(out.text.contains(QStringLiteral("other = visitor")))
            << "the rule does not act on what entered the sensor";

        // Box2D reports an overlap only when both sides asked for sensor events,
        // and nothing asks by default. The editor gives them to every shape once
        // a rule watches a sensor; without the same here the pockets saw nothing
        // but the balls the scene happened to have ticked by hand.
        const int shapes = out.text.count(QStringLiteral("b2DefaultShapeDef()"));
        const int listening = out.text.count(QStringLiteral("enableSensorEvents = true"));
        EXPECT_EQ(listening, shapes)
            << "only " << listening << " of " << shapes << " shapes report sensor overlaps,"
            << " so a sensor sees the ones that were ticked by hand and no others";
    }
}
// Where the page gets Box2D is the reader's choice, and each answer has to
// produce a page that actually loads it: carried inside the file, carried
// beside it, or fetched. Built in is the only one that needs no server.
TEST(Exporters, TheEngineArrivesTheWayTheSettingSays)
{
    CanvasScene scene;
    buildEverything(&scene);

    for (const SceneExporter::Converter &converter : shipped()) {
        if (converter.id != QStringLiteral("box2d3wasm"))
            continue;

        const auto withEngine = [&](const QString &how) {
            QJsonObject mine;
            mine.insert(QStringLiteral("engineSource"), how);
            return exportWith(converter, &scene, mine);
        };

        // Carried inside: one file, and the wasm is in it as text rather than
        // something to fetch.
        const Output inside = withEngine(QStringLiteral("Built into the page"));
        ASSERT_TRUE(inside.ok) << inside.error.toStdString();
        EXPECT_EQ(inside.written.size(), 1)
            << "the engine is built in and the export is more than one file";
        EXPECT_TRUE(inside.text.contains(QStringLiteral("id=\"wasm\"")))
            << "the wasm is not carried in the page";
        EXPECT_FALSE(inside.text.contains(QStringLiteral("<script type=\"module\"")))
            << "the page imports something though the engine is built in";

        // Beside: two files, and the page names the other one.
        const Output beside = withEngine(QStringLiteral("Beside the page"));
        ASSERT_TRUE(beside.ok) << beside.error.toStdString();
        EXPECT_TRUE(beside.written.contains(QStringLiteral("box2d3wasm.js")))
            << "the engine was not written beside the page";
        EXPECT_TRUE(beside.text.contains(QStringLiteral("src=\"box2d3wasm.js\"")))
            << "the page does not load the file written beside it";
        // And the page itself no longer carries it: that is the point of the
        // engine being beside it rather than in it.
        EXPECT_FALSE(beside.text.contains(QStringLiteral("id=\"wasm\"")))
            << "the engine was written beside the page and carried inside it as well";

        // From a CDN: neither is written, and the page imports it.
        const Output fetched = withEngine(QStringLiteral("From a CDN"));
        ASSERT_TRUE(fetched.ok) << fetched.error.toStdString();
        EXPECT_EQ(fetched.written.size(), 1)
            << "the engine comes from a CDN and something was written for it anyway";
        EXPECT_TRUE(fetched.text.contains(QStringLiteral("<script type=\"module\"")))
            << "nothing imports the engine";
        EXPECT_FALSE(fetched.text.contains(QStringLiteral("id=\"wasm\"")))
            << "the wasm is carried although it was to be fetched";
        bool saidSo = false;
        for (const QString &line : std::as_const(fetched.log)) {
            if (line.contains(QStringLiteral("needs a server")))
                saidSo = true;
        }
        EXPECT_TRUE(saidSo)
            << "the page will not open from a file any more and the export did not say so";
    }
}
// Chance and rolled values have to reach both converters, or a scene that
// works in the editor does nothing in an export of itself -- which is how
// every other rule feature here has gone wrong at least once.
TEST(Exporters, RandomReachesEveryExport)
{
    CanvasScene scene;
    buildEverything(&scene);
    scene.setRandomSeed(20260102u);

    Rule rolled;                      // a value given as a range
    rolled.conditions[0].subjectName = Rule::world();
    rolled.conditions[0].conditionKey = QStringLiteral("frame");
    rolled.conditions[0].compare = Rule::Compare::Multiple;
    rolled.conditions[0].conditionValue = 30;
    rolled.actions[0].targetName = QStringLiteral("crateBody");
    rolled.actions[0].propertyKey = QStringLiteral("velocityX");
    rolled.actions[0].op = Rule::Op::Set;
    rolled.actions[0].value = RuleNumber::range(-200.0, 200.0, 50.0);

    Rule sometimes;                   // and a chance condition
    sometimes.conditions[0].subjectName = Rule::world();
    sometimes.conditions[0].conditionKey = QStringLiteral("chance");
    sometimes.conditions[0].compare = Rule::Compare::Less;
    sometimes.conditions[0].conditionValue = 25.0;
    sometimes.actions[0].targetName = Rule::variables();
    sometimes.actions[0].propertyKey = QStringLiteral("score");
    sometimes.actions[0].op = Rule::Op::Add;
    sometimes.actions[0].value = 1.0;

    scene.setRules({ rolled, sometimes });

    for (const SceneExporter::Converter &converter : shipped()) {
        const Output out = exportWith(converter, &scene);
        ASSERT_TRUE(out.ok) << converter.id.toStdString() << ": " << out.error.toStdString();

        EXPECT_FALSE(out.text.contains(QStringLiteral("Not exported")))
            << converter.id.toStdString() << " refused a rule: "
            << out.log.join(QStringLiteral(" | ")).toStdString();

        // The generator, carrying the scene's seed so the run repeats.
        EXPECT_TRUE(out.text.contains(QStringLiteral("20260102")))
            << converter.id.toStdString() << " did not carry the scene's random seed,"
            << " so its run cannot be the same twice";
        EXPECT_TRUE(out.text.contains(QStringLiteral("0x6D2B79F5")))
            << converter.id.toStdString() << " writes no generator, so nothing is random";

        // The range is rolled where the value would have been written. The
        // fixture's is stepped, so it is the stepping form.
        EXPECT_TRUE(out.text.contains(QStringLiteral("rollSteps(")))
            << converter.id.toStdString() << " wrote a stepped range as a plain number";

        // And the chance condition reads the generator rather than a counter.
        EXPECT_TRUE(out.text.contains(QStringLiteral("randomUnit()")))
            << converter.id.toStdString() << " does not read the world's chance";

        // And a parameter of an action is a number like any other: where a
        // clone lands can be rolled, not only what a rule writes.
        CanvasScene scattered;
        buildEverything(&scattered);
        Rule copies;
        copies.conditions[0].subjectName = Rule::world();
        copies.conditions[0].conditionKey = QStringLiteral("frame");
        copies.conditions[0].compare = Rule::Compare::Multiple;
        copies.conditions[0].conditionValue = 60;
        copies.actions[0].targetName = QStringLiteral("crateBody");
        copies.actions[0].actionId = Rule::cloneAction();
        copies.actions[0].actionParams.insert(QStringLiteral("x"),
                                              RuleNumber::range(-400.0, 400.0, 20.0));
        copies.actions[0].actionParams.insert(QStringLiteral("y"), -300.0);
        scattered.setRules({ copies });

        const Output spread = exportWith(converter, &scattered);
        ASSERT_TRUE(spread.ok) << spread.error.toStdString();
        EXPECT_TRUE(spread.text.contains(QStringLiteral("cloneOf_")))
            << converter.id.toStdString() << " did not write the clone at all";
        EXPECT_TRUE(spread.text.contains(QStringLiteral("rollSteps(")))
            << converter.id.toStdString()
            << " wrote a clone's rolled position as a fixed one, so every copy"
               " would land in the same place";
    }
}
// The web converter writes JavaScript by translating the same C++-spelled code
// the other one emits, so anything it fails to translate reaches the page as
// something that runs without complaint and does the wrong thing. m(213) was
// the one that bit: two arguments build a point, one was C++'s bare length, and
// in JavaScript the second quietly produced a vector where a number was wanted,
// so the scene loaded, drew, stepped, and moved nothing.
TEST(Exporters, NoCppSpellingsSurviveIntoJavaScript)
{
    CanvasScene scene;
    buildEverything(&scene);

    struct Leftover { const char *pattern; const char *what; };
    static const Leftover leftovers[] {
        { R"rx(\bm\([^(),]*\))rx", "a one-argument m(), which builds a vector here" },
        { R"rx(\bb2Vec2\s*\{)rx", "a brace-initialised b2Vec2" },
        { R"rx(\bstd::)rx", "the C++ standard library" },
        { R"rx(\bnullptr\b)rx", "nullptr" },
        { R"rx([0-9]f\b)rx", "a float suffix" },
        { R"rx(\b(?:const\s+)?(?:float|int|bool|double)\s+\w+\s*=)rx", "a typed declaration" },
        { R"rx(\b(?:b2MinFloat|b2MaxFloat|b2AbsFloat)\s*\()rx", "a Box2D maths helper the bindings do not carry" },
        { R"rx(\.(?:begin|end|hit|move|beginTouch|endTouch)Events\s*\[)rx", "an event array subscript" },
        // Everything Box2D is reached through the module. A name that is not -- B2_PI,
        // b2ClampFloat -- is C++ that happens to parse, and throws the moment it runs.
        { R"rx((?<!b2\.)\b(?:b2[A-Z]\w*|B2_\w+))rx", "a Box2D name not reached through the module" },
    };

    for (const SceneExporter::Converter &converter : shipped()) {
        if (converter.id != QStringLiteral("box2d3wasm"))
            continue;
        const Output out = exportWith(converter, &scene);
        ASSERT_TRUE(out.ok) << out.error.toStdString();
        // Only the part the converter wrote: the vendored library is inlined
        // ahead of it and is full of C++ spellings of its own.
        const int mine = out.text.indexOf(QStringLiteral("generated by Physalis"));
        ASSERT_GE(mine, 0) << "the generated code is not marked, so it cannot be told"
                              " apart from the library inlined beside it";
        const QString written = out.text.mid(mine);
        for (const Leftover &leftover : leftovers) {
            const QRegularExpression pattern(QString::fromLatin1(leftover.pattern));
            ASSERT_TRUE(pattern.isValid()) << leftover.pattern;
            for (const QString &line : written.split(QLatin1Char('\n'))) {
                if (line.trimmed().startsWith(QStringLiteral("//")))
                    continue;                      // a comment is prose, left alone
                EXPECT_FALSE(pattern.match(line).hasMatch())
                    << "the page carries " << leftover.what << ": " << line.trimmed().toStdString();
            }
        }
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
    // Gravity and the thresholds beside it used to be quoted at the engine's
    // reference scale. They are plain metres and seconds now unless a scene
    // asks otherwise, and this one does not.
    static const QSet<QString> pace {};
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

                // At the metre this converter chose, not necessarily the scene's.
                const double metre = metreOf(out, ppm);
                double appUses = asTheAppUsesIt(property.key, wanted, metre);
                // A density is per unit area, and an area is the square of the
                // scale: a converter working at its own metre carries the
                // difference here, which is what keeps every mass the editor's.
                if (property.key == QLatin1String("density"))
                    appUses = wanted * (metre / ppm) * (metre / ppm);
                // A speed or an acceleration is quoted per second at the
                // scene's metre, so a converter working at its own has to
                // restate it there -- the same motion, a different metre.
                static const QSet<QString> perSecond {
                    QStringLiteral("gravityX"), QStringLiteral("gravityY"),
                    QStringLiteral("maximumLinearSpeed"), QStringLiteral("contactSpeed"),
                    QStringLiteral("restitutionThreshold"),
                    QStringLiteral("hitEventThreshold"), QStringLiteral("sleepThreshold"),
                };
                if (perSecond.contains(property.key))
                    appUses = wanted * (ppm / metre);
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
    ASSERT_GE(converters.size(), 1);

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
