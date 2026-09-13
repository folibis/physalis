#include "CanvasScene.h"
#include "MainWindow.h"
#include "PhysicsBody.h"
#include "PropertyPanel.h"
#include "RectangleItem.h"
#include "ShapeItem.h"

#include <QApplication>
#include <QCheckBox>
#include <QLabel>
#include <QTabWidget>
#include <QTableWidget>
#include <gtest/gtest.h>

namespace {

void settle()
{
    for (int i = 0; i < 8; ++i)
        QCoreApplication::processEvents();
}

QStringList tabsOf(PropertyPanel *panel)
{
    QStringList names;
    if (auto *tabs = panel->findChild<QTabWidget *>()) {
        for (int i = 0; i < tabs->count(); ++i)
            names << tabs->tabText(i);
    }
    return names;
}

// The editor beside the row labelled `label`, wherever in the panel it sits.
QCheckBox *checkboxFor(PropertyPanel *panel, const QString &label)
{
    for (QTableWidget *table : panel->findChildren<QTableWidget *>()) {
        for (int r = 0; r < table->rowCount(); ++r) {
            QWidget *nameCell = table->cellWidget(r, 0);
            if (!nameCell)
                continue;
            bool matched = false;
            for (QLabel *text : nameCell->findChildren<QLabel *>())
                matched = matched || text->text() == label;
            if (!matched)
                continue;
            if (auto *box = qobject_cast<QCheckBox *>(table->cellWidget(r, 1)))
                return box;
        }
    }
    return nullptr;
}

} // namespace

// Ticking Sensor turns a shape into a different kind of thing: an area that
// reports what enters it, for which nearly nothing in the physics vocabulary
// applies. That is a different pane, not a shorter list of rows -- and the
// swap has to happen when the box is ticked, not the next time the selection
// changes. Both ways round.
TEST(SensorPane, Behaves)
{
    MainWindow window;
    window.resize(1200, 800);
    window.show();
    settle();

    auto *scene = window.findChild<CanvasScene *>();
    auto *panel = window.findChild<PropertyPanel *>();
    ASSERT_TRUE(scene && panel);

    auto *shape = new RectangleItem;
    shape->setRect(QRectF(0, 0, 80, 40));
    shape->setName(QStringLiteral("gate"));
    scene->addItem(shape);
    scene->notifyShapesChanged();

    scene->setEditorMode(EditorMode::Physics);
    scene->selectForPhysics(shape, true);
    ASSERT_TRUE(scene->createBodyFromSelection() != nullptr);
    scene->clearPhysicsSelection();
    scene->selectForPhysics(shape);
    settle();

    const QStringList solid = tabsOf(panel);
    EXPECT_TRUE(solid.contains(QStringLiteral("Collision")))
        << "a solid shape gets the physics pane" << " -- " << solid.join(", ").toStdString();

    QCheckBox *tick = checkboxFor(panel, QStringLiteral("Sensor"));
    ASSERT_TRUE(tick != nullptr) << "the physics pane offers a Sensor box";
    tick->setChecked(true);
    settle();

    EXPECT_TRUE(shape->part().params["isSensor"].toBool()) << "the flag is set";
    const QStringList asSensor = tabsOf(panel);
    EXPECT_TRUE(asSensor.contains(QStringLiteral("Sensor")))
        << "and the sensor pane takes over straight away"
        << " -- " << asSensor.join(", ").toStdString();

    // Back again: the box on the sensor pane is the way out of it.
    QCheckBox *untick = checkboxFor(panel, QStringLiteral("Sensor"));
    ASSERT_TRUE(untick != nullptr) << "the sensor pane offers it too";
    untick->setChecked(false);
    settle();

    EXPECT_FALSE(shape->part().params["isSensor"].toBool()) << "the flag is cleared";
    const QStringList backToSolid = tabsOf(panel);
    EXPECT_EQ(backToSolid, solid) << "and the pane it had before comes back"
                                  << " -- " << backToSolid.join(", ").toStdString();
}
