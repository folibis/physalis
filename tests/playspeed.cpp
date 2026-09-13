#include "CanvasScene.h"
#include "MainWindow.h"
#include "PhysicsBody.h"
#include "RectangleItem.h"
#include "Rule.h"
#include "SimulationController.h"

#include <QApplication>
#include <QComboBox>
#include <QCoreApplication>
#include <gtest/gtest.h>

// The speed the run plays at: the same physics, watched faster or slower.

namespace {

void settle()
{
    for (int i = 0; i < 20; ++i)
        QCoreApplication::processEvents();
}

// One block with nothing to hit, which is all a run needs to count frames.
PhysicsBody *lonelyBlock(CanvasScene *scene)
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

// Wall time is fed the way the timer feeds it -- a slice per tick, twice as
// often as the world steps -- because a tick will only ever carry so much of a
// backlog, and handing over a tenth of a second in one go is not a tick.
void feed(SimulationController *sim, qreal wallSeconds)
{
    const qreal tick = 1.0 / 120.0;
    for (int i = 0; i < qRound(wallSeconds / tick); ++i)
        sim->advance(tick);
}

int framesAfter(SimulationController *sim, qreal wallSeconds)
{
    sim->start();
    feed(sim, wallSeconds);
    const int frames = sim->readValue(Rule::world(), QStringLiteral("frame")).toInt();
    sim->stop();
    return frames;
}

} // namespace

TEST(PlaySpeed, WallTimeIsMultipliedAndTheStepIsNot)
{
    MainWindow window;
    window.show();
    settle();
    auto *scene = window.findChild<CanvasScene *>();
    auto *sim = window.findChild<SimulationController *>();
    ASSERT_TRUE(lonelyBlock(scene));

    // A tenth of a second at sixty steps a second is six of them.
    EXPECT_EQ(framesAfter(sim, 0.1), 6) << "at the normal pace, real time is simulated time";

    sim->setSpeed(2.0);
    EXPECT_EQ(framesAfter(sim, 0.1), 12) << "twice the speed covers twice the world in the same"
                                            " tenth of a second";

    sim->setSpeed(0.5);
    EXPECT_EQ(framesAfter(sim, 0.1), 3) << "and half the speed, half of it";

    // The point of doing it this way: the solver never sees a longer step, so
    // a scene behaves the same at any speed -- only the clock differs.
    sim->setSpeed(4.0);
    sim->start();
    feed(sim, 0.1);
    EXPECT_NEAR(sim->readValue(Rule::world(), QStringLiteral("time")).toDouble(), 24.0 / 60.0, 1e-9)
        << "twenty-four steps of a sixtieth each, not four steps of a fifteenth";
    sim->stop();
}

TEST(PlaySpeed, TheToolbarPicksIt)
{
    MainWindow window;
    window.show();
    settle();
    auto *sim = window.findChild<SimulationController *>();

    QComboBox *speed = nullptr;
    for (QComboBox *combo : window.findChildren<QComboBox *>()) {
        if (combo->findText(QStringLiteral("×2")) >= 0)
            speed = combo;
    }
    ASSERT_TRUE(speed) << "the transport controls carry a speed chooser";
    EXPECT_EQ(speed->currentData().toDouble(), 1.0) << "a run plays at normal speed until asked";

    speed->setCurrentIndex(speed->findText(QStringLiteral("×4")));
    EXPECT_DOUBLE_EQ(sim->speed(), 4.0) << "picking one sets the pace of the run";

    speed->setCurrentIndex(speed->findText(QStringLiteral("×½")));
    EXPECT_DOUBLE_EQ(sim->speed(), 0.5) << "and slow motion is the same control";
}
