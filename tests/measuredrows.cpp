#include "CanvasScene.h"
#include "MainWindow.h"
#include "PhysicsBody.h"
#include "PropertyPanel.h"
#include "RectangleItem.h"
#include "ShapeItem.h"
#include "SimulationController.h"

#include <QApplication>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QTableWidget>
#include <gtest/gtest.h>

namespace {

void settle()
{
    for (int i = 0; i < 8; ++i)
        QCoreApplication::processEvents();
}

// The editor against a row, by label, or nullptr when the row is not there.
QWidget *editorFor(PropertyPanel *panel, const char *label)
{
    for (QTableWidget *table : panel->findChildren<QTableWidget *>()) {
        for (int r = 0; r < table->rowCount(); ++r) {
            QWidget *cell = table->cellWidget(r, 0);
            if (!cell)
                continue;
            for (QLabel *text : cell->findChildren<QLabel *>()) {
                if (text->text() == QString::fromLatin1(label))
                    return table->cellWidget(r, 1);
            }
        }
    }
    return nullptr;
}

} // namespace

// Speed and mass are what the solver produces. Before a run they
// read what the body starts with -- asked of a world built and not stepped --
// and during one what the run says; never a flat zero standing in for either.
TEST(MeasuredRows, Behaves)
{
    MainWindow window;
    window.resize(1100, 800);
    window.show();
    settle();

    auto *scene = window.findChild<CanvasScene *>();
    auto *panel = window.findChild<PropertyPanel *>();
    auto *sim = window.findChild<SimulationController *>();
    ASSERT_TRUE(scene && panel && sim);

    auto *shape = new RectangleItem;
    shape->setRect(QRectF(0, 0, 40, 40));
    shape->setPos(300, -200);              // nowhere near the origin
    shape->setName(QStringLiteral("crate"));
    scene->addItem(shape);
    scene->notifyShapesChanged();
    scene->setEditorMode(EditorMode::Physics);
    scene->selectForPhysics(shape, true);
    PhysicsBody *crate = scene->createBodyFromSelection();
    ASSERT_TRUE(crate != nullptr);
    // Moving from the start, whatever the world's gravity happens to be.
    crate->props().params["velocityX"] = 10.0;
    scene->clearPhysicsSelection();
    scene->selectForPhysics(shape);
    settle();

    auto *mass = qobject_cast<QDoubleSpinBox *>(editorFor(panel, "Mass (kg)"));
    ASSERT_TRUE(mass) << "offered before the run";
    EXPECT_GT(mass->value(), 0.0) << "reading what the body starts with, not zero";
    EXPECT_TRUE(editorFor(panel, "Position X") == nullptr)
        << "where a body stands is edited on the canvas, not read here";

    sim->setEngineName(QStringLiteral("Box2D"));
    sim->start();
    for (int i = 0; i < 10; ++i)
        sim->stepFrame();
    settle();

    auto *speed = qobject_cast<QDoubleSpinBox *>(editorFor(panel, "Speed"));
    ASSERT_TRUE(speed) << "a run offers it";
    EXPECT_GT(speed->value(), 0.0) << "a moving crate reads as moving -- read " << speed->value();

    sim->stop();
    settle();
    EXPECT_TRUE(editorFor(panel, "Speed") != nullptr) << "and it stays when the run ends";
}
