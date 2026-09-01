#include "CanvasScene.h"
#include "PhysicsBody.h"
#include "RectangleItem.h"
#include "Rule.h"
#include "SceneSerializer.h"
#include "ShapeItem.h"
#include "SimulationController.h"
#include "EngineRegistry.h"

#include <QApplication>
#include <gtest/gtest.h>
#include <algorithm>
#include <cstdio>

// A ring of loose blocks around a bomb. Returns how far they have scattered.
static qreal scatter(qreal impulse, qreal radius, bool *ok)
{
    CanvasScene scene;
    scene.setSimulationEngineName(QStringLiteral("Box2D"));
    scene.world().gravity = QPointF(0.0, 0.0);   // so only the blast moves them

    auto *bomb = new RectangleItem;
    bomb->setRect(QRectF(0, 0, 20, 20));
    bomb->setPos(-10, -10);
    bomb->setName(QStringLiteral("bomb"));
    scene.addItem(bomb);

    QVector<ShapeItem *> debris;
    for (int i = 0; i < 8; ++i) {
        const qreal angle = i * 2.0 * M_PI / 8.0;
        auto *block = new RectangleItem;
        block->setRect(QRectF(0, 0, 30, 30));
        block->setPos(qCos(angle) * 120.0 - 15.0, qSin(angle) * 120.0 - 15.0);
        block->setName(QStringLiteral("debris%1").arg(i));
        scene.addItem(block);
        debris << block;
    }
    scene.notifyShapesChanged();
    scene.setEditorMode(EditorMode::Physics);

    PhysicsBody *bombBody = nullptr;
    for (ShapeItem *s : scene.shapes()) {
        scene.selectForPhysics(s, true);
        PhysicsBody *body = scene.createBodyFromSelection();
        if (s == bomb) {
            bombBody = body;
            body->props().type = physics::BodyType::Static;   // the bomb stays put
        }
        scene.clearPhysicsSelection();
    }

    // One rule: the moment the run starts, set it off.
    Rule bang;
    bang.subjectName = bombBody->name();
    bang.conditionKey = QStringLiteral("positionX");
    bang.compare = Rule::Compare::Greater;
    bang.conditionValue = -1e9;            // true immediately
    bang.targetName = bombBody->name();
    bang.actionId = QStringLiteral("explode");
    bang.actionParams.insert(QStringLiteral("impulse"), impulse);
    bang.actionParams.insert(QStringLiteral("radius"), radius);
    bang.actionParams.insert(QStringLiteral("falloff"), radius / 2.0);
    bang.once = true;
    scene.setRules({ bang });

    QVector<QPointF> before;
    for (ShapeItem *s : debris)
        before << s->sceneBoundingRect().center();

    SimulationController sim(&scene, nullptr);
    sim.setEngineName(QStringLiteral("Box2D"));
    sim.start();
    for (int f = 0; f < 120; ++f)
        sim.stepFrame();

    qreal moved = 0.0;
    for (int i = 0; i < debris.size(); ++i)
        moved += QLineF(before[i], debris[i]->sceneBoundingRect().center()).length();
    *ok = true;
    sim.stop();
    return moved / debris.size();
}

TEST(ExplodeAction, Behaves)
{
    auto engine = physics::EngineRegistry::create(QStringLiteral("Box2D"));
    QStringList paramKeys;
    bool offered = false;
    for (const physics::ActionType &a : engine->bodyActions())
        if (a.id == QLatin1String("explode")) {
            offered = true;
            for (const physics::JointParam &p : a.params)
                paramKeys << p.key;
        }
    EXPECT_TRUE(offered) << "Explode is a declared body action";
    EXPECT_TRUE(paramKeys.contains(QStringLiteral("impulse"))
              && paramKeys.contains(QStringLiteral("radius"))
              && paramKeys.contains(QStringLiteral("falloff"))) << "it carries impulse, radius and falloff";
    // Held in a local: bodyProperties() returns by value, so begin() and end()
    // on two separate calls are iterators into two different temporaries.
    const physics::PropertyList bodyProps = engine->bodyProperties();
    EXPECT_TRUE(std::none_of(bodyProps.begin(), bodyProps.end(),
                       [](const physics::JointParam &p) {
                           return p.key.contains(QStringLiteral("explo"));
                       })) << "and no body pretends to have blast settings";

    bool a = false, b = false, c = false;
    const qreal quiet = scatter(0.0, 300.0, &a);
    const qreal bang = scatter(3.0, 300.0, &b);
    const qreal outOfRange = scatter(3.0, 40.0, &c);
    EXPECT_TRUE(quiet < 1.0) << "nothing moves without one";
    EXPECT_TRUE(bang > 20.0) << "the blast scatters them";
    EXPECT_TRUE(outOfRange < bang / 4.0) << "and the radius decides who it reaches";

    CanvasScene scene;
    auto *shape = new RectangleItem;
    shape->setName(QStringLiteral("mine"));
    scene.addItem(shape);
    scene.notifyShapesChanged();
    scene.setEditorMode(EditorMode::Physics);
    scene.selectForPhysics(shape, true);
    PhysicsBody *body = scene.createBodyFromSelection();
    scene.clearPhysicsSelection();

    Rule saved;
    saved.subjectName = body->name();
    saved.eventId = QStringLiteral("contactBegin");
    saved.targetName = body->name();
    saved.actionId = QStringLiteral("explode");
    saved.actionParams.insert(QStringLiteral("impulse"), 3.0);
    saved.actionParams.insert(QStringLiteral("radius"), 555.0);
    saved.actionParams.insert(QStringLiteral("falloff"), 77.0);
    scene.setRules({ saved });

    QString error;
    SceneSerializer::saveToFile(&scene, QStringLiteral("explode.phys"), &error);
    CanvasScene back;
    SceneSerializer::loadFromFile(&back, QStringLiteral("explode.phys"), &error);
    EXPECT_TRUE(!back.rules().isEmpty()
              && back.rules().first().actionId == QLatin1String("explode")
              && qFuzzyCompare(back.rules().first().actionParams
                                   .value(QStringLiteral("radius")).toDouble(), 555.0)) << "the action and its parameters come back";
}
