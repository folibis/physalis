#include "OptionsDialog.h"

#include <QApplication>
#include <QGroupBox>
#include <QGridLayout>
#include <QScreen>
#include <QToolButton>
#include <gtest/gtest.h>

// The dialog used to take a share of the window it was opened from -- 44% of
// its width, 93.5% of its height -- so on a maximised window it opened nearly
// full height whatever was on the page. Every tab is a scroll area; it never
// needed to be that tall. It is sized to what it holds, bounded by the screen.
TEST(OptionsSize, Behaves)
{
    OptionsDialog::Settings settings;

    QWidget big;                      // stands in for a large main window
    big.resize(2400, 1300);

    OptionsDialog dialog(settings, &big);
    dialog.show();
    for (int i = 0; i < 8; ++i)
        QCoreApplication::processEvents();

    EXPECT_LT(dialog.width(), 1000)
        << "not a share of the window it came from" << " -- " << dialog.width();
    EXPECT_LT(dialog.height(), 800) << "nor of its height" << " -- " << dialog.height();

    if (QScreen *screen = QGuiApplication::primaryScreen()) {
        const QSize room = screen->availableGeometry().size();
        EXPECT_LE(dialog.height(), room.height()) << "and never taller than the screen";
        EXPECT_LE(dialog.width(), room.width());
    }

    // A colour group lays its swatches across rather than one to a line: five
    // body colours down the page is five lines of mostly empty dialog.
    QGroupBox *colours = dialog.findChild<QGroupBox *>(QStringLiteral("bodyColorsGroup"));
    ASSERT_TRUE(colours != nullptr);
    auto *grid = qobject_cast<QGridLayout *>(colours->layout());
    ASSERT_TRUE(grid != nullptr) << "the swatches are laid out in a grid";

    int swatches = 0;
    for (QToolButton *button : colours->findChildren<QToolButton *>())
        swatches += button->maximumWidth() <= 60 ? 1 : 0;
    ASSERT_GE(swatches, 4) << "there are several colours to fold";
    EXPECT_LT(grid->rowCount(), swatches)
        << "so they take fewer rows than there are of them"
        << " -- " << grid->rowCount() << " rows for " << swatches;
}
