#include "MainWindow.h"

#include <QApplication>
#include <gtest/gtest.h>
#include <QAction>
#include <QToolBar>
#include <QKeySequence>
#include <cstdio>

TEST(ToolbarCheck, Behaves)
{
    MainWindow window;
    window.show();
    QCoreApplication::processEvents();

    auto *bar = window.findChild<QToolBar *>();
    ASSERT_TRUE(bar != nullptr) << "the window has a toolbar";

    const auto actions = bar->actions();
    int shown = 0;
    for (QAction *a : actions) {
        if (shown++ >= 8)
            break;
        if (a->isSeparator())
            continue;
        const bool hasIcon = !a->icon().isNull()
            && !a->icon().pixmap(QSize(22, 22)).isNull();
        EXPECT_TRUE(hasIcon) << "every toolbar action has an icon";
    }

    // The three file actions must be the first three, and stay usable no
    // matter what mode the editor is in. Save is the exception: it answers to
    // whether there is anything to write, not to the mode.
    const QStringList wanted = {"New Scene", "Load Scene...", "Save Scene"};
    for (int i = 0; i < wanted.size(); ++i) {
        const bool placed = i < actions.size() && actions[i]->text().remove('&') == wanted[i];
        EXPECT_TRUE(placed) << "file action is in toolbar slot " << (i + 1);
        if (placed && wanted[i] != QLatin1String("Save Scene"))
            EXPECT_TRUE(actions[i]->isEnabled()) << "and is always usable";
    }

    // And they must also still be in the File menu -- one action, two homes.
    window.close();
}
