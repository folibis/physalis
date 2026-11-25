// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ruslan Muhlinin. See LICENSE.
#include "CanvasScene.h"
#include "MainWindow.h"
#include "OptionsDialog.h"
#include "PropertyPanel.h"
#include "PolygonItem.h"
#include "RectangleItem.h"
#include "RulerWidget.h"
#include "UndoStack.h"
#include "Units.h"

#include <QAction>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QPainter>
#include <QImage>
#include <QTableWidget>
#include <QGraphicsSceneMouseEvent>
#include <QKeyEvent>
#include <QSignalSpy>
#include <cmath>
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

// A laid measurement is typed into from the property table: the ruler is set
// to a width and then turned about A without the width changing, and the
// protractor's angle swings C about A. None of it is an edit to the scene.
TEST(Rulers, AMeasurementIsSetFromThePropertyTable)
{
    MainWindow window;
    auto *scene = window.findChild<CanvasScene *>();
    auto *panel = window.findChild<PropertyPanel *>();
    auto *undo = window.findChild<UndoStack *>();
    ASSERT_TRUE(scene && panel && undo);
    OptionsDialog::Settings s = window.settingsSnapshotForTest();
    s.rulerUnit = Units::Unit::Metres;
    s.snapToGrid = false;
    window.applySettingsForTest(s);
    scene->setPixelsPerMeter(100.0);
    scene->setCurrentScale(100.0);
    scene->setEditorMode(EditorMode::Edit);

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
    // The panel switches to the measurement queued, behind the change.
    const auto settle = [] {
        QCoreApplication::processEvents();
        QCoreApplication::sendPostedEvents();
    };
    const auto box = [panel](const char *tip) -> QDoubleSpinBox * {
        for (QDoubleSpinBox *spin : panel->findChildren<QDoubleSpinBox *>()) {
            if (spin->toolTip().startsWith(QString::fromLatin1(tip)))
                return spin;
        }
        return nullptr;
    };
    const auto labels = [panel] {
        QStringList found;
        for (const PropertyRow &row : panel->rowsForTest())
            found << row.label;
        return found;
    };

    undo->markClean();
    distance->trigger();
    mouse(QEvent::GraphicsSceneMousePress, QPointF(1000, 1000));
    mouse(QEvent::GraphicsSceneMouseMove, QPointF(1300, 1400));
    mouse(QEvent::GraphicsSceneMouseRelease, QPointF(1300, 1400));
    settle();
    EXPECT_EQ(labels(), QStringList({ "Width (m)", "A X", "A Y", "B X", "B Y", "Angle (\u00B0)" }))
        << "the ruler's table is not what it was asked to be";

    QDoubleSpinBox *width = box("How far B is from A");
    QDoubleSpinBox *turn = box("Which way B lies from A");
    ASSERT_TRUE(width && turn) << "the laid ruler has no table";
    EXPECT_NEAR(width->value(), 5.0, 1e-9) << "a 3-4-5 ruler at 100 px per metre";

    // Set to a width, then turned: the width is what it was set to.
    width->setValue(2.0);
    EXPECT_NEAR(QLineF(scene->measurePoints().at(0), scene->measurePoints().at(1)).length(), 200.0, 1e-9);
    turn->setValue(90.0);
    const QPointF a = scene->measurePoints().at(0);
    const QPointF b = scene->measurePoints().at(1);
    EXPECT_EQ(a, QPointF(1000, 1000)) << "turning the ruler moved A";
    EXPECT_NEAR(b.x(), 1000.0, 1e-9) << "90 degrees is not straight up";
    EXPECT_NEAR(b.y(), 800.0, 1e-9) << "90 degrees is not straight up, or the width changed";
    EXPECT_NEAR(width->value(), 2.0, 1e-9) << "turning the ruler changed its width";

    // Every row follows whichever one was typed into: B moved by its
    // coordinates changes the width and the angle, and the width moves B.
    const auto rowBox = [panel](const QString &label) -> QDoubleSpinBox * {
        for (QTableWidget *table : panel->findChildren<QTableWidget *>()) {
            for (int r = 0; r < table->rowCount(); ++r) {
                QWidget *name = table->cellWidget(r, 0);
                QLabel *text = name ? name->findChild<QLabel *>() : nullptr;
                QWidget *editor = table->cellWidget(r, 1);
                if (!text || text->text() != label || !editor)
                    continue;
                if (auto *spin = qobject_cast<QDoubleSpinBox *>(editor))
                    return spin;
                return editor->findChild<QDoubleSpinBox *>();
            }
        }
        return nullptr;
    };
    QDoubleSpinBox *bx = rowBox(QStringLiteral("B X"));
    QDoubleSpinBox *by = rowBox(QStringLiteral("B Y"));
    ASSERT_TRUE(bx && by);
    settle();
    EXPECT_NEAR(bx->value(), 1000.0, 1e-9) << "turning the ruler moved B but its row did not follow";
    EXPECT_NEAR(by->value(), 800.0, 1e-9) << "turning the ruler moved B but its row did not follow";
    bx->setValue(1300.0);
    by->setValue(1000.0);
    settle();
    EXPECT_NEAR(width->value(), 3.0, 1e-9) << "moving B left the width as it was";
    EXPECT_NEAR(turn->value(), 0.0, 1e-9) << "moving B left the angle as it was";
    width->setValue(1.0);
    settle();
    EXPECT_NEAR(bx->value(), 1100.0, 1e-9) << "the width moved B but its row did not follow";
    EXPECT_NEAR(by->value(), 1000.0, 1e-9);
    turn->setValue(90.0);
    settle();
    EXPECT_NEAR(by->value(), 900.0, 1e-9) << "the angle moved B but its row did not follow";
    width->setValue(2.0);
    settle();
    ASSERT_EQ(scene->measurePoints().at(1), b) << "the ruler did not come back where it was";
    EXPECT_TRUE(undo->isClean()) << "typing into a measurement was taken as an edit to the scene";

    // Moved on the canvas, the table follows.
    mouse(QEvent::GraphicsSceneMousePress, b);
    mouse(QEvent::GraphicsSceneMouseMove, QPointF(1000, 1100));
    mouse(QEvent::GraphicsSceneMouseRelease, QPointF(1000, 1100));
    settle();
    width = box("How far B is from A");
    ASSERT_TRUE(width);
    EXPECT_NEAR(width->value(), 1.0, 1e-9) << "the table did not follow B being dragged";

    // With a shape picked the table is the shape's, and a click on the ruler's
    // line takes it back without moving either end.
    // Drawn with handles while it is the selection, and plainly once it is not.
    const auto drawn = [scene] {
        QImage image(200, 300, QImage::Format_ARGB32);
        image.fill(Qt::white);
        QPainter painter(&image);
        scene->render(&painter, QRectF(), QRectF(900, 950, 200, 300));
        return image;
    };
    EXPECT_TRUE(scene->isMeasurementSelected()) << "a ruler just laid is not the selection";
    const QImage asSelected = drawn();

    RectangleItem *shape = scene->addRectangle(QPointF(0, 0));
    scene->selectShape(shape);
    settle();
    EXPECT_FALSE(scene->isMeasurementSelected()) << "the ruler stayed selected beside a shape";
    EXPECT_TRUE(drawn() != asSelected) << "the ruler looks selected with a shape picked";
    EXPECT_TRUE(box("How far B is from A") == nullptr) << "a picked shape did not get the table";
    click(QPointF(1003, 1050));
    settle();
    EXPECT_TRUE(scene->isMeasurementSelected()) << "clicking the ruler did not select it";
    EXPECT_TRUE(drawn() == asSelected) << "the ruler picked again is not drawn selected";
    EXPECT_EQ(scene->activeItem(), nullptr) << "clicking the ruler left the shape picked";
    EXPECT_TRUE(box("How far B is from A")) << "clicking the ruler did not pick it";

    // A click on the empty field lets it go, and a click on it picks it again.
    click(QPointF(500, 500));
    settle();
    EXPECT_FALSE(scene->isMeasurementSelected()) << "clicking the field left the ruler selected";
    EXPECT_TRUE(drawn() != asSelected) << "the ruler let go of still looks selected";
    EXPECT_TRUE(box("How far B is from A") == nullptr) << "the field click left the ruler's table up";
    click(QPointF(1003, 1050));
    settle();
    EXPECT_TRUE(scene->isMeasurementSelected()) << "clicking the ruler again did not select it";
    EXPECT_EQ(scene->measurePoints().at(1), QPointF(1000, 1100)) << "clicking the ruler moved it";

    // The protractor carries C as well, and its angle swings C about A.
    angle->trigger();
    click(QPointF(1000, 1000));
    click(QPointF(1100, 1000));
    click(QPointF(1000, 900));
    settle();
    EXPECT_EQ(labels(), QStringList({ "Width AB (m)", "Width AC (m)", "A X", "A Y", "B X", "B Y",
                                      "C X", "C Y", "Angle (\u00B0)" }))
        << "the protractor's table is not what it was asked to be";

    // Each arm has its own width, and setting one leaves the other arm and
    // the angle alone.
    QDoubleSpinBox *armC = box("How far C is from A");
    ASSERT_TRUE(armC) << "the protractor's second arm has no width";
    EXPECT_NEAR(armC->value(), 1.0, 1e-9);
    armC->setValue(3.0);
    EXPECT_EQ(scene->measurePoints().at(2), QPointF(1000, 700)) << "C was not moved along its arm";
    EXPECT_EQ(scene->measurePoints().at(1), QPointF(1100, 1000)) << "B moved with C's width";
    EXPECT_EQ(scene->measureText().toStdString(), "90.0\u00B0") << "C's width changed the angle";
    armC->setValue(1.0);
    turn = box("The angle at A");
    ASSERT_TRUE(turn);
    EXPECT_NEAR(turn->value(), 90.0, 1e-9);
    turn->setValue(45.0);
    const QPointF c = scene->measurePoints().at(2);
    EXPECT_NEAR(c.x(), 1000.0 + 100.0 / std::sqrt(2.0), 1e-9) << "C did not swing about A";
    EXPECT_NEAR(c.y(), 1000.0 - 100.0 / std::sqrt(2.0), 1e-9) << "C swung to the other side of AB";
    EXPECT_EQ(scene->measureText().toStdString(), "45.0\u00B0");

    // The protractor is picked back the same way, here on a point.
    scene->selectShape(shape);
    settle();
    EXPECT_TRUE(box("The angle at A") == nullptr) << "a picked shape did not get the table";
    click(c);
    settle();
    EXPECT_EQ(scene->activeItem(), nullptr) << "clicking the protractor left the shape picked";
    EXPECT_TRUE(box("The angle at A")) << "clicking the protractor did not pick it";

    // Put away, the measurement takes its table with it.
    angle->trigger();
    settle();
    EXPECT_TRUE(box("The angle at A") == nullptr) << "the table outlived the measurement";
    EXPECT_TRUE(undo->isClean()) << "measuring marked the scene as edited";
}

