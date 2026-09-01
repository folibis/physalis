#include "Icons.h"
#include "MainWindow.h"
#include "OptionsDialog.h"

#include <QApplication>
#include <gtest/gtest.h>
#include <QDialog>
#include <QMessageBox>
#include <QPixmap>
#include <cstdio>

// A QIcon can be non-null and still paint nothing, so compare the pixels
// against the application icon rather than trusting isNull().
static bool showsTheAppIcon(const QIcon &icon)
{
    if (icon.isNull())
        return false;
    const QImage got = icon.pixmap(32, 32).toImage();
    const QImage want = Icons::app().pixmap(32, 32).toImage();
    return !got.isNull() && got == want;
}

TEST(DialogIcon, Behaves)
{
    qApp->setApplicationName(QStringLiteral("Physalis"));
    qApp->setWindowIcon(Icons::app());

    MainWindow window;
    window.show();
    for (int i = 0; i < 30; ++i)
        QCoreApplication::processEvents();
    EXPECT_TRUE(showsTheAppIcon(window.windowIcon())) << "the main window";

    OptionsDialog options(OptionsDialog::Settings{}, &window);
    options.show();
    for (int i = 0; i < 20; ++i)
        QCoreApplication::processEvents();
    EXPECT_TRUE(showsTheAppIcon(options.windowIcon())) << "the Options dialog";
    options.close();

    QMessageBox box(QMessageBox::Warning, QStringLiteral("New Scene"),
                    QStringLiteral("The scene has unsaved changes."),
                    QMessageBox::Save | QMessageBox::Cancel, &window);
    box.show();
    for (int i = 0; i < 20; ++i)
        QCoreApplication::processEvents();
    EXPECT_TRUE(showsTheAppIcon(box.windowIcon())) << "the unsaved-changes warning";
    box.close();

    QDialog plain(&window);
    plain.show();
    for (int i = 0; i < 20; ++i)
        QCoreApplication::processEvents();
    EXPECT_TRUE(showsTheAppIcon(plain.windowIcon())) << "any other dialog";
    plain.close();

    window.close();
}
