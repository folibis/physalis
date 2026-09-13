#include "CanvasScene.h"
#include "PhysicsBody.h"
#include "PropertyPane/PhysicsPropertyPane.h"
#include "RectangleItem.h"

#include <QSet>
#include <gtest/gtest.h>

namespace {

QSet<QString> keysFor(const QString &engineName)
{
    static CanvasScene *scene = nullptr;
    static PhysicsPropertyPane *pane = nullptr;
    if (!scene) {
        scene = new CanvasScene;
        RectangleItem *rectangle = scene->addRectangle(QPointF(0, 0));
        scene->setEditorMode(EditorMode::Physics);
        scene->selectForPhysics(rectangle);
        scene->createBodyFromSelection();
        scene->selectForPhysics(rectangle);
        pane = new PhysicsPropertyPane;
        pane->attach(scene);
    }
    scene->setSimulationEngineName(engineName);

    QSet<QString> keys;
    for (const PropertyRow &row : pane->rows(EditorMode::Physics)) {
        if (!row.key.isEmpty())
            keys.insert(row.key);
    }
    return keys;
}

} // namespace

// The property table is the engine's to fill: a setting the loaded engine has
// no idea of is not offered. Box2D has bullets, rolling resistance and a sleep
// speed per body; Chipmunk has none of the three, and rows for them would have
// been edited and then ignored.
TEST(EngineRows, TableOffersOnlyWhatTheEngineHonours)
{
    const QSet<QString> box2d = keysFor(QStringLiteral("Box2D"));
    const QSet<QString> chipmunk = keysFor(QStringLiteral("Chipmunk2D"));

    ASSERT_FALSE(box2d.isEmpty()) << "the table has rows at all";
    for (const char *shared : { "density", "friction", "restitution", "gravityScale",
                                   "isSensor", "enableContactEvents" }) {
        EXPECT_TRUE(box2d.contains(QString::fromLatin1(shared)))
            << "Box2D offers " << shared;
        EXPECT_TRUE(chipmunk.contains(QString::fromLatin1(shared)))
            << "and so does Chipmunk: " << shared;
    }

    for (const char *box2dOnly : { "isBullet", "sleepThreshold", "rollingResistance" }) {
        EXPECT_TRUE(box2d.contains(QString::fromLatin1(box2dOnly)))
            << "Box2D has " << box2dOnly;
        EXPECT_FALSE(chipmunk.contains(QString::fromLatin1(box2dOnly)))
            << "Chipmunk would ignore " << box2dOnly << ", so it is not shown";
    }

    // Every row comes from the engine, so an engine that is not there leaves
    // the table with nothing physical in it at all.
    EXPECT_TRUE(keysFor(QStringLiteral("NoSuchEngine")).isEmpty())
        << "no engine, no physics rows";
}
