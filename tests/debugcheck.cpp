#include "MainWindow.h"
#include "CanvasScene.h"

#include <QApplication>
#include <gtest/gtest.h>
#include <QCheckBox>
#include <cstdio>


TEST(DebugCheck, Behaves)
{
    MainWindow window;
    window.show();
    QCoreApplication::processEvents();

    auto *scene = window.findChild<CanvasScene *>();
    QCheckBox *box = nullptr;
    for (QCheckBox *c : window.findChildren<QCheckBox *>())
        if (c->text() == QLatin1String("Debug View"))
            box = c;
    ASSERT_TRUE(box != nullptr) << "the toolbar has a Debug View box";

    EXPECT_TRUE(box->isChecked() == scene->debugView()) << "starts in step with the scene, not unticked";

    scene->setEditorMode(EditorMode::Physics);
    QCoreApplication::processEvents();
    EXPECT_TRUE(box->isVisible()) << "visible in Physics mode, with the transport controls";

    scene->setEditorMode(EditorMode::Edit);
    QCoreApplication::processEvents();
    EXPECT_TRUE(!box->isVisible()) << "hidden in Edit mode, where there is no run";

    scene->setEditorMode(EditorMode::Physics);
    QCoreApplication::processEvents();
    EXPECT_TRUE(box->isEnabled()) << "enabled before a run, so it can be set first";

    box->setChecked(false);
    EXPECT_TRUE(!scene->debugView()) << "unticking turns the scene's debug drawing off";
    box->setChecked(true);
    EXPECT_TRUE(scene->debugView()) << "ticking turns it back on";

    window.close();
}
