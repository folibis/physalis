#include "MainWindow.h"
#include "CanvasScene.h"
#include "SceneFixtures.h"
#include "Joint.h"
#include "PhysicsBody.h"
#include "SceneSerializer.h"
#include "SimulationController.h"

#include <QApplication>
#include <gtest/gtest.h>
#include <QLabel>
#include <cstdio>

static const char *kScratch = "";


TEST(LogOverlay, Behaves)
{
    MainWindow window;
    window.resize(1400, 850);
    window.show();
    for (int i = 0; i < 30; ++i)
        QCoreApplication::processEvents();

    auto *scene = window.findChild<CanvasScene *>();
    auto *sim = window.findChild<SimulationController *>();
    QString error;
    Fixtures::buildCart(scene);
    scene->setEditorMode(EditorMode::Physics);
    // The scene may carry watches of its own; this test is about the mechanism.
    scene->clearWatches();
    for (int i = 0; i < 20; ++i)
        QCoreApplication::processEvents();

    auto *overlay = window.findChild<QLabel *>(QStringLiteral("LogOverlay"));
    EXPECT_TRUE(overlay && !overlay->isVisible()) << "hidden when nothing is logged";

    CanvasScene::Watch a { QStringLiteral("body_1"), QStringLiteral("velocityY"),
                           QStringLiteral("Velocity Y (m/s)") };
    CanvasScene::Watch b { QStringLiteral("wheel_2"), QStringLiteral("motorSpeed"),
                           QStringLiteral("Motor Speed (deg/s)") };
    scene->addWatch(a);
    scene->addWatch(b);
    scene->addWatch(a);   // adding twice must not duplicate
    for (int i = 0; i < 20; ++i)
        QCoreApplication::processEvents();

    EXPECT_TRUE(scene->watches().size() == 2) << "two entries, and no duplicate" << " -- " << (QStringLiteral("%1").arg(scene->watches().size())).toStdString();
    EXPECT_TRUE(!overlay->isVisible()) << "still hidden while nothing is running";

    sim->start();
    for (int frame = 0; frame < 60; ++frame) {
        sim->stepFrame();
        QCoreApplication::processEvents();
    }
    EXPECT_TRUE(overlay->isVisible()) << "appears once a run starts";
    EXPECT_TRUE(overlay->text().count(QChar::LineFeed) == 1) << "one line per entry" << " -- " << (overlay->text().replace(QChar::LineFeed, QLatin1String(" | "))).toStdString();
    EXPECT_TRUE(overlay->pos().x() < 20 && overlay->pos().y() < 20) << "it is in the top-left corner";
    EXPECT_TRUE(!overlay->text().contains(QLatin1String("--"))) << "and shows real values, not placeholders";
    window.grab().save(QString::fromLatin1(kScratch) + QStringLiteral("logrunning.png"));

    sim->stop();
    for (int i = 0; i < 20; ++i)
        QCoreApplication::processEvents();
    EXPECT_TRUE(!overlay->isVisible()) << "hidden again once the run stops";

    scene->removeWatch(QStringLiteral("body_1"), QStringLiteral("velocityY"));
    EXPECT_TRUE(scene->watches().size() == 1) << "removing one leaves the other";
    scene->clearWatches();
    EXPECT_TRUE(scene->watches().isEmpty()) << "clearing empties the log";

    // And the log travels with the scene file.
    scene->addWatch(a);
    scene->addWatch(b);
    const QString path = QString::fromLatin1(kScratch) + QStringLiteral("logsave.phys");
    SceneSerializer::saveToFile(scene, path, &error);
    CanvasScene reloaded;
    SceneSerializer::loadFromFile(&reloaded, path, &error);
    EXPECT_TRUE(reloaded.watches().size() == 2) << "the log is saved with the scene" << " -- " << (QStringLiteral("%1 after reload").arg(reloaded.watches().size())).toStdString();

    window.close();
}
