#include "CanvasScene.h"
#include "PhysicsBody.h"
#include "MainWindow.h"
#include "RectangleItem.h"
#include "ShapeItem.h"

#include <QAction>
#include <QApplication>
#include <gtest/gtest.h>
#include <QLabel>
#include <QTabWidget>
#include <QTableWidget>
#include <cstdio>

static void settle() { for (int i = 0; i < 25; ++i) QCoreApplication::processEvents(); }

static QStringList visibleRows(MainWindow &window)
{
    QStringList rows;
    for (QTabWidget *tabs : window.findChildren<QTabWidget *>())
        for (int t = 0; t < tabs->count(); ++t) {
            tabs->setCurrentIndex(t);
            settle();
            for (QTableWidget *table : tabs->widget(t)->findChildren<QTableWidget *>())
                for (int r = 0; r < table->rowCount(); ++r) {
                    QWidget *cell = table->cellWidget(r, 0);
                    auto *text = cell ? cell->findChild<QLabel *>() : nullptr;
                    if (text) rows << text->text();
                }
        }
    return rows;
}

TEST(SensorPane, Behaves)
{
    MainWindow window;
    window.resize(1200, 800);
    window.show();
    settle();
    auto *scene = window.findChild<CanvasScene *>();
    scene->setEditorMode(EditorMode::Physics);

    auto *zone = new RectangleItem;
    zone->setRect(QRectF(0, 0, 200, 80));
    zone->setName(QStringLiteral("zone"));
    scene->addItem(zone);
    scene->notifyShapesChanged();
    scene->selectForPhysics(zone, true);
    settle();
    window.findChild<QAction *>(QStringLiteral("actionCreateBody"))->trigger();
    zone->part().isSensor = true;
    zone->part().enableSensorEvents = true;
    scene->clearPhysicsSelection();
    scene->selectForPhysics(zone, true);
    settle();

    const QStringList rows = visibleRows(window);

    for (const char *gone : { "Friction", "Restitution", "Rolling Resistance",
                              "Tangent Speed (m/s)", "Density (kg/m²)",
                              "Velocity X (m/s)", "Gravity Scale", "Bullet (fast CCD)",
                              "Contact Events", "Hit Events" })
        EXPECT_TRUE(!rows.contains(QString::fromUtf8(gone))) << QStringLiteral("no %1").arg(QString::fromUtf8(gone)).toUtf8().constData();

    for (const char *kept : { "Name", "Enabled", "Movement", "Is In Group",
                              "Notices Groups" })
        EXPECT_TRUE(rows.contains(QString::fromLatin1(kept))) << QStringLiteral("has %1").arg(QString::fromLatin1(kept)).toUtf8().constData();

    auto *block = new RectangleItem;
    block->setRect(QRectF(0, 0, 60, 60));
    block->setPos(300, 0);
    block->setName(QStringLiteral("block"));
    scene->addItem(block);
    scene->notifyShapesChanged();
    scene->clearPhysicsSelection();
    scene->selectForPhysics(block, true);
    settle();
    window.findChild<QAction *>(QStringLiteral("actionCreateBody"))->trigger();
    scene->clearPhysicsSelection();
    scene->selectForPhysics(block, true);
    settle();
    const QStringList solid = visibleRows(window);
    EXPECT_TRUE(solid.contains(QStringLiteral("Friction"))) << "friction is still there for a body";
    EXPECT_TRUE(solid.contains(QString::fromUtf8("Density (kg/m²)")))
        << "and so is density";

    window.close();
}
