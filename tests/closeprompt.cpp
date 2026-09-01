#include "CanvasScene.h"
#include "MainWindow.h"

#include <QAbstractButton>
#include <QApplication>
#include <QMessageBox>
#include <QTimer>
#include <gtest/gtest.h>

namespace {

void settle()
{
    for (int i = 0; i < 20; ++i)
        QCoreApplication::processEvents();
}

// Answers the dialog the close puts up, since a test has no one to click it.
void answerWith(QMessageBox::StandardButton wanted, bool *appeared)
{
    QTimer::singleShot(0, [wanted, appeared] {
        auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
        if (!box)
            return;
        *appeared = true;
        if (QAbstractButton *button = box->button(wanted))
            button->click();
        else
            box->reject();
    });
}

}

// Closing a window with unsaved work asks first, and Cancel really cancels.
TEST(ClosePrompt, AsksBeforeLosingChanges)
{
    // The prompt stands aside for tests; this one is about the prompt itself.
    const QByteArray settings = qgetenv("PHYSALIS_SETTINGS");
    qunsetenv("PHYSALIS_SETTINGS");

    MainWindow window;
    window.show();
    settle();

    auto *scene = window.findChild<CanvasScene *>();
    scene->addRectangle(QPointF(0, 0));
    scene->notifyEdit(QStringLiteral("Add rectangle"));
    settle();
    ASSERT_TRUE(window.windowTitle().contains(QLatin1Char('*')))
        << "the scene counts as changed";

    bool asked = false;
    answerWith(QMessageBox::Cancel, &asked);
    window.close();
    settle();
    EXPECT_TRUE(asked) << "closing asked about the unsaved scene";
    EXPECT_TRUE(window.isVisible()) << "and Cancel kept the window open";

    asked = false;
    answerWith(QMessageBox::Discard, &asked);
    window.close();
    settle();
    EXPECT_TRUE(asked);
    EXPECT_FALSE(window.isVisible()) << "Discard closed it";

    if (!settings.isEmpty())
        qputenv("PHYSALIS_SETTINGS", settings);
}

// A window with nothing to lose closes without a word.
TEST(ClosePrompt, StaysQuietWhenSaved)
{
    const QByteArray settings = qgetenv("PHYSALIS_SETTINGS");
    qunsetenv("PHYSALIS_SETTINGS");

    MainWindow window;
    window.show();
    settle();
    ASSERT_FALSE(window.windowTitle().contains(QLatin1Char('*')));

    bool asked = false;
    answerWith(QMessageBox::Discard, &asked);
    window.close();
    settle();
    EXPECT_FALSE(asked) << "nothing unsaved, nothing to ask";
    EXPECT_FALSE(window.isVisible());

    if (!settings.isEmpty())
        qputenv("PHYSALIS_SETTINGS", settings);
}
