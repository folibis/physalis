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

// Position, angle and speed are what the solver produces. Nothing outside a
// run can answer them, and the row's own getter returns nothing on purpose --
// so for as long as they were offered outside one they read a flat zero, which
// looks like a measurement and is not one. They are offered while a run is
// going, and they carry what it says.
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
    ASSERT_TRUE(scene->createBodyFromSelection() != nullptr);
    scene->clearPhysicsSelection();
    scene->selectForPhysics(shape);
    settle();

    EXPECT_TRUE(editorFor(panel, "Position X") == nullptr)
        << "nothing running, so nothing measured is offered";

    sim->setEngineName(QStringLiteral("Box2D"));
    sim->start();
    sim->stepFrame();
    settle();

    auto *x = qobject_cast<QDoubleSpinBox *>(editorFor(panel, "Position X"));
    auto *y = qobject_cast<QDoubleSpinBox *>(editorFor(panel, "Position Y (down is positive)"));
    ASSERT_TRUE(x && y) << "a run offers them";
    EXPECT_NEAR(x->value(), 300.0, 5.0)
        << "and they say where the body is, not zero" << " -- read " << x->value();
    EXPECT_LT(y->value(), -100.0) << "read " << y->value();

    sim->stop();
    settle();
    EXPECT_TRUE(editorFor(panel, "Position X") == nullptr)
        << "and they go again when it ends";
}
