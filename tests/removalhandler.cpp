#include "CanvasScene.h"
#include "CircleItem.h"
#include "MainWindow.h"
#include "PhysicsBody.h"
#include "Rule.h"
#include "RulesPanel.h"
#include "SimulationController.h"

#include <QApplication>
#include <QComboBox>
#include <QTabWidget>
#include <functional>
#include <gtest/gtest.h>

// A pocket removes whatever enters it. A ball with a rule answering "to be
// removed" is not removed: the answer is carried out instead. A ball with no
// such rule goes, as it always did. And "Init state" puts a ball back where the
// run found it, standing still.

namespace {

struct Outcome {
    bool removed = false;
    qreal startX = 0.0;
    qreal x = 0.0;
    qreal movedInLastHalfSecond = 0.0;
};

// A ball rolling right at about 200 px/s through a sensor pocket at x = 300
// whose rule removes whatever enters. `answer` adds the ball's own rules.
Outcome roll(const QString &engine, const std::function<void(QVector<Rule> &, PhysicsBody *)> &answer)
{
    CanvasScene scene;
    scene.setSimulationEngineName(engine);
    scene.world().params["gravityY"] = 0.0;
    scene.setEditorMode(EditorMode::Physics);

    CircleItem *pocketShape = scene.addCircle(QPointF(300, 0));
    pocketShape->setName(QStringLiteral("pocket"));
    CircleItem *ballShape = scene.addCircle(QPointF(0, 0));
    ballShape->setName(QStringLiteral("ball"));
    scene.notifyShapesChanged();

    scene.selectForPhysics(pocketShape, true);
    PhysicsBody *pocket = scene.createBodyFromSelection();
    scene.clearPhysicsSelection();
    scene.selectForPhysics(ballShape, true);
    PhysicsBody *ball = scene.createBodyFromSelection();
    scene.clearPhysicsSelection();
    EXPECT_TRUE(pocket && ball);
    if (!pocket || !ball)
        return {};
    pocket->props().type = physics::BodyType::Static;
    pocketShape->part().params["isSensor"] = true;
    ball->props().params["velocityX"] = 4.0;

    QVector<Rule> rules;
    Rule sink;
    sink.subjectName = QStringLiteral("pocket");
    sink.eventId = QStringLiteral("sensorBegin");
    sink.targetName = Rule::otherObjectBody();
    sink.actionId = QStringLiteral("removeBody");
    rules << sink;
    answer(rules, ball);
    scene.setRules(rules);

    Outcome outcome;
    outcome.startX = ball->centerOfMassScenePos().x();

    SimulationController sim(&scene, nullptr);
    sim.setEngineName(engine);
    sim.start();
    for (int i = 0; i < 150; ++i)
        sim.stepFrame();
    const qreal halfSecondBefore = ball->centerOfMassScenePos().x();
    for (int i = 0; i < 30; ++i)
        sim.stepFrame();
    outcome.removed = ball->isRemoved();
    outcome.x = ball->centerOfMassScenePos().x();
    outcome.movedInLastHalfSecond = qAbs(outcome.x - halfSecondBefore);
    sim.stop();
    return outcome;
}

Rule toBeRemoved(PhysicsBody *ball)
{
    Rule rule;
    rule.subjectName = QStringLiteral("ball");
    rule.eventId = Rule::aboutToBeRemovedEvent();
    rule.targetName = ball->name();
    return rule;
}

void showRules(MainWindow &window)
{
    auto *panel = window.findChild<RulesPanel *>();
    for (QTabWidget *tabs : window.findChildren<QTabWidget *>())
        for (int i = 0; i < tabs->count(); ++i)
            if (tabs->widget(i) == panel || tabs->widget(i)->isAncestorOf(panel))
                tabs->setCurrentIndex(i);
    for (int i = 0; i < 30; ++i)
        QCoreApplication::processEvents();
}

} // namespace

TEST(RemovalHandler, WithNoAnswerTheBallIsRemoved)
{
    for (const QString &engine : { QStringLiteral("Box2D"), QStringLiteral("Chipmunk2D") }) {
        const Outcome outcome = roll(engine, [](QVector<Rule> &, PhysicsBody *) {});
        EXPECT_TRUE(outcome.removed) << engine.toStdString() << ": nothing answers, so the pocket takes the ball";
    }
}

