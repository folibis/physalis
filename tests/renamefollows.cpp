#include "CanvasScene.h"
#include "ExplosionItem.h"
#include "Joint.h"
#include "PhysicsBody.h"
#include "RayItem.h"
#include "RectangleItem.h"
#include "Rule.h"

#include <gtest/gtest.h>

// A name is what a rule and a row in the log have to go on, so renaming an
// object takes them with it -- whatever kind of object it is.

namespace {

struct Scene {
    CanvasScene scene;
    ShapeItem *box = nullptr;
    PhysicsBody *body = nullptr;
};

void build(Scene &s)
{
    s.scene.setSimulationEngineName(QStringLiteral("Box2D"));
    s.box = s.scene.addRectangle(QPointF(0, 0));
    s.box->setName(QStringLiteral("box"));
    s.scene.notifyShapesChanged();
    s.scene.setEditorMode(EditorMode::Physics);
    s.scene.selectForPhysics(s.box, true);
    s.body = s.scene.createBodyFromSelection();
    s.scene.clearPhysicsSelection();
}

CanvasScene::Watch watchOf(const QString &object, const QString &key)
{
    CanvasScene::Watch watch;
    watch.objectName = object;
    watch.propertyKey = key;
    watch.label = QStringLiteral("Speed");
    return watch;
}

} // namespace

TEST(RenameFollows, ALoggedShapeKeepsItsRow)
{
    Scene s;
    build(s);
    s.scene.addWatch(watchOf(QStringLiteral("box"), QStringLiteral("mass")));

    s.box->setName(QStringLiteral("crate"));

    ASSERT_EQ(s.scene.watches().size(), 1);
    EXPECT_EQ(s.scene.watches().first().objectName, QStringLiteral("crate"));
    EXPECT_TRUE(s.scene.isWatched(QStringLiteral("crate"), QStringLiteral("mass")));
    EXPECT_FALSE(s.scene.isWatched(QStringLiteral("box"), QStringLiteral("mass")));
}

TEST(RenameFollows, ALoggedBodyKeepsItsRow)
{
    Scene s;
    build(s);
    ASSERT_TRUE(s.body);
    s.scene.addWatch(watchOf(s.body->name(), QStringLiteral("speed")));

    s.body->setName(QStringLiteral("cart"));

    ASSERT_EQ(s.scene.watches().size(), 1);
    EXPECT_EQ(s.scene.watches().first().objectName, QStringLiteral("cart"));
}

TEST(RenameFollows, ALoggedRayKeepsItsRow)
{
    CanvasScene scene;
    RayItem *ray = scene.addRay(QPointF(0, 0));
    ASSERT_TRUE(ray);
    scene.addWatch(watchOf(ray->name(), QStringLiteral("hit")));

    ray->setName(QStringLiteral("tripwire"));

    ASSERT_EQ(scene.watches().size(), 1);
    EXPECT_EQ(scene.watches().first().objectName, QStringLiteral("tripwire"));
}

TEST(RenameFollows, AnExplosionKeepsTheRulesThatNameIt)
{
    CanvasScene scene;
    ExplosionItem *blast = scene.addExplosion(QPointF(0, 0));
    ASSERT_TRUE(blast);
    Rule rule;
    rule.targetName = blast->name();
    rule.actionId = QStringLiteral("explode");
    scene.setRules({ rule });

    blast->setName(QStringLiteral("charge"));

    ASSERT_EQ(scene.rules().size(), 1);
    EXPECT_EQ(scene.rules().first().targetName, QStringLiteral("charge"));
}

// Every name a rule carries, including the object a value is read from.
TEST(RenameFollows, EveryNameInARule)
{
    Scene s;
    build(s);
    ShapeItem *other = s.scene.addRectangle(QPointF(200, 0));
    other->setName(QStringLiteral("gate"));

    Rule rule;
    rule.subjectName = QStringLiteral("box");
    rule.eventId = QStringLiteral("contactBegin");
    rule.conditionValue = QStringLiteral("box");
    rule.targetName = QStringLiteral("box");
    rule.sourceObject = QStringLiteral("box");
    rule.sourceProperty = QStringLiteral("speed");
    s.scene.setRules({ rule });

    s.box->setName(QStringLiteral("crate"));

    const Rule &moved = s.scene.rules().first();
    EXPECT_EQ(moved.subjectName, QStringLiteral("crate"));
    EXPECT_EQ(moved.conditionValue.toString(), QStringLiteral("crate"));
    EXPECT_EQ(moved.targetName, QStringLiteral("crate"));
    EXPECT_EQ(moved.sourceObject, QStringLiteral("crate")) << "the object a value is read from";

    // And nothing else is touched.
    EXPECT_EQ(other->name(), QStringLiteral("gate"));
}
