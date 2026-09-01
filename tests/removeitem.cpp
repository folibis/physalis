#include "CanvasScene.h"
#include "ExplosionItem.h"
#include "MainWindow.h"
#include "PhysicsBody.h"
#include "RectangleItem.h"
#include "SceneFixtures.h"
#include "ShapeItem.h"

#include <QAction>
#include <QApplication>
#include <gtest/gtest.h>
#include <cstdio>

static void settle() { for (int i = 0; i < 20; ++i) QCoreApplication::processEvents(); }

TEST(RemoveItem, Behaves)
{
    MainWindow window;
    window.resize(1100, 700);
    window.show();
    settle();
    auto *scene = window.findChild<CanvasScene *>();
    Fixtures::buildCart(scene);
    scene->setEditorMode(EditorMode::Physics);
    settle();

    auto *remove = window.findChild<QAction *>(QStringLiteral("actionDissolveBody"));
    ShapeItem *chassis = nullptr;
    for (ShapeItem *s : scene->shapes())
        if (s->name() == QLatin1String("chassis"))
            chassis = s;

    scene->selectForPhysics(chassis, true);
    settle();
    EXPECT_TRUE(!scene->physicsSelection().isEmpty()) << "a body is selected";

    ExplosionItem *boom = scene->addExplosion(QPointF(0, 0));
    scene->selectExplosion(boom);
    settle();
    EXPECT_TRUE(scene->physicsSelection().isEmpty()) << "selecting an explosion clears the body" << " -- " << (QStringLiteral("%1 still selected").arg(scene->physicsSelection().size())).toStdString();
    EXPECT_TRUE(scene->selectedExplosion() == boom) << "and the explosion is the selected one";

    scene->selectForPhysics(chassis, true);
    settle();
    EXPECT_TRUE(scene->selectedExplosion() == nullptr) << "selecting a body clears the explosion";

    scene->selectExplosion(boom);
    settle();
    EXPECT_TRUE(remove && remove->isEnabled()) << "enabled for an explosion";
    const int before = scene->explosions().size();
    remove->trigger();
    settle();
    EXPECT_TRUE(scene->explosions().size() == before - 1) << "it deletes the explosion" << " -- " << (QStringLiteral("%1 -> %2").arg(before).arg(scene->explosions().size())).toStdString();

    scene->selectForPhysics(chassis, true);
    settle();
    EXPECT_TRUE(remove && remove->isEnabled()) << "enabled for a body";
    const int bodies = scene->bodies().size();
    remove->trigger();
    settle();
    EXPECT_TRUE(scene->bodies().size() == bodies - 1) << "it dissolves the body" << " -- " << (QStringLiteral("%1 -> %2").arg(bodies).arg(scene->bodies().size())).toStdString();

    scene->clearPhysicsSelection();
    scene->selectExplosion(nullptr);
    settle();
    EXPECT_TRUE(remove && !remove->isEnabled()) << "disabled with nothing selected";

    window.close();
}
