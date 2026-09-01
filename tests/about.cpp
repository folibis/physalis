#include "AboutDialog.h"
#include "EngineRegistry.h"

#include <QApplication>
#include <QLabel>
#include <QTextDocumentFragment>
#include <QPushButton>
#include <gtest/gtest.h>

TEST(About, Behaves)
{
    AboutDialog dialog(QStringLiteral("0.1.48"));
    dialog.show();
    for (int i = 0; i < 25; ++i) QCoreApplication::processEvents();

    // Some labels carry markup, so compare what a reader actually sees.
    QStringList lines;
    for (QLabel *l : dialog.findChildren<QLabel *>())
        if (!l->text().isEmpty())
            lines << QTextDocumentFragment::fromHtml(l->text()).toPlainText();
    for (const QString &l : lines)

    EXPECT_TRUE(lines.join(QChar::LineFeed).contains(QStringLiteral("Physalis 0.1.48")))
        << "name and version";
    EXPECT_TRUE(lines.join(QChar::LineFeed).contains(QStringLiteral("ruslan@muhlinin.com")))
        << "the author line";
    EXPECT_TRUE(lines.join(QChar::LineFeed).contains(QStringLiteral("Box2D")))
        << "the plugins it found";

    bool hasImage = false;
    for (QLabel *l : dialog.findChildren<QLabel *>())
        hasImage = hasImage || !l->pixmap().isNull();
    EXPECT_TRUE(hasImage) << "the application image";

    // The email is a real link, not just text that looks like one.
    auto *author = dialog.findChild<QLabel *>(QStringLiteral("authorLabel"));
    EXPECT_TRUE(author && author->text().contains(QStringLiteral("mailto:")))
        << "the email is a mailto link";
    EXPECT_TRUE(author && author->openExternalLinks())
        << "and clicking it opens the mail client";

    auto *plugins = dialog.findChild<QLabel *>(QStringLiteral("pluginsLabel"));
    EXPECT_TRUE(plugins && plugins->text().contains(QStringLiteral("<b>Plugins:</b>")))
        << "the heading is bold";
    EXPECT_TRUE(plugins && (plugins->alignment() & Qt::AlignLeft))
        << "and the list is left aligned";

    auto *close = dialog.findChild<QPushButton *>(QStringLiteral("closeButton"));
    EXPECT_TRUE(close != nullptr) << "a Close button";
    EXPECT_TRUE(close && close->isVisible()) << "which is visible";

    // Every plugin it lists reports a version.
    for (const auto &plugin : physics::EngineRegistry::loadedPlugins())
        EXPECT_TRUE(!plugin.version.isEmpty())
            << "plugin reports a version: " << plugin.name.toStdString();

    dialog.grab().save(QStringLiteral("about.png"));
}
