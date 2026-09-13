#include "CanvasScene.h"
#include "FullScreenView.h"
#include "MainWindow.h"
#include "PhysicsBody.h"
#include "RectangleItem.h"
#include "Rule.h"
#include "SimulationController.h"

#include <QApplication>
#include <QCheckBox>
#include <QEvent>
#include <QWheelEvent>
#include <QTest>
#include <gtest/gtest.h>

// The run on a screen of its own, and the three keys that stand in for the
// transport buttons that are not on it.

namespace {

void settle()
{
    for (int i = 0; i < 30; ++i)
        QCoreApplication::processEvents();
    // The window is closed with deleteLater -- it may be the very thing whose
    // key press is still on the stack -- and outside an event loop those are
    // only delivered when asked for.
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
}

QCheckBox *checkNamed(MainWindow *window, const QString &text)
{
    for (QCheckBox *box : window->findChildren<QCheckBox *>())
        if (box->text() == text)
            return box;
    return nullptr;
}

PhysicsBody *oneBody(CanvasScene *scene)
{
    auto *shape = new RectangleItem;
    shape->setRect(QRectF(0, 0, 60, 60));
    shape->setName(QStringLiteral("block"));
    scene->addItem(shape);
    scene->notifyShapesChanged();
    scene->setEditorMode(EditorMode::Physics);
    scene->selectForPhysics(shape, true);
    PhysicsBody *body = scene->createBodyFromSelection();
    scene->clearPhysicsSelection();
    return body;
}

} // namespace

TEST(FullScreenRun, OpensWithTheRunAndTakesTheKeys)
{
    MainWindow window;
    window.resize(1200, 800);
    window.show();
    settle();

    auto *scene = window.findChild<CanvasScene *>();
    auto *sim = window.findChild<SimulationController *>();
    ASSERT_TRUE(oneBody(scene));

    QCheckBox *full = checkNamed(&window, QStringLiteral("Full Screen"));
    ASSERT_TRUE(full) << "the transport row offers it";
    EXPECT_FALSE(window.findChild<FullScreenView *>()) << "nothing opens while it is off";

    full->setChecked(true);
    settle();
    EXPECT_FALSE(window.findChild<FullScreenView *>())
        << "and nothing opens until something is running";

    sim->start();
    settle();
    auto *view = window.findChild<FullScreenView *>();
    ASSERT_TRUE(view) << "starting a run opens it";
    EXPECT_EQ(view->scene(), scene) << "showing the same scene, not a copy of it";
    EXPECT_FALSE(view->isInteractive()) << "and showing only -- a click cannot drag a body";

    // Space holds the run and lets it go again.
    QTest::keyClick(view, Qt::Key_Space);
    settle();
    EXPECT_FALSE(sim->isRunning()) << "space held it";
    EXPECT_TRUE(sim->isActive()) << "without ending it";

    const int before = sim->readValue(Rule::world(), QStringLiteral("frame")).toInt();
    QTest::keyClick(view, Qt::Key_Right);
    settle();
    EXPECT_EQ(sim->readValue(Rule::world(), QStringLiteral("frame")).toInt(), before + 1)
        << "the right arrow advances exactly one frame";

    QTest::keyClick(view, Qt::Key_Space);
    settle();
    EXPECT_TRUE(sim->isRunning()) << "and space lets it go again";

    QTest::keyClick(view, Qt::Key_Escape);
    settle();
    EXPECT_FALSE(sim->isActive()) << "escape ends the run";
    EXPECT_FALSE(window.findChild<FullScreenView *>())
        << "and the window goes with it, back to the editor";

    window.close();
}

TEST(FullScreenRun, ClosesIfItIsSwitchedOffMidRun)
{
    MainWindow window;
    window.resize(1200, 800);
    window.show();
    settle();

    auto *scene = window.findChild<CanvasScene *>();
    auto *sim = window.findChild<SimulationController *>();
    ASSERT_TRUE(oneBody(scene));

    QCheckBox *full = checkNamed(&window, QStringLiteral("Full Screen"));
    ASSERT_TRUE(full);
    full->setChecked(true);
    sim->start();
    settle();
    ASSERT_TRUE(window.findChild<FullScreenView *>());

    full->setChecked(false);
    settle();
    EXPECT_FALSE(window.findChild<FullScreenView *>()) << "the window goes";
    EXPECT_TRUE(sim->isActive()) << "and the run carries on in the editor";
    sim->stop();
    window.close();
}

// It is a viewer, but not a fixed one: the field can be dragged around and the
// wheel zooms, the way it does on the canvas.
TEST(FullScreenRun, PansAndZooms)
{
    MainWindow window;
    window.resize(1200, 800);
    window.show();
    settle();

    auto *scene = window.findChild<CanvasScene *>();
    auto *sim = window.findChild<SimulationController *>();
    ASSERT_TRUE(oneBody(scene));

    QCheckBox *full = checkNamed(&window, QStringLiteral("Full Screen"));
    ASSERT_TRUE(full);
    full->setChecked(true);
    sim->start();
    settle();

    auto *view = window.findChild<FullScreenView *>();
    ASSERT_TRUE(view);
    EXPECT_EQ(view->dragMode(), QGraphicsView::ScrollHandDrag)
        << "dragging moves the view, since nothing in the scene can be picked up";

    const qreal fitted = view->transform().m11();
    ASSERT_GT(fitted, 0.0);
    // The field sits in the middle of the screen, whole.
    const QPointF middle = view->mapToScene(view->viewport()->rect().center());
    // Within a pixel or two: a viewport with an odd width has no exact middle.
    const qreal slack = 3.0 / fitted;
    EXPECT_NEAR(middle.x(), scene->sceneRect().center().x(), slack) << "centred across";
    EXPECT_NEAR(middle.y(), scene->sceneRect().center().y(), slack) << "and down";

    const QPointF at(view->width() / 2.0, view->height() / 2.0);
    QWheelEvent zoomIn(at, view->mapToGlobal(at.toPoint()), QPoint(), QPoint(0, 120),
                       Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
    QCoreApplication::sendEvent(view->viewport(), &zoomIn);
    settle();
    EXPECT_GT(view->transform().m11(), fitted) << "a notch forward zooms in";

    QWheelEvent zoomOut(at, view->mapToGlobal(at.toPoint()), QPoint(), QPoint(0, -240),
                        Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
    QCoreApplication::sendEvent(view->viewport(), &zoomOut);
    settle();
    EXPECT_LT(view->transform().m11(), fitted) << "and back the other way, zooms out";

    QTest::keyClick(view, Qt::Key_0);
    settle();
    EXPECT_NEAR(view->transform().m11(), fitted, fitted * 1e-6)
        << "0 puts the whole field back on the screen";

    sim->stop();
    settle();
    window.close();
}