// With a measurement on the canvas a shape is caught by its points however it
// is worked -- a handle dragged, a polygon drawn -- and Options can turn that
// off.
TEST(Rulers, AShapeIsCaughtByTheMeasurementsPointsUnlessOptionsSaysNot)
{
    MainWindow window;
    auto *scene = window.findChild<CanvasScene *>();
    ASSERT_TRUE(scene);
    OptionsDialog::Settings s = window.settingsSnapshotForTest();
    EXPECT_TRUE(s.snapToMeasurement) << "catching on the measurement is not on by default";
    s.snapToGrid = false;
    window.applySettingsForTest(s);
    scene->setCurrentScale(100.0);
    scene->setEditorMode(EditorMode::Edit);

    QAction *distance = nullptr;
    for (QAction *action : window.findChildren<QAction *>()) {
        if (action->text() == QLatin1String("Measure Distance"))
            distance = action;
    }
    ASSERT_TRUE(distance);

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
    const QPointF mark(1000, 1000);
    distance->trigger();
    mouse(QEvent::GraphicsSceneMousePress, mark);
    mouse(QEvent::GraphicsSceneMouseMove, QPointF(1300, 1400));
    mouse(QEvent::GraphicsSceneMouseRelease, QPointF(1300, 1400));
    ASSERT_TRUE(scene->isMeasurementLaid());

    auto *box = new RectangleItem;
    box->setRect(QRectF(0, 0, 100, 60));
    box->setPos(800, 800);
    scene->addItem(box);
    scene->notifyShapesChanged();
    const auto dragCorner = [&](const QPointF &to) {
        scene->selectShape(box);
        const QPointF grab = box->mapToScene(box->handleRect(HandleId::BottomRight).center());
        mouse(QEvent::GraphicsSceneMousePress, grab);
        mouse(QEvent::GraphicsSceneMouseMove, to);
        mouse(QEvent::GraphicsSceneMouseRelease, to);
        return box->mapToScene(box->rect().bottomRight());
    };
    EXPECT_EQ(dragCorner(mark + QPointF(3, -2)), mark) << "a resized corner was not caught by the ruler's end";

    // A polygon's vertex is caught the same way, and is a vertex rather than
    // the measurement's point picked up.
    scene->startPolygonDrawing();
    click(QPointF(700, 1200));
    click(mark + QPointF(-2, 3));
    QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
    QCoreApplication::sendEvent(scene, &enter);
    PolygonItem *drawn = nullptr;
    for (QGraphicsItem *item : scene->items()) {
        if (auto *polygon = dynamic_cast<PolygonItem *>(item))
            drawn = polygon;
    }
    ASSERT_TRUE(drawn) << "the polygon was not drawn";
    EXPECT_EQ(drawn->mapToScene(drawn->points().at(1)), mark) << "a vertex was not caught by the ruler's end";
    EXPECT_EQ(scene->measurePoints().at(0), mark) << "drawing the polygon moved the measurement";

    s.snapToMeasurement = false;
    window.applySettingsForTest(s);
    EXPECT_FALSE(window.settingsSnapshotForTest().snapToMeasurement) << "the choice was not kept";
    EXPECT_EQ(dragCorner(QPointF(990, 1030)), QPointF(990, 1030));
    EXPECT_EQ(dragCorner(mark + QPointF(3, -2)), mark + QPointF(3, -2))
        << "turned off in Options, the ruler still caught the corner";
}

