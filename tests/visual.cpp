// A visual check of the whole window: every widget must be where it belongs,
// at a sane size, and actually drawing something. Structural tests (does the
// object exist, does the action fire) miss all of that -- a panel can exist,
// be connected, and still be collapsed to nothing or covering the canvas.
#include "MainWindow.h"
#include "CanvasScene.h"
#include "SceneFixtures.h"
#include "RulerWidget.h"
#include "SceneSerializer.h"

#include <QApplication>
#include <gtest/gtest.h>

// Streams a QString into a failure message.
#include "QtGTest.h"
#include <QDockWidget>
#include <QGraphicsView>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QPainter>
#include <QPixmap>
#include <QStatusBar>
#include <QTabWidget>
#include <QToolBar>
#include <cstdio>

static const char *kScratch = "";

// How much a widget actually draws: anything darker than near-white counts.
// A collapsed or blank panel scores almost nothing.
static int inkedPixels(const QPixmap &pixmap)
{
    const QImage image = pixmap.toImage();
    int n = 0;
    for (int y = 0; y < image.height(); ++y)
        for (int x = 0; x < image.width(); ++x) {
            const QColor c = image.pixelColor(x, y);
            if (c.red() < 220 || c.green() < 220 || c.blue() < 220)
                ++n;
        }
    return n;
}

static void checkWidget(QWidget *window, QWidget *w, const QString &name,
                        int minWidth, int minHeight, int minInk)
{
    if (!w) {
        EXPECT_TRUE(false) << name + " exists";
        return;
    }
    const QString size = QStringLiteral("(%1x%2)").arg(w->width()).arg(w->height());
    EXPECT_TRUE(w->width() >= minWidth && w->height() >= minHeight) << name + " is big enough" << " -- " << (size).toStdString();
    if (minInk > 0) {
        const int ink = inkedPixels(w->grab());
        EXPECT_TRUE(ink >= minInk) << name + " draws something" << " -- " << (QStringLiteral("%1 inked").arg(ink)).toStdString();
    }
    Q_UNUSED(window);
}

TEST(Visual, Behaves)
{
    MainWindow window;
    window.resize(1400, 850);
    window.show();
    for (int i = 0; i < 40; ++i)
        QCoreApplication::processEvents();

    auto *scene = window.findChild<CanvasScene *>();
    QString error;
    Fixtures::buildCart(scene);
    for (int i = 0; i < 40; ++i)
        QCoreApplication::processEvents();

    auto *view = window.findChild<QGraphicsView *>("canvasView");
    auto *dock = window.findChild<QDockWidget *>("propertyDock");
    auto *bar = window.findChild<QToolBar *>("toolBar");
    auto *tabs = window.findChild<QTabWidget *>("sidePanel");

    EXPECT_TRUE(view && view->width() > window.width() * 0.6) << "canvas fills most of the window" << " -- " << (QStringLiteral("canvas %1 of %2 px wide")
              .arg(view ? view->width() : 0).arg(window.width())).toStdString();
    EXPECT_TRUE(dock && dock->width() > 150 && dock->width() < window.width() * 0.4) << "dock takes a quarter, not the lot" << " -- " << (QStringLiteral("dock %1 px").arg(dock ? dock->width() : 0)).toStdString();
    EXPECT_TRUE(view && dock && dock->mapTo(&window, QPoint(0, 0)).x()
              > view->mapTo(&window, QPoint(0, 0)).x()) << "dock is on the right of the canvas";
    EXPECT_TRUE(bar && bar->width() > window.width() * 0.9) << "toolbar spans the window";
    EXPECT_TRUE(window.menuBar()->actions().size() == 4)
        << "menu bar has its four menus -- got "
        << window.menuBar()->actions().size();
    EXPECT_TRUE(!window.findChild<QStatusBar *>()->findChildren<QLabel *>().isEmpty()) << "status bar carries the help text";

    checkWidget(&window, view, QStringLiteral("canvas"), 600, 400, 5000);
    checkWidget(&window, window.findChild<RulerWidget *>("topRuler"),
                QStringLiteral("top ruler"), 600, 20, 500);
    checkWidget(&window, window.findChild<RulerWidget *>("leftRuler"),
                QStringLiteral("left ruler"), 20, 400, 500);
    checkWidget(&window, bar, QStringLiteral("toolbar"), 600, 30, 800);
    checkWidget(&window, dock, QStringLiteral("dock"), 150, 400, 500);

    for (int i = 0; i < tabs->count(); ++i) {
        tabs->setCurrentIndex(i);
        for (int n = 0; n < 20; ++n)
            QCoreApplication::processEvents();
        QWidget *page = tabs->widget(i);
        checkWidget(&window, page, tabs->tabText(i), 150, 300, 300);
    }
    tabs->setCurrentIndex(0);

    const QStringList wanted = {QStringLiteral("menuFile"), QStringLiteral("menuEdit"),
                                QStringLiteral("menuShapes"), QStringLiteral("menuAddShape")};
    for (const QString &name : wanted) {
        auto *menu = window.findChild<QMenu *>(name);
        EXPECT_TRUE(menu && !menu->actions().isEmpty()) << name + " has entries" << " -- " << (QStringLiteral("%1 items").arg(menu ? menu->actions().size() : 0)).toStdString();
    }

    for (EditorMode mode : {EditorMode::Edit, EditorMode::Physics}) {
        scene->setEditorMode(mode);
        for (int n = 0; n < 30; ++n)
            QCoreApplication::processEvents();
        const QString name = mode == EditorMode::Edit ? QStringLiteral("Edit mode")
                                                      : QStringLiteral("Physics mode");
        EXPECT_TRUE(inkedPixels(view->grab()) > 5000) << name + " canvas draws the scene";
        EXPECT_TRUE(inkedPixels(bar->grab()) > 800) << name + " toolbar draws its tools";
        window.grab().save(QString::fromLatin1(kScratch)
                           + QStringLiteral("window_%1.png")
                                 .arg(mode == EditorMode::Edit ? "edit" : "physics"));
    }

    window.close();
}
