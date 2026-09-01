#include "Icons.h"
#include "MainWindow.h"

#include <QApplication>
#include <gtest/gtest.h>
#include <QIcon>
#include <QImage>
#include <QPixmap>
#include <cstdio>

static const char *kScratch = "";


TEST(Branding, Behaves)
{
    qApp->setApplicationName(QStringLiteral("Physalis"));
    qApp->setWindowIcon(Icons::app());

    const QIcon icon = Icons::app();
    EXPECT_TRUE(!icon.isNull()) << "the resource exists";

    // An SVG Qt cannot parse still yields a QIcon; it just paints nothing.
    for (int size : { 16, 32, 64, 256 }) {
        const QPixmap pm = icon.pixmap(size, size);
        QImage im = pm.toImage().convertToFormat(QImage::Format_ARGB32);
        int painted = 0;
        for (int y = 0; y < im.height(); ++y)
            for (int x = 0; x < im.width(); ++x)
                if (qAlpha(im.pixel(x, y)) > 8) ++painted;
        const int total = im.width() * im.height();
        EXPECT_TRUE(qMax(im.width(), im.height()) >= size && painted > total / 20) << QStringLiteral("renders at %1 px").arg(size).toUtf8().constData() << " -- " << (QStringLiteral("%1x%2, %3% painted")
                  .arg(im.width()).arg(im.height())
                  .arg(total ? painted * 100 / total : 0)).toStdString();
        if (size == 256)
            im.save(QString::fromLatin1(kScratch) + QStringLiteral("appicon.png"));
    }

    MainWindow window;
    window.resize(1200, 800);
    window.show();
    for (int i = 0; i < 30; ++i)
        QCoreApplication::processEvents();

    EXPECT_TRUE(window.windowTitle().contains(QStringLiteral("Physalis"))) << "is titled Physalis" << " -- " << (window.windowTitle()).toStdString();
    EXPECT_TRUE(!window.windowTitle().contains(QStringLiteral("Shape Editor"))) << "no longer says Shape Editor";
    EXPECT_TRUE(!window.windowIcon().isNull()) << "carries the icon";
    window.grab().save(QString::fromLatin1(kScratch) + QStringLiteral("renamed.png"));

    window.close();
}