// A ruler is marked off in grid cells from A, so a length can be counted off
// it the way it is counted off the grid.
TEST(Rulers, TheRulerIsMarkedOffInGridCells)
{
    CanvasScene scene;
    scene.setCurrentScale(100.0);
    scene.setShowGrid(false);
    scene.setMeasureTool(CanvasScene::Measure::Distance);
    const auto mouse = [&scene](QEvent::Type type, const QPointF &at) {
        QGraphicsSceneMouseEvent event(type);
        event.setScenePos(at);
        event.setButton(type == QEvent::GraphicsSceneMouseMove ? Qt::NoButton : Qt::LeftButton);
        event.setButtons(type == QEvent::GraphicsSceneMouseRelease ? Qt::NoButton : Qt::LeftButton);
        QCoreApplication::sendEvent(&scene, &event);
    };
    mouse(QEvent::GraphicsSceneMousePress, QPointF(0, 0));
    mouse(QEvent::GraphicsSceneMouseMove, QPointF(200, 0));
    mouse(QEvent::GraphicsSceneMouseRelease, QPointF(200, 0));
    ASSERT_TRUE(scene.isMeasurementLaid());

    // Rendered one to one, so a scene unit is a pixel: ticks stand off the
    // line by a few pixels, and are looked for just under it. A mark is a
    // pixel wide and lands on the edge between two, so both are asked.
    const auto inkAt = [&scene](qreal cell, int x) {
        scene.setGridCellSize(cell);
        QImage image(240, 40, QImage::Format_ARGB32);
        image.fill(Qt::white);
        QPainter painter(&image);
        scene.render(&painter, QRectF(), QRectF(-20, -20, 240, 40));
        painter.end();
        for (int column : { x + 19, x + 20 }) {
            if (image.pixelColor(column, 20 + 3).green() < 220)
                return true;
        }
        return false;
    };
    EXPECT_TRUE(inkAt(20.0, 100)) << "the fifth cell from A has no long mark";
    EXPECT_FALSE(inkAt(20.0, 110)) << "there is a mark between two cells";
    EXPECT_FALSE(inkAt(1000.0, 100)) << "a mark where the cell is longer than the ruler";
}

