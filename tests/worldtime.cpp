#include "CanvasScene.h"
#include "MainWindow.h"
#include "PhysicsBody.h"
#include "RectangleItem.h"
#include "Rule.h"
#include "ShapeItem.h"
#include "SimulationController.h"

#include <QApplication>
#include <gtest/gtest.h>
#include <QLabel>
#include <cstdio>

static void settle() { for (int i = 0; i < 20; ++i) QCoreApplication::processEvents(); }

TEST(WorldTime, Behaves)
{
    MainWindow window;
    window.resize(1200, 800);
    window.show();
    settle();
    auto *scene = window.findChild<CanvasScene *>();
    auto *sim = window.findChild<SimulationController *>();
    scene->setEditorMode(EditorMode::Physics);

    auto *block = new RectangleItem;
    block->setRect(QRectF(0, 0, 60, 60));
    block->setName(QStringLiteral("block"));
    scene->addItem(block);
    scene->notifyShapesChanged();
    scene->selectForPhysics(block, true);
    PhysicsBody *body = scene->createBodyFromSelection();
    scene->clearPhysicsSelection();

    sim->start();
    for (int f = 0; f < 120; ++f)
        sim->stepFrame();
    const QVariant t = sim->readValue(Rule::world(), QStringLiteral("time"));
    const QVariant n = sim->readValue(Rule::world(), QStringLiteral("frame"));
    EXPECT_TRUE(t.isValid() && t.toDouble() > 1.9 && t.toDouble() < 2.1) << "elapsed time is readable";
    EXPECT_TRUE(n.isValid() && qFuzzyCompare(n.toDouble(), 120.0)) << "frame count is readable";
    sim->stop();

    sim->start();
    for (int f = 0; f < 10; ++f) sim->stepFrame();
    const QVariant again = sim->readValue(Rule::world(), QStringLiteral("frame"));
    EXPECT_TRUE(qFuzzyCompare(again.toDouble(), 10.0)) << "a second run starts from zero" << " -- " << (QStringLiteral("frame %1").arg(again.toDouble())).toStdString();
    sim->stop();

    Rule after;
    after.subjectName = Rule::world();
    after.conditionKey = QStringLiteral("time");
    after.compare = Rule::Compare::Greater;
    after.conditionValue = 1.0;                 // one second in
    after.targetName = body->name();
    after.propertyKey = QStringLiteral("gravityScale");
    after.op = Rule::Op::Set;
    after.value = 0.0;
    after.once = true;
    scene->setRules({ after });

    sim->start();
    QVariant early, late;
    for (int f = 1; f <= 120; ++f) {
        sim->stepFrame();
        if (f == 30)
            early = sim->readValue(body->name(), QStringLiteral("gravityScale"));
    }
    late = sim->readValue(body->name(), QStringLiteral("gravityScale"));
    sim->stop();
    settle();
    EXPECT_TRUE(early.isValid() && early.toDouble() > 0.5) << "it has not fired at half a second" << " -- " << (QStringLiteral("gravityScale %1").arg(early.toDouble())).toStdString();
    EXPECT_TRUE(late.isValid() && qFuzzyIsNull(late.toDouble())) << "and has fired by two seconds" << " -- " << (QStringLiteral("gravityScale %1").arg(late.toDouble())).toStdString();

    scene->clearWatches();
    scene->addWatch({ Rule::world(), QStringLiteral("time"), QStringLiteral("Elapsed Time (s)") });
    scene->addWatch({ Rule::world(), QStringLiteral("frame"), QStringLiteral("Frame") });
    sim->start();
    for (int f = 0; f < 45; ++f) { sim->stepFrame(); QCoreApplication::processEvents(); }
    auto *overlay = window.findChild<QLabel *>(QStringLiteral("LogOverlay"));
    const QString shown = overlay ? overlay->text() : QString();
    EXPECT_TRUE(overlay && overlay->isVisible()
              && !shown.contains(QStringLiteral("--"))) << "the log shows them";
    sim->stop();

    window.close();
}
