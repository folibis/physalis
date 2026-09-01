#include "MainWindow.h"
#include "CanvasScene.h"
#include "SceneFixtures.h"
#include "ShapeItem.h"
#include "SceneSerializer.h"
#include "SimulationController.h"

#include <QApplication>
#include <gtest/gtest.h>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsSceneWheelEvent>
#include <QGraphicsView>
#include <QScrollBar>
#include <cstdio>

// Drags empty field and reports how far the view scrolled.
static QPoint dragEmptyField(CanvasScene *scene, QGraphicsView *view, const QPointF &emptyAt)
{
    const QPoint before(view->horizontalScrollBar()->value(),
                        view->verticalScrollBar()->value());

    const QPoint screenStart = view->viewport()->mapToGlobal(view->mapFromScene(emptyAt));

    QGraphicsSceneMouseEvent press(QEvent::GraphicsSceneMousePress);
    press.setScenePos(emptyAt);
    press.setScreenPos(screenStart);
    press.setButton(Qt::LeftButton);
    press.setButtons(Qt::LeftButton);
    QApplication::sendEvent(scene, &press);

    QGraphicsSceneMouseEvent move(QEvent::GraphicsSceneMouseMove);
    move.setScenePos(emptyAt + QPointF(60, 40));
    move.setScreenPos(screenStart + QPoint(60, 40));
    move.setButtons(Qt::LeftButton);
    QApplication::sendEvent(scene, &move);

    QGraphicsSceneMouseEvent release(QEvent::GraphicsSceneMouseRelease);
    release.setScenePos(emptyAt + QPointF(60, 40));
    release.setScreenPos(screenStart + QPoint(60, 40));
    release.setButton(Qt::LeftButton);
    QApplication::sendEvent(scene, &release);

    for (int i = 0; i < 10; ++i)
        QCoreApplication::processEvents();
    return QPoint(view->horizontalScrollBar()->value(),
                  view->verticalScrollBar()->value()) - before;
}

TEST(PanMode, Behaves)
{
    MainWindow window;
    window.resize(1000, 700);
    window.show();
    for (int i = 0; i < 30; ++i)
        QCoreApplication::processEvents();

    auto *scene = window.findChild<CanvasScene *>();
    auto *view = window.findChild<QGraphicsView *>(QStringLiteral("canvasView"));
    QString error;
    Fixtures::buildCart(scene);
    for (int i = 0; i < 20; ++i)
        QCoreApplication::processEvents();

    // Somewhere with no shape under it.
    const QPointF empty(-900, -260);

    scene->setEditorMode(EditorMode::Edit);
    for (int i = 0; i < 10; ++i)
        QCoreApplication::processEvents();
    const QPoint edit = dragEmptyField(scene, view, empty);
    EXPECT_TRUE(edit != QPoint(0, 0)) << "Edit mode" << " -- " << (QStringLiteral("scrolled (%1,%2)").arg(edit.x()).arg(edit.y())).toStdString();

    scene->setEditorMode(EditorMode::Physics);
    for (int i = 0; i < 10; ++i)
        QCoreApplication::processEvents();
    const QPoint physics = dragEmptyField(scene, view, empty);
    EXPECT_TRUE(physics != QPoint(0, 0)) << "Physics mode" << " -- " << (QStringLiteral("scrolled (%1,%2)").arg(physics.x()).arg(physics.y())).toStdString();

    // The case that actually mattered: panning while the simulation runs.
    auto *sim = window.findChild<SimulationController *>();
    sim->start();
    for (int frame = 0; frame < 30; ++frame) {
        sim->stepFrame();
        QCoreApplication::processEvents();
    }
    const QPoint duringRun = dragEmptyField(scene, view, empty);
    EXPECT_TRUE(duringRun != QPoint(0, 0)) << "Physics mode, simulation running" << " -- " << (QStringLiteral("scrolled (%1,%2)").arg(duringRun.x()).arg(duringRun.y())).toStdString();
    EXPECT_TRUE(sim->isActive()) << "and the run is genuinely active";
    EXPECT_TRUE(!scene->selectionAllowed()) << "while selection stays off";
    sim->stop();
    for (int i = 0; i < 10; ++i)
        QCoreApplication::processEvents();

    // And the wheel: Shift+wheel zooms, and it must not be mode-gated either.
    for (EditorMode mode : { EditorMode::Edit, EditorMode::Physics }) {
        scene->setEditorMode(mode);
        for (int i = 0; i < 10; ++i)
            QCoreApplication::processEvents();
        const qreal before = scene->currentScale();
        QGraphicsSceneWheelEvent zoom(QEvent::GraphicsSceneWheel);
        zoom.setScenePos(empty);
        zoom.setDelta(120);
        zoom.setModifiers(Qt::ShiftModifier);
        QApplication::sendEvent(scene, &zoom);
        for (int i = 0; i < 10; ++i)
            QCoreApplication::processEvents();
        EXPECT_TRUE(!qFuzzyCompare(scene->currentScale(), before))
            << (mode == EditorMode::Edit ? "Shift+wheel zooms in Edit mode"
                                         : "Shift+wheel zooms in Physics mode")
            << " -- "
            << QStringLiteral("%1% -> %2%").arg(before).arg(scene->currentScale()).toStdString();
    }

    window.close();
}
