#include "CanvasScene.h"
#include "MainWindow.h"
#include "PhysicsBody.h"
#include "RectangleItem.h"
#include "SimulationController.h"
#include "ViewLayersCombo.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QMouseEvent>
#include <gtest/gtest.h>

// What a *run* shows, as a list of switches on the toolbar. The editor draws
// everything whatever they say -- a joint being placed cannot be hidden from
// the person placing it.

using RunLayer = CanvasScene::RunLayer;

namespace {

void settle()
{
    for (int i = 0; i < 20; ++i)
        QCoreApplication::processEvents();
}

// Clicks the entry with this label, the way a user does: in the open list.
void clickLayer(ViewLayersCombo *layers, const QString &label)
{
    QAbstractItemView *list = layers->view();
    for (int row = 0; row < layers->count(); ++row) {
        if (layers->itemText(row) != label)
            continue;
        const QPoint at = list->visualRect(layers->model()->index(row, 0)).center();
        QMouseEvent release(QEvent::MouseButtonRelease, at, list->viewport()->mapToGlobal(at),
                            Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QCoreApplication::sendEvent(list->viewport(), &release);
        break;
    }
    settle();
}

} // namespace

TEST(ViewLayers, EachSwitchIsOneOfTheRunsLayers)
{
    MainWindow window;
    window.show();
    settle();

    auto *scene = window.findChild<CanvasScene *>();
    auto *layers = window.findChild<ViewLayersCombo *>();
    ASSERT_TRUE(layers) << "the toolbar carries the list";

    scene->setEditorMode(EditorMode::Physics);
    settle();
    EXPECT_TRUE(layers->isVisible()) << "shown in Physics mode, with the transport controls";
    scene->setEditorMode(EditorMode::Edit);
    settle();
    EXPECT_FALSE(layers->isVisible()) << "and out of the way in Edit mode";
    scene->setEditorMode(EditorMode::Physics);
    settle();

    struct Layer { const char *label; RunLayer layer; };
    const Layer layerList[] = {
        {"Grid", RunLayer::Grid},
        {"Joints", RunLayer::Joints},
        {"Body axes", RunLayer::BodyAxes},
        {"Rays", RunLayer::Rays},
        {"Explosions", RunLayer::Explosions},
        {"Sleep shading", RunLayer::SleepShading},
    };

    for (const Layer &entry : layerList) {
        const QString label = QString::fromLatin1(entry.label);
        ASSERT_GE(layers->findText(label), 0) << "the list offers " << entry.label;
        EXPECT_TRUE(scene->runLayer(entry.layer)) << "everything starts on -- " << entry.label;

        clickLayer(layers, label);
        EXPECT_FALSE(scene->runLayer(entry.layer))
            << "clicking it turns the layer off -- " << entry.label;
        clickLayer(layers, label);
        EXPECT_TRUE(scene->runLayer(entry.layer))
            << "and clicking again turns it back on -- " << entry.label;
    }

    window.close();
}

// One click, one switch. The list swallows the click rather than letting the
// view act on it, which is what keeps it open for the next one -- setting three
// layers should not mean opening the list three times.
TEST(ViewLayers, AClickIsSwallowedSoTheListStaysOpen)
{
    MainWindow window;
    window.show();
    settle();
    window.findChild<CanvasScene *>()->setEditorMode(EditorMode::Physics);
    settle();

    auto *layers = window.findChild<ViewLayersCombo *>();
    ASSERT_TRUE(layers);
    ASSERT_GT(layers->count(), 0);

    QAbstractItemView *list = layers->view();
    const QPoint at = list->visualRect(layers->model()->index(0, 0)).center();
    QMouseEvent release(QEvent::MouseButtonRelease, at, list->viewport()->mapToGlobal(at),
                        Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    EXPECT_TRUE(QCoreApplication::sendEvent(list->viewport(), &release))
        << "the click was taken by the list itself, not passed on to close it";

    window.close();
}

// The whole point of the change: a layer switched off is switched off for the
// run, and for nothing else.
TEST(ViewLayers, NothingIsHiddenWhileEditing)
{
    CanvasScene scene;
    scene.setSimulationEngineName(QStringLiteral("Box2D"));

    auto *shape = new RectangleItem;
    shape->setRect(QRectF(0, 0, 60, 60));
    shape->setName(QStringLiteral("block"));
    scene.addItem(shape);
    scene.notifyShapesChanged();
    scene.setEditorMode(EditorMode::Physics);
    scene.selectForPhysics(shape, true);
    ASSERT_TRUE(scene.createBodyFromSelection());
    scene.clearPhysicsSelection();

    for (int i = 0; i < static_cast<int>(RunLayer::Count); ++i)
        scene.setRunLayer(static_cast<RunLayer>(i), false);

    for (int i = 0; i < static_cast<int>(RunLayer::Count); ++i) {
        EXPECT_TRUE(scene.layerVisible(static_cast<RunLayer>(i)))
            << "with nothing running, every layer is drawn -- layer " << i;
    }

    SimulationController sim(&scene, nullptr);
    sim.setEngineName(QStringLiteral("Box2D"));
    sim.start();
    ASSERT_TRUE(scene.simulationRunning());
    for (int i = 0; i < static_cast<int>(RunLayer::Count); ++i) {
        EXPECT_FALSE(scene.layerVisible(static_cast<RunLayer>(i)))
            << "and once it runs, the switches decide -- layer " << i;
    }

    scene.setRunLayer(RunLayer::Joints, true);
    EXPECT_TRUE(scene.layerVisible(RunLayer::Joints)) << "one at a time";
    EXPECT_FALSE(scene.layerVisible(RunLayer::Rays)) << "and only the one";
    sim.stop();
}
