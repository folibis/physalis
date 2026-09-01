#include "CanvasScene.h"
#include "MainWindow.h"
#include "RectangleItem.h"
#include "ShapeItem.h"

#include <QAction>
#include <QApplication>
#include <gtest/gtest.h>
#include <QGraphicsView>
#include <QMenu>
#include <QTimer>
#include <cstdio>

static void settle()
{
    for (int i = 0; i < 20; ++i)
        QCoreApplication::processEvents();
}

// The menu blocks in exec(), so it is inspected and closed from a timer.
static QStringList originItems(MainWindow &window, const QPoint &at,
                               QList<QAction *> *actionsOut)
{
    QStringList items;
    QTimer::singleShot(0, [&items, actionsOut] {
        for (QWidget *w : QApplication::topLevelWidgets()) {
            auto *menu = qobject_cast<QMenu *>(w);
            if (!menu || !menu->isVisible())
                continue;
            for (QAction *a : menu->actions()) {
                if (a->menu() && a->text().remove('&').startsWith(QStringLiteral("Move Origin"))) {
                    for (QAction *sub : a->menu()->actions()) {
                        items << sub->text().remove('&');
                        *actionsOut << sub;
                    }
                }
            }
            menu->close();
        }
    });
    auto *view = window.findChild<QGraphicsView *>(QStringLiteral("canvasView"));
    emit view->customContextMenuRequested(at);
    settle();
    return items;
}

// Opens the context menu and fires one Move Origin To entry while the menu is
// still alive -- its actions are destroyed with it, so they cannot be kept.
static void triggerOrigin(MainWindow &window, const QPoint &at, const QString &label)
{
    QTimer::singleShot(0, [label] {
        for (QWidget *w : QApplication::topLevelWidgets()) {
            auto *menu = qobject_cast<QMenu *>(w);
            if (!menu || !menu->isVisible())
                continue;
            for (QAction *a : menu->actions()) {
                if (!a->menu() || !a->text().remove('&').startsWith(QStringLiteral("Move Origin")))
                    continue;
                for (QAction *sub : a->menu()->actions())
                    if (sub->text().remove('&') == label)
                        sub->trigger();
            }
            menu->close();
        }
    });
    auto *view = window.findChild<QGraphicsView *>(QStringLiteral("canvasView"));
    emit view->customContextMenuRequested(at);
    settle();
}

TEST(OriginMenu, Behaves)
{
    MainWindow window;
    window.resize(1200, 800);
    window.show();
    settle();

    auto *scene = window.findChild<CanvasScene *>();
    scene->setEditorMode(EditorMode::Edit);
    auto *rect = qgraphicsitem_cast<RectangleItem *>(scene->addRectangle(QPointF(0, 0)));
    rect->setRect(QRectF(0, 0, 200, 100));
    scene->selectShape(rect);
    settle();

    auto *view = window.findChild<QGraphicsView *>(QStringLiteral("canvasView"));
    QList<QAction *> actions;
    const QStringList items = originItems(window, view->mapFromScene(rect->pos()), &actions);

    const char *wanted[] = { "Center", "Top", "Bottom", "Left", "Right",
                             "Top-Left Corner", "Top-Right Corner",
                             "Bottom-Left Corner", "Bottom-Right Corner" };
    for (const char *w : wanted)
        EXPECT_TRUE(items.contains(QString::fromLatin1(w))) << w;
    EXPECT_TRUE(!items.contains(QStringLiteral("Move Origin to Center"))) << "the old single action is gone";

    struct Expect { const char *label; QPointF at; };
    const Expect expected[] = {
        { "Center",              QPointF(100,  50) },
        { "Top",                 QPointF(100,   0) },
        { "Bottom",              QPointF(100, 100) },
        { "Left",                QPointF(  0,  50) },
        { "Right",               QPointF(200,  50) },
        { "Top-Left Corner",     QPointF(  0,   0) },
        { "Top-Right Corner",    QPointF(200,   0) },
        { "Bottom-Left Corner",  QPointF(  0, 100) },
        { "Bottom-Right Corner", QPointF(200, 100) },
    };
    for (const Expect &e : expected) {
        rect->setOrigin(QPointF(-1, -1));   // so a no-op is visible as a failure
        triggerOrigin(window, view->mapFromScene(rect->pos()),
                      QString::fromLatin1(e.label));
        const QPointF got = rect->origin();
        EXPECT_TRUE(QLineF(got, e.at).length() < 0.01) << e.label << " -- " << (QStringLiteral("(%1,%2)").arg(got.x()).arg(got.y())).toStdString();
    }

    window.close();
}
