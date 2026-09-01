#include "MainWindow.h"
#include "CanvasScene.h"
#include "PolygonItem.h"
#include "RectangleItem.h"
#include "ShapeItem.h"

#include <QApplication>
#include <gtest/gtest.h>
#include <QAction>
#include <QGraphicsView>
#include <QMenu>
#include <QTimer>
#include <cstdio>

// The context menu blocks in exec(), so it is closed from a timer and its
// items read from there.
static QStringList openMenuItems(MainWindow &window, const QPoint &viewPos)
{
    QStringList items;
    QTimer::singleShot(0, [&items] {
        for (QWidget *w : QApplication::topLevelWidgets()) {
            auto *menu = qobject_cast<QMenu *>(w);
            if (!menu || !menu->isVisible())
                continue;
            for (QAction *a : menu->actions())
                items << (a->isSeparator() ? QStringLiteral("---") : a->text().remove('&'));
            menu->close();
        }
    });
    auto *view = window.findChild<QGraphicsView *>(QStringLiteral("canvasView"));
    emit view->customContextMenuRequested(viewPos);
    for (int i = 0; i < 20; ++i)
        QCoreApplication::processEvents();
    return items;
}

TEST(EditMenu, Behaves)
{
    MainWindow window;
    window.resize(1200, 800);
    window.show();
    for (int i = 0; i < 30; ++i)
        QCoreApplication::processEvents();

    auto *scene = window.findChild<CanvasScene *>();
    scene->setEditorMode(EditorMode::Edit);
    ShapeItem *rect = scene->addRectangle(QPointF(0, 0));
    scene->selectShape(rect);
    for (int i = 0; i < 20; ++i)
        QCoreApplication::processEvents();

    auto *view = window.findChild<QGraphicsView *>(QStringLiteral("canvasView"));
    const QPoint at = view->mapFromScene(rect->pos());
    const QStringList items = openMenuItems(window, at);
    for (const char *wanted : { "Duplicate", "Flip Horizontally", "Flip Vertically", "Delete" })
        EXPECT_TRUE(items.contains(QString::fromLatin1(wanted))) << wanted;

    const int before = scene->shapes().size();
    window.duplicateShapeForTest(rect);
    EXPECT_TRUE(scene->shapes().size() == before + 1) << "Duplicate adds a shape" << " -- " << (QStringLiteral("%1 -> %2").arg(before).arg(scene->shapes().size())).toStdString();
    ShapeItem *copy = scene->activeItem();
    EXPECT_TRUE(copy && copy != rect && copy->name() != rect->name()) << "the copy is selected and separately named" << " -- " << (copy ? copy->name() : QStringLiteral("(none)")).toStdString();
    EXPECT_TRUE(copy && copy->pos() != rect->pos()) << "and is offset from the original";

    // Flipping a polygon must mirror its points about the origin.
    QPolygonF triangle;
    triangle << QPointF(0, 0) << QPointF(60, 0) << QPointF(0, 40);
    auto *poly = new PolygonItem(triangle, true);
    scene->addItem(poly);
    scene->notifyShapesChanged();
    poly->setOrigin(QPointF(30, 20));
    const QPointF firstBefore = poly->nodePosition(1);
    window.flipShapeForTest(poly, true);
    const QPointF firstAfter = poly->nodePosition(1);
    EXPECT_TRUE(qFuzzyCompare(firstAfter.x(), 2.0 * poly->origin().x() - firstBefore.x())) << "Flip Horizontally mirrors about the origin";
    EXPECT_TRUE(qFuzzyCompare(firstAfter.y(), firstBefore.y())) << "and leaves the other axis alone";

    window.flipShapeForTest(poly, true);
    EXPECT_TRUE(qFuzzyCompare(poly->nodePosition(1).x(),
                                                            firstBefore.x())) << "flipping twice returns the shape";

    window.close();
}
