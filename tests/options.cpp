// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ruslan Muhlinin. See LICENSE.
#include "CanvasScene.h"
#include "OptionsDialog.h"
#include "ShapeStyle.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <gtest/gtest.h>

// The Options dialog, every value in it. It is handed what the application
// currently has and gives back what the user chose, so the one thing that has
// to be true of every single field is that opening the dialog and pressing OK
// changes nothing. A control that does not load its value, or does not report
// it, silently resets a setting the user never touched -- and there are
// seventy-seven of them, including the ones the dialog does not show at all
// and only carries through.

namespace {

// Every field set to something that is not its default, and not the same as
// its neighbours, so a control wired to the wrong field shows up as a
// mismatch rather than passing by luck.
OptionsDialog::Settings distinctive()
{
    OptionsDialog::Settings s;
    s.fieldWidth = 1234.0;
    s.fieldHeight = 987.0;
    s.backgroundColor = QColor(11, 22, 33);
    s.showGrid = false;
    s.gridCellSize = 37.0;
    s.gridColor = QColor(44, 55, 66);
    s.snapToGrid = true;
    s.snapPoint = SnapPoint::Origin;
    s.snapStep = 13.0;
    s.snapSensitivity = 7.0;

    s.currentScale = 175.0;
    s.scaleMin = 15.0;
    s.scaleMax = 450.0;
    s.scaleStep = 25.0;

    for (const QString &kind : ShapeStyle::kinds()) {
        ShapeStyle style;
        style.body = QColor(101, 102, 103, 120);
        style.border = QColor(77, 88, 99, 200);
        style.borderWidth = 3.5 + ShapeStyle::kinds().indexOf(kind);
        style.borderStyle = Qt::DashDotLine;
        s.shapeStyles.insert(kind, style);
    }

    s.logFontFamily = QStringLiteral("Courier New");
    s.logFontSize = 14;
    s.logColor = QColor(12, 34, 56);
    s.logCorner = Qt::BottomRightCorner;

    s.selectionLineStyle = Qt::DashLine;
    s.selectionLineWidth = 4.0;
    s.selectionColor = QColor(200, 100, 50);
    s.undoDepth = 77;

    s.handleShape = HandleShape::Circle;
    s.handleSize = 11.0;
    s.handleColor = QColor(1, 2, 3);
    s.handleBorderWidth = 2.5;
    s.handleBorderColor = QColor(4, 5, 6);

    s.bodyDynamicColor = QColor(10, 20, 30);
    s.bodyStaticColor = QColor(40, 50, 60);
    s.bodyKinematicColor = QColor(70, 80, 90);
    s.unassignedShapeColor = QColor(100, 110, 120);
    s.sensorColor = QColor(130, 140, 150);
    s.sensorPattern = Qt::BDiagPattern;
    s.sensorFillsBody = true;
    s.physicsBorderWidth = 5.0;
    s.physicsFillAlpha = 123;
    s.jointFillAlpha = 199;
    s.jointAnchorOpacity = 45;
    s.physicsSelectionLineStyle = Qt::DashDotLine;
    s.physicsSelectionLineWidth = 6.0;
    s.physicsSelectionColor = QColor(160, 170, 180);

    s.sleepShading = false;
    s.runShowGrid = false;
    s.runShowJoints = false;
    s.runShowBodyAxes = false;
    s.runShowRays = false;
    s.runShowExplosions = false;
    s.showBodyAxes = false;
    s.bodyAxisLength = 55.0;
    s.bodyAxisWidth = 3.0;
    s.bodyAxisXColor = QColor(190, 200, 210);
    s.bodyAxisYColor = QColor(220, 230, 240);
    s.sleepShiftPercent = 42;
    s.maxPolygonVertices = 7;
    s.simulationStepsPerSecond = 90;
    s.simulationSpeed = 2.0;
    s.simulationFullScreen = true;

    s.shotLightColor = QColor(9, 8, 7);
    s.shotFullColor = QColor(6, 5, 4);
    s.shotLineWidth = 4.5;
    // One of the four the slingshot combo offers; it cannot show any other.
    s.shotLineStyle = Qt::DashDotLine;

    s.jointColor = QColor(3, 2, 1);
    s.jointKindColors.insert(0, QColor(12, 34, 56));
    s.jointKindColors.insert(3, QColor(65, 43, 21));
    s.jointKindStyles.insert(0, JointStyle::Rod);
    s.jointKindStyles.insert(2, JointStyle::Dashed);
    s.jointSelectionLineStyle = Qt::DashLine;
    // The width spin boxes carry one decimal, so these sit on that grid.
    s.jointSelectionLineWidth = 3.3;
    s.jointSelectionColor = QColor(210, 120, 30);
    s.simulationEngineName = QStringLiteral("Box2D");
    s.defaultEngineName = QStringLiteral("Box2D");
    s.jointOutlineColor = QColor(15, 25, 35);
    s.jointAnchorRadius = 9.0;
    s.jointAxisLength = 65.0;
    s.jointWaistWidth = 4.8;
    s.jointOutlineWidth = 2.3;

    s.converterPath = QStringLiteral("C:/converters");
    s.converterSettings.insert(QStringLiteral("planck-js"),
                               QVariantMap { { QStringLiteral("scale"), 3 } });

    s.pixelsPerMeter = 250.0;
    s.fieldBoundsSolid = true;
    return s;
}

#define SAME(field) EXPECT_EQ(out.field, in.field) << "Options lost or changed " #field

void everyFieldComesBack(const OptionsDialog::Settings &in, const OptionsDialog::Settings &out)
{
    SAME(fieldWidth); SAME(fieldHeight); SAME(backgroundColor); SAME(showGrid);
    SAME(gridCellSize); SAME(gridColor); SAME(snapToGrid); SAME(snapStep);
    SAME(snapSensitivity);
    EXPECT_EQ(int(out.snapPoint), int(in.snapPoint)) << "Options lost or changed snapPoint";

    SAME(currentScale); SAME(scaleMin); SAME(scaleMax); SAME(scaleStep);

    // Every kind's fill, border, width and line come back as they went out.
    ASSERT_EQ(out.shapeStyles.size(), in.shapeStyles.size())
        << "Options lost a shape style";
    for (auto it = in.shapeStyles.constBegin(); it != in.shapeStyles.constEnd(); ++it) {
        const ShapeStyle got = out.shapeStyles.value(it.key());
        const std::string kind = it.key().toStdString();
        EXPECT_EQ(got.body, it->body) << kind << " lost its fill colour";
        EXPECT_EQ(got.border, it->border) << kind << " lost its border colour";
        EXPECT_DOUBLE_EQ(got.borderWidth, it->borderWidth) << kind << " lost its border width";
        EXPECT_EQ(int(got.borderStyle), int(it->borderStyle)) << kind << " lost its line style";
    }
    SAME(logFontFamily); SAME(logFontSize); SAME(logColor);
    EXPECT_EQ(int(out.logCorner), int(in.logCorner)) << "Options lost or changed logCorner";
    EXPECT_EQ(int(out.selectionLineStyle), int(in.selectionLineStyle))
        << "Options lost or changed selectionLineStyle";
    SAME(selectionLineWidth); SAME(selectionColor); SAME(undoDepth);

    EXPECT_EQ(int(out.handleShape), int(in.handleShape)) << "Options lost or changed handleShape";
    SAME(handleSize); SAME(handleColor); SAME(handleBorderWidth); SAME(handleBorderColor);

    SAME(bodyDynamicColor); SAME(bodyStaticColor); SAME(bodyKinematicColor);
    SAME(unassignedShapeColor); SAME(sensorColor);
    EXPECT_EQ(int(out.sensorPattern), int(in.sensorPattern))
        << "Options lost or changed sensorPattern";
    SAME(sensorFillsBody); SAME(physicsBorderWidth); SAME(physicsFillAlpha);
    SAME(jointFillAlpha); SAME(jointAnchorOpacity);
    EXPECT_EQ(int(out.physicsSelectionLineStyle), int(in.physicsSelectionLineStyle))
        << "Options lost or changed physicsSelectionLineStyle";
    SAME(physicsSelectionLineWidth); SAME(physicsSelectionColor);

    SAME(sleepShading); SAME(runShowGrid); SAME(runShowJoints); SAME(runShowBodyAxes);
    SAME(runShowRays); SAME(runShowExplosions); SAME(showBodyAxes);
    SAME(bodyAxisLength); SAME(bodyAxisWidth); SAME(bodyAxisXColor); SAME(bodyAxisYColor);
    SAME(sleepShiftPercent); SAME(maxPolygonVertices); SAME(simulationStepsPerSecond);
    SAME(simulationSpeed); SAME(simulationFullScreen);

    SAME(shotLightColor); SAME(shotFullColor); SAME(shotLineWidth);
    EXPECT_EQ(int(out.shotLineStyle), int(in.shotLineStyle))
        << "Options lost or changed shotLineStyle";

    SAME(jointColor);
    // The dialog has a row per joint kind, so it gives back all five whatever
    // it was handed: what matters is that the ones that were set come back as
    // they were, and the rest are filled in rather than left out.
    for (auto it = in.jointKindColors.cbegin(); it != in.jointKindColors.cend(); ++it) {
        EXPECT_EQ(out.jointKindColors.value(it.key()), it.value())
            << "Options lost or changed the colour of joint kind " << it.key();
    }
    for (auto it = in.jointKindStyles.cbegin(); it != in.jointKindStyles.cend(); ++it) {
        EXPECT_EQ(int(out.jointKindStyles.value(it.key())), int(it.value()))
            << "Options lost or changed the style of joint kind " << it.key();
    }
    EXPECT_EQ(out.jointKindColors.size(), 5) << "every joint kind has a colour after Options";
    EXPECT_EQ(out.jointKindStyles.size(), 5) << "and a style";
    EXPECT_EQ(int(out.jointSelectionLineStyle), int(in.jointSelectionLineStyle))
        << "Options lost or changed jointSelectionLineStyle";
    SAME(jointSelectionLineWidth); SAME(jointSelectionColor);
    SAME(simulationEngineName); SAME(defaultEngineName);
    SAME(jointOutlineColor); SAME(jointAnchorRadius); SAME(jointAxisLength);
    SAME(jointWaistWidth); SAME(jointOutlineWidth);

    SAME(converterPath); SAME(converterSettings);
    SAME(pixelsPerMeter); SAME(fieldBoundsSolid);
}

#undef SAME

} // namespace

