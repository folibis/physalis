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
    rule.subjectName = Rule::world();
    rule.conditionKey = QStringLiteral("frame");
    rule.compare = Rule::Compare::Multiple;
    rule.conditionValue = 30;
    rule.targetName = crateBody->name();
    rule.actionId = QStringLiteral("pushAt");
    rule.actionParams.insert(QStringLiteral("impulseX"), 5.0);
    scene->setRules({ rule });
}

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

        // The line that sets a body's starting velocity, whatever the format
        // spells it -- looking for the bare number anywhere in the file finds
        // every other 5 and 0.1 in it and proves nothing.
        QString velocityLine;
        for (const QString &line : out.text.split(QLatin1Char('\n'))) {
            if (line.contains(QStringLiteral("inearVelocity"))) {
                velocityLine = line.trimmed();
                break;
            }
        }
        ASSERT_FALSE(velocityLine.isEmpty())
            << converter.id.toStdString() << " writes no starting velocity at all";

        // Every number on that line, and one of them has to be the velocity.
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
