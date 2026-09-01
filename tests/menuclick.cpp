#include "MainWindow.h"
#include "CanvasScene.h"
#include "SceneFixtures.h"
#include "ShapeItem.h"
#include "SceneSerializer.h"

#include <QApplication>
#include <gtest/gtest.h>
#include <QContextMenuEvent>
#include <QLabel>
#include <QMenu>
#include <QTableWidget>
#include <QTimer>
#include <cstdio>


TEST(MenuClick, Behaves)
{
    MainWindow window;
    window.resize(1400, 850);
    window.show();
    for (int i = 0; i < 30; ++i)
        QCoreApplication::processEvents();

    auto *scene = window.findChild<CanvasScene *>();
    QString error;
    Fixtures::buildCart(scene);
    scene->setEditorMode(EditorMode::Edit);
    scene->selectShape(scene->shapes().first());
    for (int i = 0; i < 30; ++i)
        QCoreApplication::processEvents();

    QWidget *rotationCell = nullptr;
    for (QTableWidget *table : window.findChildren<QTableWidget *>()) {
        for (int r = 0; r < table->rowCount(); ++r) {
            QWidget *cell = table->cellWidget(r, 0);
            auto *label = cell ? cell->findChild<QLabel *>() : nullptr;
            if (label && label->text() == QLatin1String("Rotation"))
                rotationCell = cell;
        }
    }
    ASSERT_TRUE(rotationCell != nullptr) << "the property table has a Rotation row";

    // Right-click in the empty part of the cell, past the end of the text.
    const QPoint farRight(rotationCell->width() - 6, rotationCell->height() / 2);

    QMenu *seen = nullptr;
    QStringList items;
    QTimer::singleShot(0, [&seen, &items] {
        for (QWidget *w : QApplication::topLevelWidgets())
            if (auto *m = qobject_cast<QMenu *>(w))
                if (m->isVisible()) {
                    seen = m;
                    for (QAction *a : m->actions())
                        items << (a->isSeparator() ? QStringLiteral("---")
                                                   : a->text());
                    m->close();
                }
    });
    QContextMenuEvent event(QContextMenuEvent::Mouse, farRight,
                            rotationCell->mapToGlobal(farRight));
    QApplication::sendEvent(rotationCell, &event);
    for (int i = 0; i < 20; ++i)
        QCoreApplication::processEvents();

    EXPECT_TRUE(seen != nullptr) << "a menu opened";
    if (seen) {
        EXPECT_TRUE(items.contains(QStringLiteral("Add to Log"))) << "it offers Add to Log" << " -- " << (items.join(QStringLiteral(" / "))).toStdString();
    }

    window.close();
}