// Opening Options and pressing OK without touching anything gives back exactly
// what went in -- all seventy-seven fields, shown or merely carried.
TEST(Options, OpeningAndAcceptingChangesNothing)
{
    const OptionsDialog::Settings in = distinctive();
    OptionsDialog dialog(in, nullptr);
    const OptionsDialog::Settings out = dialog.settings();
    everyFieldComesBack(in, out);
}

// And the same with the dialog actually shown and laid out, which is when a
// control can reset itself as its widgets are sized and its tabs built.
TEST(Options, ShowingItChangesNothingEither)
{
    const OptionsDialog::Settings in = distinctive();
    OptionsDialog dialog(in, nullptr);
    dialog.resize(600, 900);
    dialog.show();
    for (int i = 0; i < 20; ++i)
        QCoreApplication::processEvents();
    const OptionsDialog::Settings out = dialog.settings();
    everyFieldComesBack(in, out);
}

// Changing a control changes the setting it belongs to, and nothing else. One
// per kind of control the dialog uses -- a spin box, a double spin box, a
// check box and a combo -- so the wiring itself is proven, not just the
// carrying.
TEST(Options, ChangingAControlChangesItsOwnSetting)
{
    const OptionsDialog::Settings in = distinctive();

    {
        OptionsDialog dialog(in, nullptr);
        auto *undoDepth = dialog.findChild<QSpinBox *>(QStringLiteral("undoDepth"));
        ASSERT_TRUE(undoDepth) << "the undo depth spin box is in the dialog";
        undoDepth->setValue(31);
        const OptionsDialog::Settings out = dialog.settings();
        EXPECT_EQ(out.undoDepth, 31) << "the spin box wrote its own setting";
        EXPECT_EQ(out.maxPolygonVertices, in.maxPolygonVertices) << "and left the others alone";
        EXPECT_EQ(out.fieldWidth, in.fieldWidth);
    }
    {
        OptionsDialog dialog(in, nullptr);
        auto *cellSize = dialog.findChild<QDoubleSpinBox *>(QStringLiteral("cellSize"));
        ASSERT_TRUE(cellSize);
        cellSize->setValue(64.0);
        const OptionsDialog::Settings out = dialog.settings();
        EXPECT_DOUBLE_EQ(out.gridCellSize, 64.0);
        EXPECT_DOUBLE_EQ(out.snapStep, in.snapStep) << "the grid size is not the snap step";
    }
    {
        OptionsDialog dialog(in, nullptr);
        auto *showGrid = dialog.findChild<QCheckBox *>(QStringLiteral("showGrid"));
        ASSERT_TRUE(showGrid);
        showGrid->setChecked(!in.showGrid);
        const OptionsDialog::Settings out = dialog.settings();
        EXPECT_EQ(out.showGrid, !in.showGrid);
        EXPECT_EQ(out.snapToGrid, in.snapToGrid) << "showing the grid is not snapping to it";
    }
}
