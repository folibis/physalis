#include "CanvasScene.h"
#include "CircleItem.h"
#include "ExplosionItem.h"
#include "Joint.h"
#include "PhysicsBody.h"
#include "RayItem.h"
#include "RectangleItem.h"
#include "Rule.h"
#include "SceneExporter.h"

#include <QDir>
#include <QJsonObject>
#include <QTemporaryDir>
#include <gtest/gtest.h>

// Every event a rule can wait for and every action it can take, in one scene,
// through every export converter: a converter that leaves one out says so in
// its log, and that is what this reads.

namespace {

QString shippedConverters()
{
    QDir dir(QStringLiteral(__FILE__));
    dir.cdUp();     // tests/
    dir.cdUp();     // the project root
    return dir.absoluteFilePath(QStringLiteral("exporters"));
}

struct Everything {
    CanvasScene scene;
    QStringList ruleNames;
};

PhysicsBody *bodyOf(CanvasScene &scene, ShapeItem *shape, physics::BodyType type)
{
    scene.clearPhysicsSelection();
    scene.selectForPhysics(shape, true);
    PhysicsBody *body = scene.createBodyFromSelection();
    body->props().type = type;
    scene.clearPhysicsSelection();
    return body;
}

void build(Everything &e)
{
    CanvasScene &scene = e.scene;
    scene.setSimulationEngineName(QStringLiteral("Box2D"));

    auto *ground = scene.addRectangle(QPointF(-300, 200));
    ground->setRect(QRectF(0, 0, 600, 40));
    ground->setName(QStringLiteral("ground"));
    auto *box = scene.addRectangle(QPointF(0, 0));
    box->setName(QStringLiteral("box"));
    auto *ball = scene.addCircle(QPointF(100, 0));
    ball->setName(QStringLiteral("ball"));
    auto *pocket = scene.addCircle(QPointF(200, 150));
    pocket->setName(QStringLiteral("pocket"));
    scene.notifyShapesChanged();
    scene.setEditorMode(EditorMode::Physics);

    PhysicsBody *groundBody = bodyOf(scene, ground, physics::BodyType::Static);
    PhysicsBody *boxBody = bodyOf(scene, box, physics::BodyType::Dynamic);
    bodyOf(scene, ball, physics::BodyType::Dynamic)->setName(QStringLiteral("ballBody"));
    bodyOf(scene, pocket, physics::BodyType::Static);
    pocket->part().params[QStringLiteral("isSensor")] = true;
    boxBody->setName(QStringLiteral("boxBody"));

    QVariantMap limits;
    limits.insert(QStringLiteral("enableLimit"), true);
    limits.insert(QStringLiteral("lowerAngle"), -30.0);
    limits.insert(QStringLiteral("upperAngle"), 30.0);
    Joint *hinge = scene.createJoint(QStringLiteral("revolute"), groundBody, boxBody, 1, limits);
    hinge->setName(QStringLiteral("hinge"));

    RayItem *ray = scene.addRay(QPointF(-200, 0));
    ray->setName(QStringLiteral("eye"));
    ExplosionItem *blast = scene.addExplosion(QPointF(0, 100));
    blast->setName(QStringLiteral("blast"));

    QVector<Rule> rules;
    // Each event, answered by a plain property change.
    const auto onEvent = [&](const QString &subject, const QString &event, const QString &with = QString()) {
        Rule rule;
        rule.name = QStringLiteral("event ") + event;
        rule.subjectName = subject;
        rule.eventId = event;
        if (!with.isEmpty())
            rule.conditionValue = with;
        rule.targetName = QStringLiteral("boxBody");
        rule.propertyKey = QStringLiteral("velocityX");
        rule.value = 10.0;
        rules << rule;
    };
    onEvent(QStringLiteral("box"), QStringLiteral("contactBegin"), QStringLiteral("ground"));
    onEvent(QStringLiteral("box"), QStringLiteral("contactEnd"), QStringLiteral("ground"));
    onEvent(QStringLiteral("box"), QStringLiteral("contactHit"));
    onEvent(QStringLiteral("box"), QStringLiteral("preSolve"));
    onEvent(QStringLiteral("pocket"), QStringLiteral("sensorBegin"));
    onEvent(QStringLiteral("pocket"), QStringLiteral("sensorEnd"));
    onEvent(QStringLiteral("boxBody"), QStringLiteral("bodyMoved"));
    onEvent(QStringLiteral("boxBody"), QStringLiteral("bodyFellAsleep"));
    onEvent(QStringLiteral("eye"), QStringLiteral("rayDetects"), QStringLiteral("box"));
    onEvent(QStringLiteral("hinge"), QStringLiteral("limitLower"));
    onEvent(QStringLiteral("hinge"), QStringLiteral("limitUpper"));
    onEvent(QStringLiteral("hinge"), QStringLiteral("limitEither"));
    onEvent(Rule::world(), Rule::runStartedEvent());
    onEvent(QStringLiteral("ballBody"), Rule::aboutToBeRemovedEvent());

    // Each action, on a timer.
    const auto doAction = [&](const QString &target, const QString &action, const QVariantMap &params = {}) {
        Rule rule;
        rule.name = QStringLiteral("action ") + action;
        rule.subjectName = Rule::world();
        rule.conditionKey = QStringLiteral("time");
        rule.compare = Rule::Compare::Greater;
        rule.conditionValue = 1.0;
        rule.targetName = target;
        rule.actionId = action;
        rule.actionParams = params;
        rules << rule;
    };
    doAction(QStringLiteral("blast"), QStringLiteral("explode"), {{QStringLiteral("radius"), 200.0}});
    doAction(QStringLiteral("boxBody"), QStringLiteral("pushAt"), {{QStringLiteral("impulseX"), 1.0}});
    doAction(QStringLiteral("boxBody"), QStringLiteral("pushForceAt"), {{QStringLiteral("impulseX"), 1.0}});
    doAction(QStringLiteral("boxBody"), QStringLiteral("resetMass"));
    doAction(QStringLiteral("ballBody"), QStringLiteral("removeBody"));
    doAction(QStringLiteral("hinge"), QStringLiteral("breakJoint"));
    doAction(Rule::world(), Rule::stopRunAction());
    doAction(Rule::world(), Rule::holdRunAction());
    doAction(QStringLiteral("boxBody"), Rule::initStateAction());
    doAction(QStringLiteral("boxBody"), Rule::cloneAction(),
             {{Rule::cloneXParam(), 50.0}, {Rule::cloneYParam(), -100.0}});

    scene.setRules(rules);
    for (const Rule &rule : rules)
        e.ruleNames << rule.name;
}

// The rules a converter says it could not export.
QStringList droppedBy(const QString &converterId, CanvasScene &scene, const QStringList &names)
{
    for (const SceneExporter::Converter &converter : SceneExporter::discover(shippedConverters())) {
        if (converter.id != converterId)
            continue;
        QTemporaryDir output;
        QString error;
        QStringList log;
        if (!SceneExporter::run(converter, &scene, output.path(), QJsonObject(), &error, nullptr, &log))
            return { QStringLiteral("export failed: ") + error };
        QStringList dropped;
        for (const QString &name : names) {
            for (const QString &line : log) {
                if (line.startsWith(QStringLiteral("Rule ") + name + QStringLiteral(" was not exported"))) {
                    dropped << line;
                    break;
                }
            }
        }
        return dropped;
    }
    return { QStringLiteral("no converter ") + converterId };
}

} // namespace

class ExportCoverage : public ::testing::TestWithParam<const char *> {};

TEST_P(ExportCoverage, EveryEventAndActionIsExported)
{
    Everything e;
    build(e);
    const QStringList dropped = droppedBy(QString::fromLatin1(GetParam()), e.scene, e.ruleNames);
    EXPECT_TRUE(dropped.isEmpty()) << dropped.join(QStringLiteral("\n")).toStdString();
}

INSTANTIATE_TEST_SUITE_P(Converters, ExportCoverage,
                         ::testing::Values("box2d-qt-project", "planck-js", "qml-box2d"));
