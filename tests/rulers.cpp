// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ruslan Muhlinin. See LICENSE.
#include "CanvasScene.h"
#include "MainWindow.h"
#include "OptionsDialog.h"
#include "RulerWidget.h"
#include "Units.h"

#include <QSignalSpy>
#include <gtest/gtest.h>

// A scene coordinate is only a real distance through the scene's own scale,
// and that conversion lives in one place so the rulers and anything else that
// has to quote a distance cannot disagree about it.
TEST(Rulers, SceneUnitsBecomeARealDistanceThroughTheScale)
{
    EXPECT_DOUBLE_EQ(Units::fromScene(425.0, Units::Unit::Scene, 100.0), 425.0)
        << "scene units went through the scale";
    EXPECT_DOUBLE_EQ(Units::fromScene(425.0, Units::Unit::Metres, 100.0), 4.25);
    EXPECT_DOUBLE_EQ(Units::fromScene(425.0, Units::Unit::Centimetres, 100.0), 425.0);
    EXPECT_DOUBLE_EQ(Units::fromScene(425.0, Units::Unit::Millimetres, 100.0), 4250.0);

    // The same drawing at a tabletop scale is a tenth of the distance.
    EXPECT_DOUBLE_EQ(Units::fromScene(425.0, Units::Unit::Metres, 1000.0), 0.425);
    EXPECT_DOUBLE_EQ(Units::fromScene(4250.0, Units::Unit::Kilometres, 100.0), 0.0425);

    // A number is written to what its step is worth and no further: quoting
    // more digits than the ticks are apart claims a precision there is none of.
    EXPECT_EQ(Units::format(425.0, Units::Unit::Metres, 100.0, 1.0).toStdString(), "4");
    EXPECT_EQ(Units::format(425.0, Units::Unit::Metres, 100.0, 0.25).toStdString(), "4.25");
    EXPECT_EQ(Units::format(2000.0, Units::Unit::Centimetres, 100.0, 100.0).toStdString(), "2000");
    // Rounding to nothing must not come out as a negative zero.
    EXPECT_EQ(Units::format(-0.2, Units::Unit::Metres, 100.0, 1.0).toStdString(), "0");

    // The tick spacing is a round number in whatever unit is shown; "every
    // 23.4 cm" is a step nobody can read a position off.
    EXPECT_DOUBLE_EQ(Units::niceStep(0.3), 0.5);
    EXPECT_DOUBLE_EQ(Units::niceStep(1.0), 1.0);
    EXPECT_DOUBLE_EQ(Units::niceStep(3.0), 5.0);
    EXPECT_DOUBLE_EQ(Units::niceStep(64.0), 100.0);

    // A key the build has never heard of is somebody else's settings file,
    // not an error: it falls back rather than refusing to open.
    EXPECT_EQ(Units::fromKey(QStringLiteral("m")), Units::Unit::Metres);
    EXPECT_EQ(Units::fromKey(QStringLiteral("furlongs")), Units::Unit::Scene);
    for (const Units::Entry &entry : Units::all())
        EXPECT_EQ(Units::fromKey(Units::key(entry.unit)), entry.unit) << "a unit lost its key";
}

// Both edges of one canvas show one unit, the choice outlives the window, and
// either ruler's own menu is a way of making it.
TEST(Rulers, TheUnitIsOneChoiceForBothEdgesAndIsRemembered)
{
    MainWindow window;
    auto *top = window.findChild<RulerWidget *>(QStringLiteral("topRuler"));
    auto *left = window.findChild<RulerWidget *>(QStringLiteral("leftRuler"));
    ASSERT_NE(top, nullptr);
    ASSERT_NE(left, nullptr);
    EXPECT_EQ(OptionsDialog::Settings {}.rulerUnit, Units::Unit::Scene)
        << "a ruler stopped starting in scene units, which is what it has always shown";

    // Said outright rather than assumed: picking on a ruler writes the choice
    // down, so this test's own settings file carries whatever it ran with last.
    OptionsDialog::Settings s = window.settingsSnapshotForTest();
    s.rulerUnit = Units::Unit::Scene;
    window.applySettingsForTest(s);
    ASSERT_EQ(top->unit(), Units::Unit::Scene);
    EXPECT_EQ(window.settingsSnapshotForTest().rulerUnit, Units::Unit::Scene)
        << "the snapshot does not read the unit back off the ruler";

    s.rulerUnit = Units::Unit::Centimetres;
    window.applySettingsForTest(s);
    EXPECT_EQ(top->unit(), Units::Unit::Centimetres);
    EXPECT_EQ(left->unit(), Units::Unit::Centimetres) << "the two edges disagree";
    EXPECT_EQ(window.settingsSnapshotForTest().rulerUnit, Units::Unit::Centimetres);

    // What a ruler's own menu does. It asks rather than deciding, because the
    // window owns a setting that both of them share.
    QSignalSpy picked(left, &RulerWidget::unitPicked);
    emit left->unitPicked(Units::Unit::Metres);
    EXPECT_EQ(picked.count(), 1);
    EXPECT_EQ(top->unit(), Units::Unit::Metres) << "picking on one ruler left the other behind";
    EXPECT_EQ(left->unit(), Units::Unit::Metres);
}
