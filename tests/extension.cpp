#include "CanvasScene.h"
#include "SceneFixtures.h"
#include "MainWindow.h"
#include "SceneSerializer.h"

#include <QApplication>
#include <gtest/gtest.h>
#include <QFile>
#include <QFileInfo>
#include <cstdio>


TEST(Extension, Behaves)
{
    MainWindow window;
    window.show();
    for (int i = 0; i < 25; ++i)
        QCoreApplication::processEvents();
    auto *scene = window.findChild<CanvasScene *>();

    QString error;
    // Written here, so the test depends on nothing it did not make.
    {
        CanvasScene source;
        Fixtures::buildCart(&source);
        SceneSerializer::saveToFile(&source, QStringLiteral("newformat.phys"), &error);
        SceneSerializer::saveToFile(&source, QStringLiteral("legacy.scene"), &error);
    }

    EXPECT_TRUE(SceneSerializer::loadFromFile(scene, QStringLiteral("newformat.phys"), &error)) << "a .phys scene loads" << " -- " << (error).toStdString();
    const int shapes = scene->shapes().size();
    EXPECT_TRUE(shapes > 0) << "and brings its contents" << " -- " << (QStringLiteral("%1 shapes").arg(shapes)).toStdString();

    // An untouched pre-rename file, written when the extension was .scene.
    CanvasScene old;
    EXPECT_TRUE(SceneSerializer::loadFromFile(&old, QStringLiteral("legacy.scene"), &error)) << "a .scene file still parses" << " -- " << (error).toStdString();
    EXPECT_TRUE(!old.shapes().isEmpty()) << "with its shapes intact" << " -- " << (QStringLiteral("%1 shapes").arg(old.shapes().size())).toStdString();
    QFile::remove(QStringLiteral("legacy.scene"));

    // Save As appends the default suffix when the user types a bare name.
    const QString bare = QStringLiteral("suffixcheck");
    QFile::remove(bare + QStringLiteral(".phys"));
    window.saveSceneAsForTest(bare);
    for (int i = 0; i < 20; ++i)
        QCoreApplication::processEvents();
    EXPECT_TRUE(QFileInfo::exists(bare + QStringLiteral(".phys"))) << "gets .phys appended" << " -- " << (window.windowTitle()).toStdString();
    QFile::remove(bare + QStringLiteral(".phys"));

    window.close();
}