TEST(RemovalHandler, AnAnswerIsCarriedOutInstead)
{
    for (const QString &engine : { QStringLiteral("Box2D"), QStringLiteral("Chipmunk2D") }) {
        const Outcome outcome = roll(engine, [](QVector<Rule> &rules, PhysicsBody *ball) {
            Rule back = toBeRemoved(ball);
            back.propertyKey = QStringLiteral("positionX");
            back.op = Rule::Op::Set;
            back.value = -200.0;
            rules << back;
        });
        EXPECT_FALSE(outcome.removed) << engine.toStdString() << ": the ball answered, so it stays";
        EXPECT_LT(outcome.x, 250.0) << engine.toStdString()
                                    << ": and was put back where the answer said -- it ended at x=" << outcome.x;
    }
}

TEST(RemovalHandler, AnAnswerThatRemovesReallyRemoves)
{
    const Outcome outcome = roll(QStringLiteral("Box2D"), [](QVector<Rule> &rules, PhysicsBody *ball) {
        Rule gone = toBeRemoved(ball);
        gone.actionId = QStringLiteral("removeBody");
        rules << gone;
    });
    EXPECT_TRUE(outcome.removed) << "answering a removal with a removal removes it, and does not ask again for ever";
}

// The pool table's cue ball: pocketed, it goes back to where it started and stops.
TEST(InitState, APocketedBallGoesBackAndStops)
{
    for (const QString &engine : { QStringLiteral("Box2D"), QStringLiteral("Chipmunk2D") }) {
        const Outcome outcome = roll(engine, [](QVector<Rule> &rules, PhysicsBody *ball) {
            Rule back = toBeRemoved(ball);
            back.actionId = Rule::initStateAction();
            rules << back;
        });
        EXPECT_FALSE(outcome.removed) << engine.toStdString() << ": the ball stays in the run";
        EXPECT_NEAR(outcome.x, outcome.startX, 3.0) << engine.toStdString() << ": back where it started";
        EXPECT_LT(outcome.movedInLastHalfSecond, 1.0) << engine.toStdString() << ": and standing still";
    }
}

TEST(InitState, PutsABodyBackWheneverARuleAsks)
{
    for (const QString &engine : { QStringLiteral("Box2D"), QStringLiteral("Chipmunk2D") }) {
        CanvasScene scene;
        scene.setSimulationEngineName(engine);
        scene.world().params["gravityY"] = 0.0;
        scene.setEditorMode(EditorMode::Physics);
        CircleItem *ballShape = scene.addCircle(QPointF(0, 0));
        ballShape->setName(QStringLiteral("ball"));
        scene.notifyShapesChanged();
        scene.selectForPhysics(ballShape, true);
        PhysicsBody *ball = scene.createBodyFromSelection();
        scene.clearPhysicsSelection();
        ASSERT_TRUE(ball);
        ball->props().params["velocityX"] = 4.0;
        ball->props().params["angularVelocity"] = 90.0;
        const QPointF start = ball->centerOfMassScenePos();

        Rule back;
        back.subjectName = Rule::world();
        back.conditionKey = QStringLiteral("time");
        back.compare = Rule::Compare::Greater;
        back.conditionValue = 0.5;
        back.targetName = ball->name();
        back.actionId = Rule::initStateAction();
        back.once = true;
        scene.setRules({ back });

        SimulationController sim(&scene, nullptr);
        sim.setEngineName(engine);
        sim.start();
        for (int i = 0; i < 60; ++i)
            sim.stepFrame();
        const QPointF after = ball->centerOfMassScenePos();
        for (int i = 0; i < 30; ++i)
            sim.stepFrame();
        EXPECT_NEAR(after.x(), start.x(), 3.0) << engine.toStdString() << ": back where the run started";
        EXPECT_NEAR(ball->centerOfMassScenePos().x(), after.x(), 1.0)
            << engine.toStdString() << ": with its speed gone";
        EXPECT_NEAR(ball->rotationDegrees(), 0.0, 1.0) << engine.toStdString() << ": facing the way it started";
        sim.stop();
    }
}

