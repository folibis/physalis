#include "MainWindow.h"
#include "CanvasScene.h"
#include "SceneFixtures.h"
#include "SceneSerializer.h"

#include <QAction>
#include <QApplication>
#include <gtest/gtest.h>
#include <QFile>
#include <QKeySequence>
#include <QDateTime>
#include <QFileInfo>
#include <QtTest/QTest>
#include <QtTest/QSignalSpy>
#include <cstdio>

static const char *kScratch = "";

static void settle()
{
    for (int i = 0; i < 25; ++i)
        QCoreApplication::processEvents();
}

TEST(NewSave, Behaves)
{
    MainWindow window;
    window.resize(1200, 800);
    window.show();
    settle();

    auto *scene = window.findChild<CanvasScene *>();
    QString error;
    Fixtures::buildCart(scene);
    scene->addWatch({ QStringLiteral("body_1"), QStringLiteral("velocityY"),
                      QStringLiteral("Velocity Y") });
    settle();

    window.findChild<QAction *>(QStringLiteral("actionNewScene"))->trigger();
    settle();

    EXPECT_TRUE(scene->shapes().isEmpty()) << "no shapes" << " -- " << (QStringLiteral("%1 left").arg(scene->shapes().size())).toStdString();
    EXPECT_TRUE(scene->bodies().isEmpty()) << "no bodies" << " -- " << (QStringLiteral("%1 left").arg(scene->bodies().size())).toStdString();
    EXPECT_TRUE(scene->joints().isEmpty()) << "no joints" << " -- " << (QStringLiteral("%1 left").arg(scene->joints().size())).toStdString();
    EXPECT_TRUE(scene->rules().isEmpty()) << "no rules" << " -- " << (QStringLiteral("%1 left").arg(scene->rules().size())).toStdString();
    EXPECT_TRUE(scene->watches().isEmpty()) << "no logged properties" << " -- " << (QStringLiteral("%1 left").arg(scene->watches().size())).toStdString();

    auto *save = window.findChild<QAction *>(QStringLiteral("actionSaveScene"));
    EXPECT_TRUE(save != nullptr) << "the Save action exists";
    EXPECT_TRUE(save && save->shortcut() == QKeySequence(QStringLiteral("Ctrl+S"))) << "and is on Ctrl+S" << " -- " << (save ? save->shortcut().toString() : QString()).toStdString();
    EXPECT_TRUE(save && save->isEnabled()) << "and is enabled";

    const QString path = QString::fromLatin1(kScratch) + QStringLiteral("titletest.phys");
    QFile::remove(path);
    SceneSerializer::saveToFile(scene, path, &error);
    window.openSceneForTest(path);
    settle();
    const QString saved = window.windowTitle();
    EXPECT_TRUE(saved.contains(QStringLiteral("titletest"))) << "shows the file name" << " -- " << (saved).toStdString();
    EXPECT_TRUE(!saved.contains(QLatin1Char('*'))) << "with no star when saved" << " -- " << (saved).toStdString();

    scene->addRectangle(QPointF(0, 0));
    scene->notifyEdit(QStringLiteral("Add rectangle"));
    settle();
    const QString dirty = window.windowTitle();
    EXPECT_TRUE(dirty.contains(QLatin1Char('*'))) << "gains a star once edited" << " -- " << (dirty).toStdString();

    // A window shortcut only fires while its window is the active one, and a
    // test launched by CTest does not get the foreground for free.
    window.activateWindow();
    window.raise();
    ASSERT_TRUE(QTest::qWaitForWindowActive(&window))
        << "the window has to be active for a shortcut to resolve";

    // The real keystroke: an action can carry Ctrl+S and still never see it
    // if something up the chain eats the key.
    const QDateTime beforeKey = QFileInfo(path).lastModified();
    QTest::qWait(1100);
    QTest::keyClick(&window, Qt::Key_S, Qt::ControlModifier);
    settle();
    EXPECT_TRUE(QFileInfo(path).lastModified() > beforeKey) << "Ctrl+S actually reaches the action" << " -- " << (QFileInfo(path).lastModified().toString(QStringLiteral("HH:mm:ss"))).toStdString();
    EXPECT_TRUE(!window.windowTitle().contains(QLatin1Char('*'))) << "and clears the star" << " -- " << (window.windowTitle()).toStdString();

    scene->addRectangle(QPointF(40, 40));
    scene->notifyEdit(QStringLiteral("Add another"));
    settle();
    window.saveSceneForTest();
    settle();
    const QString resaved = window.windowTitle();
    EXPECT_TRUE(!resaved.contains(QLatin1Char('*'))) << "loses the star again once saved" << " -- " << (resaved).toStdString();

    window.close();
}
