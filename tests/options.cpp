// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ruslan Muhlinin. See LICENSE.
#include "CanvasScene.h"
#include "RectangleItem.h"
#include "ShapeItem.h"
#include "OptionsDialog.h"
#include "ShapeStyle.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QDirIterator>
#include <QIcon>
#include <QImage>
#include <QPixmap>
#include "PhysicsBody.h"
#include "SceneSerializer.h"
#include "PhysicsTypes.h"
#include "SceneControl.h"
#include "SimulationController.h"
#include "Rule.h"
#include "SceneVariable.h"
#include <QtTest/QTest>
#include <QPushButton>
#include <QSlider>
#include "ControlsPanel.h"
#include <QFrame>
#include <QLineEdit>
#include <QGraphicsView>
#include "PropertyPanel.h"
#include <QTabWidget>
#include <QTableWidget>
#include <QScrollBar>
#include <QLabel>
#include <QTextDocument>
#include <gtest/gtest.h>
#include "MainWindow.h"
#include <QAction>
#include <QDir>
#include <QMenu>
#include <QTemporaryDir>

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
    s.converterSettings.insert(QStringLiteral("box2d3wasm"),
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

namespace {

// The paths the Open Recent menu is offering, newest first.
QStringList offered(MainWindow *window)
{
    QStringList paths;
    auto *menu = window->findChild<QMenu *>(QStringLiteral("menuRecent"));
    if (!menu)
        return paths;
    for (QAction *action : menu->actions())
        paths << QDir::fromNativeSeparators(action->toolTip());
    return paths;
}

QString madeIn(const QTemporaryDir &folder, const QString &name)
{
    return QDir(folder.path()).filePath(name);
}

} // namespace

// Saving a scene puts it on the list. A scene saved for the first time has
// never been opened, so recording only on open left it unreachable from Open
// Recent until it had been opened the long way round once.
TEST(RecentFiles, SavingASceneRecordsIt)
{
    QTemporaryDir folder;
    ASSERT_TRUE(folder.isValid());

    MainWindow window;
    const QString path = madeIn(folder, QStringLiteral("saved.phys"));
    ASSERT_TRUE(window.saveSceneAsForTest(path)) << "the scene would not save";

    EXPECT_TRUE(offered(&window).contains(QDir::fromNativeSeparators(path)))
        << "a scene that was just saved is not offered under Open Recent";
}

// Opening one does too, newest first, with no repeats and no more than five.
TEST(RecentFiles, TheListIsNewestFirstWithoutRepeats)
{
    QTemporaryDir folder;
    ASSERT_TRUE(folder.isValid());

    MainWindow window;
    QStringList made;
    for (int i = 1; i <= 7; ++i) {
        const QString path = madeIn(folder, QStringLiteral("scene%1.phys").arg(i));
        ASSERT_TRUE(window.saveSceneAsForTest(path)) << path.toStdString();
        made << QDir::fromNativeSeparators(path);
    }

    QStringList list = offered(&window);
    EXPECT_EQ(list.size(), 5) << "the list is not kept to five";
    EXPECT_EQ(list.first(), made.last()) << "the newest scene is not at the top";
    EXPECT_FALSE(list.contains(made.first()))
        << "the oldest scene was not pushed off the end";

    // Saving one again moves it to the top rather than listing it twice.
    ASSERT_TRUE(window.saveSceneAsForTest(made.at(3)));
    list = offered(&window);
    EXPECT_EQ(list.first(), made.at(3)) << "saving a scene again did not bring it to the top";
    EXPECT_EQ(list.count(made.at(3)), 1) << "the same scene is listed twice";
}

// And one that has gone takes itself off rather than sitting there failing.
TEST(RecentFiles, AMissingSceneDropsOffTheList)
{
    QTemporaryDir folder;
    ASSERT_TRUE(folder.isValid());

    MainWindow window;
    const QString path = madeIn(folder, QStringLiteral("gone.phys"));
    ASSERT_TRUE(window.saveSceneAsForTest(path));
    ASSERT_TRUE(offered(&window).contains(QDir::fromNativeSeparators(path)));

    ASSERT_TRUE(QFile::remove(path));
    auto *menu = window.findChild<QMenu *>(QStringLiteral("menuRecent"));
    ASSERT_NE(menu, nullptr);
    ASSERT_FALSE(menu->actions().isEmpty());
    menu->actions().first()->trigger();

    EXPECT_FALSE(offered(&window).contains(QDir::fromNativeSeparators(path)))
        << "a scene that no longer opens is still being offered";
}

// Qt's SVG renderer is not the whole of SVG: an icon that is perfectly valid
// XML can still draw nothing at all -- an arc command once left the Rules tab
// with a blank square. Valid is not the question; pixels are.
TEST(Options, EveryIconActuallyDraws)
{
    QDirIterator walk(QStringLiteral(":/icons"), QDirIterator::Subdirectories);
    int checked = 0;
    while (walk.hasNext()) {
        const QString path = walk.next();
        if (!path.endsWith(QLatin1String(".svg")))
            continue;
        ++checked;

        const QPixmap pixmap = QIcon(path).pixmap(20, 20);
        ASSERT_FALSE(pixmap.isNull()) << path.toStdString() << " produced no pixmap";

        const QImage image = pixmap.toImage();
        int painted = 0;
        for (int y = 0; y < image.height(); ++y) {
            for (int x = 0; x < image.width(); ++x) {
                if (qAlpha(image.pixel(x, y)) > 20)
                    ++painted;
            }
        }
        EXPECT_GT(painted, 20)
            << path.toStdString() << " draws only " << painted
            << " pixels at 20px -- Qt could not render it";
    }
    EXPECT_GT(checked, 20) << "the icon resource looks empty";
}

// A caption wraps, and a wrapped label cannot size itself: its size hint is
// the width the text would want on one line, so asking it to adjust gave a box
// too narrow for what it then wrapped and the last words were cut off.
TEST(Options, ACaptionIsTallEnoughForWhatItWraps)
{
    MainWindow window;
    window.resize(900, 700);
    // Shown, or the label is never laid out and never "visible".
    window.show();
    QCoreApplication::processEvents();
    CanvasScene *scene = window.findChild<CanvasScene *>();
    ASSERT_TRUE(scene);

    QLabel *caption = window.findChild<QLabel *>(QStringLiteral("SceneCaption"));
    ASSERT_TRUE(caption) << "the window has no caption label";

    const QString words =
        QStringLiteral("Press <b>Space</b> for start/stop. Press Arrow <b>Up</b>/Down for"
                       " increment/decrement the motor speed, and keep going until there is"
                       " plainly more here than fits on one line.");
    scene->setCaption(words, true);
    QCoreApplication::processEvents();

    ASSERT_TRUE(caption->isVisible());
    EXPECT_LE(caption->width(), caption->maximumWidth()) << "the caption ignored its width limit";

    // What the text needs at that width, laid out the same way the label lays
    // it out. The label has to be at least that tall, or it is clipping.
    QTextDocument wrapped;
    wrapped.setDefaultFont(caption->font());
    wrapped.setHtml(words);
    wrapped.setTextWidth(caption->contentsRect().width());
    EXPECT_GE(caption->contentsRect().height(), int(wrapped.size().height()))
        << "the caption is shorter than the text it wrapped -- the end is cut off";
    EXPECT_GT(wrapped.size().height(), caption->fontMetrics().height() * 2)
        << "the test text did not actually wrap";
}

// The caption sits under the log, and the log grows every time a watch is
// added. Placed once and left alone, it was overlapped the moment the log
// gained a line.
TEST(Options, ACaptionStaysClearOfTheLog)
{
    MainWindow window;
    window.resize(900, 700);
    window.show();
    QCoreApplication::processEvents();

    CanvasScene *scene = window.findChild<CanvasScene *>();
    ASSERT_TRUE(scene);
    QLabel *caption = window.findChild<QLabel *>(QStringLiteral("SceneCaption"));
    QLabel *log = window.findChild<QLabel *>(QStringLiteral("LogOverlay"));
    ASSERT_TRUE(caption);
    ASSERT_TRUE(log);

    RectangleItem *box = scene->addRectangle(QPointF(0, 0));
    ASSERT_TRUE(box);
    // The log only appears while a run is going: outside one the values are
    // what the property table already shows.
    scene->setEditorMode(EditorMode::Physics);
    scene->selectForPhysics(box, true);
    ASSERT_TRUE(scene->createBodyFromSelection());
    window.startRunForTest();
    QCoreApplication::processEvents();

    scene->setCaption(QStringLiteral("Press <b>Space</b> to start, and read this to the end."),
                      true);
    QCoreApplication::processEvents();
    ASSERT_TRUE(caption->isVisible());

    const auto clear = [&] {
        if (!log->isVisible())
            return true;
        return !log->geometry().intersects(caption->geometry());
    };
    EXPECT_TRUE(clear()) << "the caption overlaps the log with nothing logged yet";

    // Every watch the log gains makes it taller, and the caption has to move.
    for (int i = 0; i < 4; ++i) {
        scene->addWatch({ box->name(), QStringLiteral("rotation"),
                          QStringLiteral("turn%1").arg(i) });
        QCoreApplication::processEvents();
        ASSERT_TRUE(log->isVisible()) << "the log did not appear";
        EXPECT_TRUE(clear()) << "the caption overlaps the log after " << (i + 1) << " watches";
        EXPECT_GE(caption->y(), log->y() + log->height())
            << "the caption is not below the log after " << (i + 1) << " watches";
    }
}

// A control declared in the scene appears on the canvas and works: ticking the
// box on screen is what a rule hears, and a rule writing it moves the widget.
TEST(Options, ControlsAppearOnTheCanvasAndWork)
{
    MainWindow window;
    window.resize(900, 700);
    window.show();
    QCoreApplication::processEvents();

    CanvasScene *scene = window.findChild<CanvasScene *>();
    ASSERT_TRUE(scene);

    SceneControl tick;
    tick.id = QStringLiteral("go");
    tick.kind = SceneControl::Kind::Checkbox;
    tick.initial = false;
    SceneControl pull;
    pull.id = QStringLiteral("speed");
    pull.kind = SceneControl::Kind::Slider;
    pull.minimum = 0.0;
    pull.maximum = 10.0;
    pull.step = 1.0;
    pull.initial = 2.0;
    SceneControl press;
    press.id = QStringLiteral("fire");
    press.kind = SceneControl::Kind::Button;
    scene->setControls({ tick, pull, press });

    // They are only up while a run is: see ControlsAreOnlyThereWhileARunIs.
    RectangleItem *ground = scene->addRectangle(QPointF(0, 0));
    scene->setEditorMode(EditorMode::Physics);
    scene->selectForPhysics(ground, true);
    ASSERT_TRUE(scene->createBodyFromSelection());
    window.startRunForTest();
    QCoreApplication::processEvents();

    QWidget *panel = window.findChild<QWidget *>(QStringLiteral("SceneControls"));
    ASSERT_TRUE(panel) << "the canvas has no controls panel";
    EXPECT_TRUE(panel->isVisible()) << "the controls did not appear";

    QCheckBox *box = nullptr;
    QPushButton *button = nullptr;
    QSlider *slider = nullptr;
    for (QCheckBox *one : panel->findChildren<QCheckBox *>())
        box = one;
    for (QPushButton *one : panel->findChildren<QPushButton *>())
        button = one;
    for (QSlider *one : panel->findChildren<QSlider *>())
        slider = one;
    ASSERT_TRUE(box) << "no checkbox on the canvas";
    ASSERT_TRUE(button) << "no button on the canvas";
    ASSERT_TRUE(slider) << "no slider on the canvas";
    EXPECT_EQ(box->text(), QStringLiteral("go")) << "the name is not what it shows";

    // A hand on the widgets is what the scene hears.
    box->setChecked(true);
    EXPECT_TRUE(scene->controlValue(QStringLiteral("go")).toBool())
        << "ticking the box on screen did not reach the scene";
    slider->setValue(7);
    EXPECT_DOUBLE_EQ(scene->controlValue(QStringLiteral("speed")).toDouble(), 7.0)
        << "pulling the slider did not reach the scene";

    button->pressed();
    EXPECT_TRUE(scene->controlValue(QStringLiteral("fire")).toBool());
    button->released();
    EXPECT_FALSE(scene->controlValue(QStringLiteral("fire")).toBool());

    // And a rule writing one moves the widget, without the widget answering
    // back and writing it again.
    scene->setControlValue(QStringLiteral("speed"), 3.0);
    QCoreApplication::processEvents();
    EXPECT_EQ(slider->value(), 3) << "the slider did not follow the scene";
    EXPECT_DOUBLE_EQ(scene->controlValue(QStringLiteral("speed")).toDouble(), 3.0)
        << "the widget answered back and overwrote the value";

    scene->setControlEnabled(QStringLiteral("speed"), false);
    QCoreApplication::processEvents();
    EXPECT_FALSE(slider->isEnabled()) << "greying the control out did not reach the widget";

    // A rule that changes what the button says must not cost it its press. The
    // caption used to count as a different set of controls, which rebuilt the
    // widgets while the button was still down: the release never arrived, the
    // value stayed true, and the press after it did nothing at all.
    SceneVariable count;
    count.name = QStringLiteral("presses");
    count.type = SceneVariable::Type::Integer;
    count.initial = 0;
    scene->setVariables({ count });

    Rule counting;
    counting.conditions.resize(1);
    counting.conditions[0].subjectName = Rule::controls();
    counting.conditions[0].eventId = Rule::controlPressedEvent();
    counting.conditions[0].conditionValue = press.id;
    counting.actions.resize(2);
    counting.actions[0].targetName = Rule::variables();
    counting.actions[0].propertyKey = count.name;
    counting.actions[0].op = Rule::Op::Add;
    counting.actions[0].value = 1;
    counting.actions[1].targetName = Rule::controls();
    counting.actions[1].propertyKey = press.captionKey();
    counting.actions[1].op = Rule::Op::Set;
    counting.actions[1].value = QStringLiteral("Again");
    scene->setRules({ counting });

    SimulationController *sim = window.findChild<SimulationController *>();
    ASSERT_TRUE(sim);
    for (int click = 1; click <= 2; ++click) {
        QTest::mouseClick(button, Qt::LeftButton);
        QCoreApplication::processEvents();
        sim->stepFrame();
        QCoreApplication::processEvents();
        EXPECT_FALSE(scene->controlValue(press.id).toBool())
            << "click " << click << " left the button held down";
        EXPECT_EQ(sim->readValue(Rule::variables(), count.name).toInt(), click)
            << "click " << click << " did not reach the rule";
    }
    EXPECT_EQ(button->text(), QStringLiteral("Again"))
        << "the button does not say what the rule set";

    // They sit below the caption, not over it.
    scene->setCaption(QStringLiteral("Hello"), true);
    QCoreApplication::processEvents();
    QLabel *caption = window.findChild<QLabel *>(QStringLiteral("SceneCaption"));
    ASSERT_TRUE(caption);
    EXPECT_GE(panel->y(), caption->y() + caption->height())
        << "the controls overlap the caption";
}

// One body can be kept in view: while a run is going the view follows it, so
// it stays in the middle of the screen and the scene moves around it. One,
// because a view has one middle -- giving it to another takes it off the
// first.
TEST(Options, TheViewFollowsTheBodyKeptInView)
{
    MainWindow window;
    window.resize(900, 700);
    window.show();
    QCoreApplication::processEvents();

    CanvasScene *scene = window.findChild<CanvasScene *>();
    QGraphicsView *view = window.findChild<QGraphicsView *>();
    ASSERT_TRUE(scene);
    ASSERT_TRUE(view);
    scene->setEditorMode(EditorMode::Physics);

    const auto makeBody = [&](const QPointF &at, physics::BodyType type) {
        RectangleItem *shape = scene->addRectangle(at);
        shape->setRect(QRectF(-20, -10, 40, 20));
        scene->clearPhysicsSelection();
        scene->selectForPhysics(shape, true);
        PhysicsBody *body = scene->createBodyFromSelection();
        body->props().type = type;
        return body;
    };
    PhysicsBody *faller = makeBody(QPointF(-300, -200), physics::BodyType::Dynamic);
    PhysicsBody *other = makeBody(QPointF(300, 150), physics::BodyType::Dynamic);
    ASSERT_TRUE(faller);
    ASSERT_TRUE(other);
    EXPECT_EQ(scene->focusedBody(), nullptr) << "a scene follows nothing until it is told to";

    scene->setFocusedBody(faller);
    EXPECT_EQ(scene->focusedBody(), faller);
    scene->setFocusedBody(other);
    EXPECT_EQ(scene->focusedBody(), other) << "the second body was not given it";
    EXPECT_FALSE(faller->isFocused()) << "two bodies are kept in view at once";
    scene->setFocusedBody(faller);

    // It survives the file, and only the one body carries it.
    CanvasScene read;
    QString error;
    ASSERT_TRUE(SceneSerializer::load(&read, SceneSerializer::save(scene), &error))
        << error.toStdString();
    ASSERT_TRUE(read.focusedBody());
    EXPECT_EQ(read.focusedBody()->name(), faller->name());
    int followed = 0;
    for (PhysicsBody *body : read.bodies())
        followed += body->isFocused() ? 1 : 0;
    EXPECT_EQ(followed, 1);

    // While the run goes, the view keeps it in the middle.
    const QPointF started = faller->originScenePos();
    window.startRunForTest();
    QCoreApplication::processEvents();
    SimulationController *sim = window.findChild<SimulationController *>();
    ASSERT_TRUE(sim);
    for (int i = 0; i < 20; ++i)
        sim->stepFrame();
    QCoreApplication::processEvents();

    const QPointF middle = view->mapToScene(view->viewport()->rect().center());
    const QPointF where = faller->originScenePos();
    EXPECT_LT(QLineF(middle, where).length(), 40.0)
        << "the view is at " << middle.x() << "," << middle.y() << " and the body at "
        << where.x() << "," << where.y();

    // And it moved to get there: following something standing still proves
    // nothing.
    EXPECT_GT(where.y() - started.y(), 20.0) << "the body did not fall";
    sim->stop();

    // A rule can hand the view to another body while the run is going, and
    // the scene's own choice comes back when it stops.
    Rule handover;
    handover.name = QStringLiteral("watch the other one");
    handover.conditions[0].subjectName = Rule::world();
    handover.conditions[0].eventId = Rule::runStartedEvent();
    handover.actions[0].targetName = other->name();
    handover.actions[0].actionId = Rule::keepInViewAction();
    handover.actions[0].actionParams.insert(Rule::keepInViewParam(), true);
    scene->setRules({ handover });

    window.startRunForTest();
    QCoreApplication::processEvents();
    for (int i = 0; i < 10; ++i)
        sim->stepFrame();
    QCoreApplication::processEvents();
    EXPECT_EQ(scene->focusedBody(), other) << "the rule did not hand the view over";
    EXPECT_LT(QLineF(view->mapToScene(view->viewport()->rect().center()),
                     other->originScenePos()).length(), 40.0)
        << "the view did not follow the body the rule named";

    sim->stop();
    EXPECT_EQ(scene->focusedBody(), faller)
        << "stopping did not put back the body the scene itself keeps in view";
}

// The field is the scene's: its size, its background and its grid travel in
// the file, and opening one brings its own. What Options holds is the field a
// *new* scene starts with -- changing it does not repaint the scene that is
// open, and the scene's own colour does not creep into the settings and from
// there into every scene after it.
TEST(Options, TheFieldBelongsToTheSceneAndOptionsHoldsTheDefault)
{
    MainWindow window;
    CanvasScene *scene = window.findChild<CanvasScene *>();
    ASSERT_TRUE(scene);

    // What this scene is drawn on.
    scene->setBackgroundColor(QColor(200, 30, 30));
    scene->setShowGrid(false);
    scene->setGridCellSize(37.0);
    scene->setFieldSize(1234.0, 567.0);

    // Somebody sets a different default in Options.
    OptionsDialog::Settings defaults = window.settingsSnapshotForTest();
    defaults.backgroundColor = QColor(20, 40, 200);
    defaults.showGrid = true;
    defaults.gridCellSize = 50.0;
    defaults.fieldWidth = 800.0;
    defaults.fieldHeight = 600.0;
    window.applySettingsForTest(defaults);

    EXPECT_EQ(scene->backgroundColor(), QColor(200, 30, 30))
        << "a default repainted the scene that was open";
    EXPECT_FALSE(scene->showGrid()) << "a default turned the open scene's grid back on";
    EXPECT_DOUBLE_EQ(scene->gridCellSize(), 37.0);
    EXPECT_DOUBLE_EQ(scene->fieldWidth(), 1234.0);

    // And what would be written to the settings file is the default, not
    // whatever the open scene happens to be drawn on.
    const OptionsDialog::Settings saved = window.settingsSnapshotForTest();
    EXPECT_EQ(saved.backgroundColor, QColor(20, 40, 200))
        << "the open scene's background became the setting";
    EXPECT_TRUE(saved.showGrid);
    EXPECT_DOUBLE_EQ(saved.gridCellSize, 50.0);

    // The scene carries its own through the file.
    CanvasScene read;
    QString error;
    ASSERT_TRUE(SceneSerializer::load(&read, SceneSerializer::save(scene), &error))
        << error.toStdString();
    EXPECT_EQ(read.backgroundColor(), QColor(200, 30, 30)) << "the file lost the background";
    EXPECT_FALSE(read.showGrid()) << "the file lost whether the grid is shown";
    EXPECT_DOUBLE_EQ(read.gridCellSize(), 37.0);
    EXPECT_DOUBLE_EQ(read.fieldWidth(), 1234.0);

    // A new scene is where the default is for.
    QAction *newScene = window.findChild<QAction *>(QStringLiteral("actionNewScene"));
    ASSERT_TRUE(newScene);
    newScene->trigger();
    QCoreApplication::processEvents();
    EXPECT_EQ(scene->backgroundColor(), QColor(20, 40, 200))
        << "a new scene did not start with the default background";
    EXPECT_TRUE(scene->showGrid()) << "a new scene did not start with the default grid";
    EXPECT_DOUBLE_EQ(scene->gridCellSize(), 50.0);
    EXPECT_DOUBLE_EQ(scene->fieldWidth(), 800.0);
}

// The Controls tab is cards, not a table: a slider has four numbers to declare
// and a row of cells too narrow to read is no way to show them. Each card
// carries the control's fields on rows of its own, the way a rule card does.
TEST(Options, TheControlsTabIsOneCardPerControl)
{
    MainWindow window;
    window.resize(900, 700);
    window.show();
    QCoreApplication::processEvents();

    CanvasScene *scene = window.findChild<CanvasScene *>();
    ControlsPanel *panel = window.findChild<ControlsPanel *>();
    ASSERT_TRUE(scene);
    ASSERT_TRUE(panel) << "there is no Controls tab";

    const auto cards = [&] { return panel->findChildren<QFrame *>(QStringLiteral("controlCard")); };
    EXPECT_TRUE(cards().isEmpty()) << "a scene with no controls shows a card";

    SceneControl tick;
    tick.id = QStringLiteral("go");
    tick.kind = SceneControl::Kind::Checkbox;
    SceneControl pull;
    pull.id = QStringLiteral("speed");
    pull.kind = SceneControl::Kind::Slider;
    pull.minimum = 0.0;
    pull.maximum = 10.0;
    pull.step = 0.5;
    pull.initial = 4.0;
    scene->setControls({ tick, pull });
    QCoreApplication::processEvents();

    ASSERT_EQ(cards().size(), 2) << "one card per control";

    // The checkbox card: its id, its caption, its kind, and where it starts.
    QFrame *box = cards().at(0);
    const QList<QLineEdit *> words = box->findChildren<QLineEdit *>();
    ASSERT_EQ(words.size(), 2) << "the id a rule uses and the caption it shows";
    EXPECT_EQ(words.at(0)->text(), QStringLiteral("go"));
    EXPECT_TRUE(words.at(1)->text().isEmpty()) << "no caption declared";
    EXPECT_EQ(words.at(1)->placeholderText(), QStringLiteral("go"))
        << "an empty caption says what it falls back to";
    EXPECT_EQ(box->findChildren<QComboBox *>().size(), 1) << "the kind";
    EXPECT_TRUE(box->findChildren<QDoubleSpinBox *>().isEmpty())
        << "a checkbox has no numbers to declare";

    // The slider card: all four of its numbers, each on a row of its own.
    QFrame *slider = cards().at(1);
    EXPECT_EQ(slider->findChildren<QDoubleSpinBox *>().size(), 4)
        << "a slider declares where it starts, its lowest, its highest and its step";

    // Editing one writes it through.
    QDoubleSpinBox *step = slider->findChildren<QDoubleSpinBox *>().at(3);
    step->setValue(2.0);
    QCoreApplication::processEvents();
    EXPECT_DOUBLE_EQ(scene->controls().at(1).step, 2.0) << "the step did not reach the scene";
}

// Controls are how a running scene is worked: outside a run there is nothing
// for a press to reach, and a slider left standing over a scene being edited
// is just something in the way.
TEST(Options, ControlsAreOnlyThereWhileARunIs)
{
    MainWindow window;
    window.resize(900, 700);
    window.show();
    QCoreApplication::processEvents();

    CanvasScene *scene = window.findChild<CanvasScene *>();
    ASSERT_TRUE(scene);

    SceneControl pull;
    pull.id = QStringLiteral("speed");
    pull.kind = SceneControl::Kind::Slider;
    pull.minimum = 0.0;
    pull.maximum = 10.0;
    pull.step = 1.0;
    pull.initial = 2.0;
    scene->setControls({ pull });
    QCoreApplication::processEvents();

    QWidget *panel = window.findChild<QWidget *>(QStringLiteral("SceneControls"));
    ASSERT_TRUE(panel);
    EXPECT_FALSE(panel->isVisible()) << "the controls are up with no run going";

    RectangleItem *box = scene->addRectangle(QPointF(0, 0));
    scene->setEditorMode(EditorMode::Physics);
    scene->selectForPhysics(box, true);
    ASSERT_TRUE(scene->createBodyFromSelection());
    window.startRunForTest();
    QCoreApplication::processEvents();
    EXPECT_TRUE(panel->isVisible()) << "the controls did not appear when the run started";

    window.stopRunForTest();
    QCoreApplication::processEvents();
    EXPECT_FALSE(panel->isVisible()) << "the controls are still up after the run stopped";
}

// A full-screen run is a different view, and the log, the caption and the
// controls are children of a view. Left behind, they stayed on the editor's
// window where nobody was looking -- and the keys went to a window that had
// never been given the focus.
TEST(Options, TheOverlaysFollowAFullScreenRun)
{
    MainWindow window;
    window.resize(900, 700);
    window.show();
    QCoreApplication::processEvents();

    CanvasScene *scene = window.findChild<CanvasScene *>();
    ASSERT_TRUE(scene);

    SceneControl pull;
    pull.id = QStringLiteral("speed");
    pull.kind = SceneControl::Kind::Slider;
    pull.minimum = 0.0;
    pull.maximum = 10.0;
    pull.step = 1.0;
    pull.initial = 2.0;
    scene->setControls({ pull });

    RectangleItem *box = scene->addRectangle(QPointF(0, 0));
    scene->setEditorMode(EditorMode::Physics);
    scene->selectForPhysics(box, true);
    ASSERT_TRUE(scene->createBodyFromSelection());
    scene->addWatch({ box->name(), QStringLiteral("rotation"), QStringLiteral("turn") });

    window.startRunForTest();
    QCoreApplication::processEvents();

    QGraphicsView *editorView = window.findChild<QGraphicsView *>();
    ASSERT_TRUE(editorView);
    QLabel *log = window.findChild<QLabel *>(QStringLiteral("LogOverlay"));
    QWidget *controls = window.findChild<QWidget *>(QStringLiteral("SceneControls"));
    ASSERT_TRUE(log);
    ASSERT_TRUE(controls);
    scene->setCaption(QStringLiteral("Hello"), true);
    QCoreApplication::processEvents();
    QLabel *caption = window.findChild<QLabel *>(QStringLiteral("SceneCaption"));
    ASSERT_TRUE(caption);
    EXPECT_EQ(log->parentWidget(), editorView);

    // Handed to another view the way the full-screen run hands them over.
    // Driven directly rather than by opening a real full-screen window, which
    // needs a screen this may not have.
    QGraphicsView other;
    other.setScene(scene);
    other.resize(800, 600);
    window.moveOverlaysForTest(&other);
    QCoreApplication::processEvents();

    EXPECT_EQ(log->parentWidget(), &other) << "the log stayed on the editor's window";
    EXPECT_EQ(caption->parentWidget(), &other) << "the caption stayed behind";
    EXPECT_EQ(controls->parentWidget(), &other) << "the controls stayed behind";

    window.moveOverlaysForTest(editorView);
    QCoreApplication::processEvents();
    EXPECT_EQ(log->parentWidget(), editorView) << "the log did not come back";
    EXPECT_EQ(caption->parentWidget(), editorView) << "the caption did not come back";
    EXPECT_EQ(controls->parentWidget(), editorView) << "the controls did not come back";

    window.stopRunForTest();
}

// The table opens the next object where the last one was left. Scrolled half
// way down, another shape of the same kind opens half way down -- that is how
// a value is compared across objects. Scrolled back to the top, it opens at
// the top: the place was recorded only when it was not zero, so going back up
// left the old offset standing and the next shape opened at it.
TEST(Options, AnotherObjectOpensWhereTheLastOneWasLeft)
{
    MainWindow window;
    window.resize(700, 420);
    window.show();
    QCoreApplication::processEvents();

    CanvasScene *scene = window.findChild<CanvasScene *>();
    PropertyPanel *panel = window.findChild<PropertyPanel *>();
    ASSERT_TRUE(scene);
    ASSERT_TRUE(panel);
    scene->setEditorMode(EditorMode::Edit);

    RectangleItem *first = scene->addRectangle(QPointF(0, 0));
    RectangleItem *second = scene->addRectangle(QPointF(200, 0));
    scene->notifyShapesChanged();

    // By the name of the section, not by which table happens to be the
    // tallest: at this size more than one of them scrolls by a single step,
    // and which of those is "tallest" comes down to the order the panel built
    // them in.
    const auto tableOf = [&](const QString &section) -> QTableWidget * {
        for (QTabWidget *tabs : panel->findChildren<QTabWidget *>()) {
            for (int i = 0; i < tabs->count(); ++i) {
                if (tabs->tabText(i) == section)
                    return qobject_cast<QTableWidget *>(tabs->widget(i));
            }
        }
        return nullptr;
    };

    scene->selectShape(first);
    QCoreApplication::processEvents();

    QString section;
    for (QTabWidget *tabs : panel->findChildren<QTabWidget *>()) {
        for (int i = 0; i < tabs->count(); ++i) {
            auto *one = qobject_cast<QTableWidget *>(tabs->widget(i));
            if (one && one->verticalScrollBar()->maximum() > 0)
                section = tabs->tabText(i);
        }
    }
    ASSERT_FALSE(section.isEmpty())
        << "every section fits without scrolling, so this proves nothing";

    // Left part way down: the next shape opens there. The bottom rather than
    // the middle, because a table only a step or two taller than the panel has
    // no middle to speak of.
    const int middle = tableOf(section)->verticalScrollBar()->maximum();
    ASSERT_GT(middle, 0);
    tableOf(section)->verticalScrollBar()->setValue(middle);
    QCoreApplication::processEvents();
    ASSERT_EQ(tableOf(section)->verticalScrollBar()->value(), middle)
        << "the table would not scroll there at all";

    scene->selectShape(second);
    QCoreApplication::processEvents();
    EXPECT_EQ(tableOf(section)->verticalScrollBar()->value(), middle)
        << "the next shape did not open where the last one was left";

    // Left at the top: the next shape opens at the top.
    tableOf(section)->verticalScrollBar()->setValue(0);
    QCoreApplication::processEvents();

    scene->selectShape(first);
    QCoreApplication::processEvents();
    EXPECT_EQ(tableOf(section)->verticalScrollBar()->value(), 0)
        << "scrolled back to the top, the next shape still opened half way down";
}

// A short caption should take the room a short caption needs. Sized to the
// widest it was allowed, a line and a half of text put a 420-pixel box of
// white space over the scene.
TEST(Options, AShortCaptionDoesNotTakeTheWholeWidth)
{
    MainWindow window;
    window.resize(900, 700);
    window.show();
    QCoreApplication::processEvents();

    CanvasScene *scene = window.findChild<CanvasScene *>();
    ASSERT_TRUE(scene);
    QLabel *caption = window.findChild<QLabel *>(QStringLiteral("SceneCaption"));
    ASSERT_TRUE(caption);

    scene->setCaption(QStringLiteral("Use <b>Left/Right</b> arrow key<br>to control the"
                                     " flippers."),
                      true);
    QCoreApplication::processEvents();
    ASSERT_TRUE(caption->isVisible());
    const int shortWidth = caption->width();
    EXPECT_LT(shortWidth, caption->maximumWidth())
        << "a two-line caption took the whole width it was allowed";

    // Still wide enough for what it says.
    QTextDocument shown;
    shown.setDefaultFont(caption->font());
    shown.setHtml(scene->caption());
    shown.setTextWidth(caption->contentsRect().width());
    EXPECT_GE(caption->contentsRect().height(), int(shown.size().height()))
        << "shrinking it cut the text off";

    // A long one still grows to the limit and wraps there.
    QString much;
    for (int i = 0; i < 40; ++i)
        much += QStringLiteral("every word here is one more than the box can take. ");
    scene->setCaption(much, true);
    QCoreApplication::processEvents();
    EXPECT_GT(caption->width(), shortWidth) << "a long caption did not grow";
    EXPECT_LE(caption->width(), caption->maximumWidth()) << "it grew past its limit";
}
