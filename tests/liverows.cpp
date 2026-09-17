#include "CanvasScene.h"
#include "MainWindow.h"
#include "PhysicsBody.h"
#include "PropertyPanel.h"
#include "RectangleItem.h"
#include "SimulationController.h"

#include <QAbstractSpinBox>
#include <QAction>
#include <QApplication>
#include <QLabel>
#include <QLocale>
#include <gtest/gtest.h>

namespace {

void settle()
{
    for (int i = 0; i < 30; ++i)
        QCoreApplication::processEvents();
}

QLabel *rowLabel(PropertyPanel *panel, const QString &text)
{
    for (QLabel *label : panel->findChildren<QLabel *>()) {
        if (label->text() == text)
            return label;
    }
    return nullptr;
}

// The spin box on the same table row as a name label.
QAbstractSpinBox *rowEditor(QLabel *label)
{
    QWidget *cell = label->parentWidget();
    QWidget *viewport = cell ? cell->parentWidget() : nullptr;
    if (!viewport)
        return nullptr;
    QAbstractSpinBox *nearest = nullptr;
    for (QAbstractSpinBox *spin : viewport->findChildren<QAbstractSpinBox *>(Qt::FindDirectChildrenOnly)) {
        if (qAbs(spin->geometry().center().y() - cell->geometry().center().y()) < cell->height() / 2)
            nearest = spin;
    }
    return nearest;
}

} // namespace

// A reading such as Speed is there before the run starts, showing what the body
// starts with, and still answers a right-click during it -- the log is filled
// from that menu.
TEST(LiveRows, ReadingsCanBeLoggedBeforeAndDuringARun)
{
    MainWindow window;
    window.resize(1280, 800);
    window.show();
    settle();

    auto *scene = window.findChild<CanvasScene *>();
    auto *panel = window.findChild<PropertyPanel *>();
    ASSERT_TRUE(scene && panel);

    auto *shape = new RectangleItem;
    shape->setRect(QRectF(0, 0, 40, 40));
    scene->addItem(shape);
    scene->notifyShapesChanged();
    scene->setEditorMode(EditorMode::Physics);
    scene->selectForPhysics(shape, true);
    ASSERT_TRUE(scene->createBodyFromSelection());
    scene->clearPhysicsSelection();
    scene->selectForPhysics(shape, false);
    settle();

    QLabel *speed = rowLabel(panel, QObject::tr("Speed"));
    ASSERT_NE(speed, nullptr) << "no Speed row before the run";
    EXPECT_EQ(speed->contextMenuPolicy(), Qt::CustomContextMenu);
    ASSERT_NE(rowEditor(speed), nullptr);
    // Before a run a reading is what the object starts with.
    EXPECT_FALSE(rowEditor(speed)->text().isEmpty()) << "Speed shows nothing before the run";
    QLabel *mass = rowLabel(panel, QObject::tr("Mass (kg)"));
    ASSERT_NE(mass, nullptr);
    ASSERT_NE(rowEditor(mass), nullptr);
    EXPECT_GT(QLocale().toDouble(rowEditor(mass)->text()), 0.0) << "a body's starting mass reads as nothing";

    // The table carries the readings worth watching; the rest are for rules.
    for (const char *shown : {"Kinetic Energy (mJ)", "Contacts", "Joints", "Mass (kg)"})
        EXPECT_NE(rowLabel(panel, QObject::tr(shown)), nullptr) << "no row " << shown;
    for (const char *hidden : {"Bounds Left", "Centre of Mass X", "Last Hit Speed", "Position X", "Angle (deg)",
                               "Radius", "Things Inside", "Material Id"})
        EXPECT_EQ(rowLabel(panel, QObject::tr(hidden)), nullptr) << "row " << hidden << " is still shown";

    QAction *step = nullptr;
    for (QAction *action : window.findChildren<QAction *>()) {
        if (action->objectName() == QStringLiteral("actionStep"))
            step = action;
    }
    ASSERT_NE(step, nullptr);
    step->trigger();
    settle();

    speed = rowLabel(panel, QObject::tr("Speed"));
    ASSERT_NE(speed, nullptr) << "no Speed row during the run";
    EXPECT_TRUE(speed->isEnabled()) << "a disabled row gets no right-click, so nothing can be logged";
    EXPECT_FALSE(rowEditor(speed)->text().isEmpty()) << "the run did not fill the reading";

    QLabel *damping = rowLabel(panel, QObject::tr("Linear Damping"));
    ASSERT_NE(damping, nullptr);
    ASSERT_NE(rowEditor(damping), nullptr);
    EXPECT_FALSE(rowEditor(damping)->isEnabled()) << "a setting can still be edited during the run";

    window.findChild<SimulationController *>()->stop();
    window.close();
}
