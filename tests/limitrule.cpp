#include "CanvasScene.h"
#include "EngineRegistry.h"
#include "IPhysicsEngine.h"
#include "Joint.h"
#include "MainWindow.h"
#include "PhysicsBody.h"
#include "RectangleItem.h"
#include "Rule.h"
#include "RulesPanel.h"

#include <QApplication>
#include <QComboBox>
#include <QTabWidget>
#include <gtest/gtest.h>

// "When the joint reaches its upper limit" happens to the joint and to nobody
// else, so the rule card asks nothing further -- and the same fact is readable
// as a flag, for a rule that wants the state rather than the arrival.

using namespace physics;

namespace {

void settle()
{
    for (int i = 0; i < 30; ++i)
        QCoreApplication::processEvents();
}

void showRules(MainWindow *window)
{
    for (QTabWidget *tabs : window->findChildren<QTabWidget *>())
        for (int i = 0; i < tabs->count(); ++i)
            if (tabs->tabText(i).contains(QStringLiteral("Rule")))
                tabs->setCurrentIndex(i);
    settle();
}

// Is the card asking which other object the event happened with? That is the
// chooser a touch needs and a limit does not; the target combo further down
// lists objects too, so it is found by name rather than by its contents.
bool asksWhichObject(RulesPanel *rules)
{
    for (QComboBox *combo : rules->findChildren<QComboBox *>())
        if (combo->objectName() == QStringLiteral("conditionOther"))
            return true;
    return false;
}

} // namespace

TEST(LimitRule, ArrivingAtALimitAsksNothingFurther)
{
    MainWindow window;
    window.resize(1300, 800);
    window.show();
    settle();

    auto *scene = window.findChild<CanvasScene *>();
    scene->setEditorMode(EditorMode::Physics);

    auto *postShape = new RectangleItem;
    postShape->setRect(QRectF(0, 0, 40, 40));
    postShape->setName(QStringLiteral("post"));
    postShape->setPos(0, 0);
    auto *plateShape = new RectangleItem;
    plateShape->setRect(QRectF(0, 0, 40, 40));
    plateShape->setName(QStringLiteral("plate"));
    plateShape->setPos(200, 0);
    scene->addItem(postShape);
    scene->addItem(plateShape);
    scene->notifyShapesChanged();

    scene->selectForPhysics(postShape, true);
    PhysicsBody *post = scene->createBodyFromSelection();
    scene->clearPhysicsSelection();
    scene->selectForPhysics(plateShape, true);
    PhysicsBody *plate = scene->createBodyFromSelection();
    scene->clearPhysicsSelection();
    Joint *slider = scene->createJoint(QStringLiteral("prismatic"), post, plate, 2, {});
    ASSERT_TRUE(slider);

    // Watching the joint arrive at its upper stop.
    Rule reached;
    reached.subjectName = slider->name();
    reached.eventId = QStringLiteral("limitUpper");
    reached.targetName = slider->name();
    reached.propertyKey = QStringLiteral("enableMotor");
    reached.op = Rule::Op::Set;
    reached.value = false;
    scene->setRules({ reached });
    showRules(&window);

    auto *rules = window.findChild<RulesPanel *>();
    ASSERT_TRUE(rules);
    EXPECT_FALSE(asksWhichObject(rules))
        << "a joint arrives at its limit on its own -- there is no other object to name";

    // The same card, watching a touch instead: now the question is real.
    Rule touched = reached;
    touched.subjectName = QStringLiteral("post");
    touched.eventId = QStringLiteral("contactBegin");
    scene->setRules({ touched });
    settle();
    EXPECT_TRUE(asksWhichObject(rules))
        << "a touch happens with something, and a rule may single it out";

    window.close();
}

TEST(LimitRule, TheEngineAnswersWhetherItIsThereNow)
{
    auto engine = EngineRegistry::create(QStringLiteral("Box2D"));
    ASSERT_TRUE(engine);

    bool published = false;
    for (const JointParam &p : engine->jointReadables(QStringLiteral("prismatic")))
        published = published || p.key == QStringLiteral("atUpperLimit");
    EXPECT_TRUE(published) << "a sliding joint says whether it is against its upper bound";

    WorldDesc world;
    world.params["gravityY"] = 0.0;
    engine->createWorld(world);

    const auto block = [](const QString &name, const QPointF &at, BodyType type) {
        ShapePart part;
        part.name = name;
        part.geometry.kind = GeometryKind::Box;
        part.geometry.halfExtents = QPointF(20, 20);
        BodyDesc body;
        body.name = name;
        body.position = at;
        body.type = type;
        body.parts.append(part);
        return body;
    };
    const BodyHandle post = engine->addBody(block("post", QPointF(0, 0), BodyType::Static));
    const BodyHandle plate = engine->addBody(block("plate", QPointF(0, 0), BodyType::Dynamic));

    JointDesc slider;
    slider.typeId = QStringLiteral("prismatic");
    slider.bodyA = post;
    slider.bodyB = plate;
    slider.anchors = { QPointF(0, 0), QPointF(0, 0) };
    slider.axis = QPointF(1, 0);
    slider.params = { { "enableLimit", true }, { "lowerTranslation", 0.0 },
                      { "upperTranslation", 100.0 }, { "enableMotor", true },
                      { "motorSpeed", 300.0 }, { "maxMotorForce", 0.05 } };
    const JointHandle handle = engine->addJoint(slider);
    ASSERT_NE(handle, kInvalidJoint);

    engine->step(1.0 / 60.0);
    EXPECT_FALSE(engine->jointValue(handle, "atUpperLimit").toBool())
        << "it has only just set off";

    for (int i = 0; i < 120; ++i)
        engine->step(1.0 / 60.0);
    EXPECT_TRUE(engine->jointValue(handle, "atUpperLimit").toBool())
        << "and now it is sitting against the far stop";
    EXPECT_FALSE(engine->jointValue(handle, "atLowerLimit").toBool())
        << "which is not the near one";
}