// The protractor's arms are marked off the same way, each from A.
TEST(Rulers, TheProtractorsArmsAreMarkedOffInGridCells)
{
    CanvasScene scene;
    scene.setCurrentScale(100.0);
    scene.setShowGrid(false);
    scene.setGridCellSize(20.0);
    scene.setMeasureTool(CanvasScene::Measure::Angle);
    const auto click = [&scene](const QPointF &at) {
        for (QEvent::Type type : { QEvent::GraphicsSceneMousePress, QEvent::GraphicsSceneMouseRelease }) {
            QGraphicsSceneMouseEvent event(type);
            event.setScenePos(at);
            event.setButton(Qt::LeftButton);
            event.setButtons(type == QEvent::GraphicsSceneMouseRelease ? Qt::NoButton : Qt::LeftButton);
            QCoreApplication::sendEvent(&scene, &event);
        }
    };
    click(QPointF(0, 0));
    click(QPointF(200, 0));
    click(QPointF(0, 200));
    ASSERT_TRUE(scene.isMeasurementLaid());

    QImage image(240, 240, QImage::Format_ARGB32);
    image.fill(Qt::white);
    QPainter painter(&image);
    scene.render(&painter, QRectF(), QRectF(-20, -20, 240, 240));
    painter.end();
    const auto inked = [&image](int x, int y) {
        return image.pixelColor(x + 20, y + 20).green() < 220;
    };
    EXPECT_TRUE(inked(100, -3) || inked(99, -3)) << "the first arm has no long mark at its fifth cell";
    EXPECT_FALSE(inked(110, -3) || inked(109, -3)) << "the first arm has a mark between two cells";
    EXPECT_TRUE(inked(-3, 100) || inked(-3, 99)) << "the second arm has no long mark at its fifth cell";
}
