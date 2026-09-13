#include "CanvasScene.h"
#include "Joint.h"
#include "MainWindow.h"
#include "PhysicsBody.h"
#include "RectangleItem.h"
#include "SimulationController.h"

#include <QApplication>
#include <QLabel>
#include <QVariantMap>
#include <gtest/gtest.h>

namespace {

void settle()
{
    for (int i = 0; i < 8; ++i)
        QCoreApplication::processEvents();
}

QString overlayText(MainWindow &window)
{
    QString all;
    for (QLabel *label : window.findChildren<QLabel *>()) {
        if (label->isVisible() && label->text().contains(QStringLiteral(" · ")))
            all += label->text() + QLatin1Char('\n');
    }
    return all;
}

} // namespace

// A joint has a Spring, a Limit and a Motor, and the switch on each is called
// "Enabled". A log entry naming only that names none of them -- and the name
// is stored with the entry, so one written before the heading was included
// keeps saying it. The heading is worked out again when the line is drawn, so
// an entry already sitting in a file reads properly without being added afresh.
TEST(LogLabel, Behaves)
{
    MainWindow window;
    window.resize(1100, 800);
    window.show();
    settle();

    auto *scene = window.findChild<CanvasScene *>();
    auto *sim = window.findChild<SimulationController *>();
    ASSERT_TRUE(scene && sim);

    auto *a = new RectangleItem;
    a->setRect(QRectF(0, 0, 200, 20));
    a->setPos(0, 0);
    a->setName(QStringLiteral("base"));
    scene->addItem(a);
    auto *b = new RectangleItem;
    b->setRect(QRectF(0, 0, 40, 40));
    b->setPos(80, -60);
    b->setName(QStringLiteral("arm"));
    scene->addItem(b);
    scene->notifyShapesChanged();
    scene->setEditorMode(EditorMode::Physics);

    scene->selectForPhysics(a, true);
    PhysicsBody *bodyA = scene->createBodyFromSelection();
    scene->clearPhysicsSelection();
    scene->selectForPhysics(b, true);
    PhysicsBody *bodyB = scene->createBodyFromSelection();
    scene->clearPhysicsSelection();
    Joint *joint = scene->createJoint(QStringLiteral("revolute"), bodyA, bodyB, 1, QVariantMap());
    ASSERT_TRUE(joint != nullptr);

    // An entry as an older file carries it: the bare switch name, no heading.
    CanvasScene::Watch bare;
    bare.objectName = joint->name();
    bare.propertyKey = QStringLiteral("enableMotor");
    bare.label = QStringLiteral("Enabled");
    scene->addWatch(bare);

    sim->setEngineName(QStringLiteral("Box2D"));
    sim->start();
    sim->stepFrame();
    settle();

    const QString shown = overlayText(window);
    EXPECT_TRUE(shown.contains(QStringLiteral("Motor · Enabled")))
        << "the line says which switch it is" << " -- " << shown.toStdString();
    EXPECT_FALSE(shown.contains(QStringLiteral("· Enabled   ")) &&
                 !shown.contains(QStringLiteral("Motor · Enabled")))
        << "and not the bare word on its own";

    sim->stop();
}
