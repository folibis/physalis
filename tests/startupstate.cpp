#include "CanvasScene.h"
#include "MainWindow.h"
#include "SimulationController.h"
#include "ViewLayersCombo.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QSettings>
#include <QTemporaryDir>
#include <gtest/gtest.h>

// The transport controls are built with the toolbar, after the settings are
// first applied -- so what was saved for them has to be put on them a second
// time. Without that, Full Screen and the playback speed came up at their
// defaults however they were left, and only the scene remembered anything.

namespace {

void settle()
{
    for (int i = 0; i < 30; ++i)
        QCoreApplication::processEvents();
}

} // namespace

TEST(StartupState, TheTransportControlsComeUpAsTheyWereLeft)
{
    QTemporaryDir home;
    ASSERT_TRUE(home.isValid());
    const QString path = home.filePath(QStringLiteral("settings.ini"));

    {
        QSettings written(path, QSettings::IniFormat);
        written.beginGroup(QStringLiteral("Physics"));
        written.setValue(QStringLiteral("fullScreen"), true);
        written.setValue(QStringLiteral("simulationSpeed"), 2.0);
        written.setValue(QStringLiteral("runShowJoints"), false);
        written.setValue(QStringLiteral("runShowRays"), false);
        written.endGroup();
    }

    const QByteArray previous = qgetenv("PHYSALIS_SETTINGS");
    qputenv("PHYSALIS_SETTINGS", path.toLocal8Bit());

    {
        MainWindow window;
        window.show();
        settle();
        window.findChild<CanvasScene *>()->setEditorMode(EditorMode::Physics);
        settle();

        auto *scene = window.findChild<CanvasScene *>();
        auto *sim = window.findChild<SimulationController *>();

        QCheckBox *full = nullptr;
        for (QCheckBox *box : window.findChildren<QCheckBox *>())
            if (box->text() == QStringLiteral("Full Screen"))
                full = box;
        ASSERT_TRUE(full);
        EXPECT_TRUE(full->isChecked()) << "Full Screen was left on, and comes up on";

        EXPECT_DOUBLE_EQ(sim->speed(), 2.0) << "and the run plays at the speed it was left at";
        QComboBox *speed = nullptr;
        for (QComboBox *combo : window.findChildren<QComboBox *>())
            if (combo->findText(QStringLiteral("×2")) >= 0)
                speed = combo;
        ASSERT_TRUE(speed);
        EXPECT_DOUBLE_EQ(speed->currentData().toDouble(), 2.0)
            << "with the chooser showing it, not ×1";

        auto *layers = window.findChild<ViewLayersCombo *>();
        ASSERT_TRUE(layers);
        EXPECT_FALSE(scene->runLayer(CanvasScene::RunLayer::Joints))
            << "the layers were left off and stay off";
        EXPECT_FALSE(layers->isOn(QStringLiteral("joints")))
            << "and the list says so rather than claiming everything is on";
        EXPECT_TRUE(layers->isOn(QStringLiteral("grid"))) << "while the others are untouched";

        window.close();
    }

    if (previous.isEmpty())
        qunsetenv("PHYSALIS_SETTINGS");
    else
        qputenv("PHYSALIS_SETTINGS", previous);
}
