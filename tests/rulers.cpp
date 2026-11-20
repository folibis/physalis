// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ruslan Muhlinin. See LICENSE.
#include "CanvasScene.h"
#include "MainWindow.h"
#include "OptionsDialog.h"
#include "RectangleItem.h"
#include "RulerWidget.h"
#include "Units.h"

#include <QAction>
#include <QGraphicsSceneMouseEvent>
#include <QKeyEvent>
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

// The canvas measures in the rulers' unit, and what it measures is no part of
// the scene. The measurement is there while the button is down: the canvas
// is worked on around it, a shape dragged near one of its points is laid onto
// it, its points can be moved, and letting the button up takes it away.
TEST(Rulers, TheCanvasMeasuresADistanceAndAnAngleInTheRulersUnit)
{
    MainWindow window;
    auto *scene = window.findChild<CanvasScene *>();
    ASSERT_TRUE(scene);
    OptionsDialog::Settings s = window.settingsSnapshotForTest();
    s.rulerUnit = Units::Unit::Metres;
    s.snapToGrid = false;
    window.applySettingsForTest(s);
    scene->setPixelsPerMeter(100.0);
    scene->setCurrentScale(100.0);

    QAction *distance = nullptr;
    QAction *angle = nullptr;
    for (QAction *action : window.findChildren<QAction *>()) {
        if (action->text() == QLatin1String("Measure Distance"))
            distance = action;
        if (action->text() == QLatin1String("Measure Angle"))
            angle = action;
    }
    ASSERT_TRUE(distance && angle) << "the toolbar has no measuring tools";

    const auto mouse = [scene](QEvent::Type type, const QPointF &at) {
        QGraphicsSceneMouseEvent event(type);
        event.setScenePos(at);
        event.setButton(type == QEvent::GraphicsSceneMouseMove ? Qt::NoButton : Qt::LeftButton);
        event.setButtons(type == QEvent::GraphicsSceneMouseRelease ? Qt::NoButton : Qt::LeftButton);
        QCoreApplication::sendEvent(scene, &event);
    };
    const auto click = [&](const QPointF &at) {
        mouse(QEvent::GraphicsSceneMousePress, at);
        mouse(QEvent::GraphicsSceneMouseRelease, at);
    };

    distance->trigger();
    ASSERT_EQ(scene->measureTool(), CanvasScene::Measure::Distance);
    const int undoable = scene->items().size();
    mouse(QEvent::GraphicsSceneMousePress, QPointF(1000, 1000));
    mouse(QEvent::GraphicsSceneMouseMove, QPointF(1300, 1400));
    mouse(QEvent::GraphicsSceneMouseRelease, QPointF(1300, 1400));
    EXPECT_EQ(scene->measureText().section(QLatin1Char('\n'), 0, 0).toStdString(), "5.00 m")
        << "a 3-4-5 drag at 100 px per metre";
    EXPECT_EQ(scene->items().size(), undoable) << "a measurement put something in the scene";
    EXPECT_TRUE(distance->isChecked()) << "the button came up when the measurement was laid";

    // The canvas has its mouse back away from the measurement, and a corner
    // dragged near an end of it is laid on it.
    RectangleItem *box = scene->addRectangle(QPointF(0, 0));
    const QPointF grab = box->mapToScene(box->rect().center());
    const QPointF toEnd = QPointF(1300, 1400) - box->mapToScene(box->rect().topRight());
    mouse(QEvent::GraphicsSceneMousePress, grab);
    mouse(QEvent::GraphicsSceneMouseMove, grab + toEnd + QPointF(3, -2));
    mouse(QEvent::GraphicsSceneMouseRelease, grab + toEnd + QPointF(3, -2));
    EXPECT_EQ(box->mapToScene(box->rect().topRight()), QPointF(1300, 1400))
        << "the shape was not laid on the end of the measurement";
    ASSERT_EQ(scene->measurePoints().size(), 2) << "working on the canvas took the measurement up";

    // A point is picked up and moved, and is drawn to a corner near where it lands.
    const QPointF corner = box->mapToScene(box->rect().bottomLeft());
    mouse(QEvent::GraphicsSceneMousePress, QPointF(1002, 999));
    mouse(QEvent::GraphicsSceneMouseMove, corner + QPointF(3, -2));
    mouse(QEvent::GraphicsSceneMouseRelease, corner + QPointF(3, -2));
    EXPECT_EQ(scene->measurePoints().at(0), corner) << "the start was not moved to the corner";
    EXPECT_EQ(scene->measurePoints().at(1), QPointF(1300, 1400)) << "the other end moved too";

    // Picking the other tool puts this one away, measurement and all.
    angle->trigger();
    EXPECT_FALSE(distance->isChecked());
    ASSERT_EQ(scene->measureTool(), CanvasScene::Measure::Angle);
    click(QPointF(1000, 1000));
    click(QPointF(1100, 1000));
    click(QPointF(1000, 900));
    EXPECT_EQ(scene->measureText().toStdString(), "90.0°");

    QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QCoreApplication::sendEvent(scene, &escape);
    EXPECT_FALSE(scene->hasMeasurement()) << "Esc left the measurement down";
    EXPECT_EQ(scene->measureTool(), CanvasScene::Measure::Angle) << "one Esc put the tool away";
    QCoreApplication::sendEvent(scene, &escape);
    EXPECT_EQ(scene->measureTool(), CanvasScene::Measure::None);
    EXPECT_FALSE(angle->isChecked()) << "the toolbar still shows the tool in hand";

    // Letting the button up takes the measurement with it.
    angle->trigger();
    click(QPointF(1000, 1000));
    click(QPointF(1100, 1000));
    click(QPointF(1000, 900));
    ASSERT_TRUE(scene->hasMeasurement());
    angle->trigger();
    EXPECT_FALSE(scene->hasMeasurement()) << "the measurement outlived its button";
}