TEST(RemovalHandler, TheCardOffersTheEventAndInitState)
{
    MainWindow window;
    window.resize(1300, 900);
    window.show();
    for (int i = 0; i < 30; ++i)
        QCoreApplication::processEvents();

    auto *scene = window.findChild<CanvasScene *>();
    auto *panel = window.findChild<RulesPanel *>();
    ASSERT_TRUE(scene && panel);
    scene->setEditorMode(EditorMode::Physics);
    CircleItem *ballShape = scene->addCircle(QPointF(0, 0));
    ballShape->setName(QStringLiteral("ball"));
    scene->notifyShapesChanged();
    scene->selectForPhysics(ballShape, true);
    PhysicsBody *ball = scene->createBodyFromSelection();
    scene->clearPhysicsSelection();
    ASSERT_TRUE(ball);

    Rule rule = toBeRemoved(ball);
    rule.actionId = Rule::initStateAction();
    scene->setRules({ rule });
    scene->notifyRulesChanged();
    showRules(window);

    bool offersEvent = false;
    bool offersInitState = false;
    bool asksWhich = false;
    for (QComboBox *combo : panel->findChildren<QComboBox *>()) {
        offersEvent = offersEvent || combo->findText(QStringLiteral("to be removed")) >= 0;
        offersInitState = offersInitState || combo->findText(QStringLiteral("Init state")) >= 0;
        asksWhich = asksWhich || combo->objectName() == QStringLiteral("conditionOther");
    }
    EXPECT_TRUE(offersEvent) << "a ball's rule can watch for it being removed";
    EXPECT_FALSE(asksWhich) << "and there is no object to pick after it";
    EXPECT_TRUE(offersInitState) << "and Init state is among what the rule can do to the ball";
    window.close();
}

// The world's "starting simulation": a rule on it is carried out once, as the run
// starts and before anything steps -- and Stop still puts the scene back.
TEST(RunStarted, AStartRuleSetsThingsUp)
{
    for (const QString &engine : { QStringLiteral("Box2D"), QStringLiteral("Chipmunk2D") }) {
        CanvasScene scene;
        scene.setSimulationEngineName(engine);
        scene.world().params["gravityY"] = 0.0;
        scene.setEditorMode(EditorMode::Physics);
        CircleItem *ballShape = scene.addCircle(QPointF(0, 0));
        ballShape->setName(QStringLiteral("ball"));
        scene.notifyShapesChanged();
        scene.selectForPhysics(ballShape, true);
        PhysicsBody *ball = scene.createBodyFromSelection();
        scene.clearPhysicsSelection();
        ASSERT_TRUE(ball);
        const QPointF before = ball->centerOfMassScenePos();

        Rule setUp;
        setUp.subjectName = Rule::world();
        setUp.eventId = Rule::runStartedEvent();
        setUp.targetName = ball->name();
        setUp.propertyKey = QStringLiteral("positionX");
        setUp.op = Rule::Op::Set;
        setUp.value = 500.0;
        scene.setRules({ setUp });

        SimulationController sim(&scene, nullptr);
        sim.setEngineName(engine);
        sim.start();
        EXPECT_NEAR(ball->centerOfMassScenePos().x(), 500.0, 2.0)
            << engine.toStdString() << ": the start rule ran before the first step";
        for (int i = 0; i < 30; ++i)
            sim.stepFrame();
        EXPECT_NEAR(ball->centerOfMassScenePos().x(), 500.0, 2.0)
            << engine.toStdString() << ": and once -- nothing moved it back or again";
        sim.stop();
        EXPECT_NEAR(ball->centerOfMassScenePos().x(), before.x(), 0.5)
            << engine.toStdString() << ": Stop puts the scene back as it was before the run";
    }
}

TEST(RunStarted, TheWorldCardOffersItWithNoObjectToPick)
{
    MainWindow window;
    window.resize(1300, 900);
    window.show();
    for (int i = 0; i < 30; ++i)
        QCoreApplication::processEvents();

    auto *scene = window.findChild<CanvasScene *>();
    auto *panel = window.findChild<RulesPanel *>();
    ASSERT_TRUE(scene && panel);
    scene->setEditorMode(EditorMode::Physics);
    CircleItem *ballShape = scene->addCircle(QPointF(0, 0));
    ballShape->setName(QStringLiteral("ball"));
    scene->notifyShapesChanged();
    scene->selectForPhysics(ballShape, true);
    PhysicsBody *ball = scene->createBodyFromSelection();
    scene->clearPhysicsSelection();
    ASSERT_TRUE(ball);

    Rule setUp;
    setUp.subjectName = Rule::world();
    setUp.eventId = Rule::runStartedEvent();
    setUp.targetName = ball->name();
    setUp.propertyKey = QStringLiteral("positionX");
    setUp.value = 0.0;
    scene->setRules({ setUp });
    scene->notifyRulesChanged();
    showRules(window);

    bool offered = false;
    bool asksWhich = false;
    for (QComboBox *combo : panel->findChildren<QComboBox *>()) {
        offered = offered || combo->findText(QStringLiteral("starting simulation")) >= 0;
        asksWhich = asksWhich || combo->objectName() == QStringLiteral("conditionOther");
    }
    EXPECT_TRUE(offered) << "the world's rule can watch the simulation starting";
    EXPECT_FALSE(asksWhich) << "and it happens with nothing, so no other object is asked for";
    window.close();
}
