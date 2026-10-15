// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ruslan Muhlinin. See LICENSE.
#include "HelpWindow.h"

#include "AssetFolder.h"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QHBoxLayout>
#include <QListWidget>
#include <QSplitter>
#include <QTextBrowser>
#include <QTextStream>

namespace {

// The pages carry the website's front matter -- a title and a position between
// a pair of `---` lines. The title is worth having; the rest would be printed
// as if it were part of the text.
struct Page {
    QString title;
    int order = 1000;
    QString markdown;
};

Page readPage(const QString &path)
{
    Page page;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return page;

    QTextStream in(&file);
    QString line = in.readLine();
    if (line.trimmed() == QLatin1String("---")) {
        while (!in.atEnd()) {
            line = in.readLine();
            if (line.trimmed() == QLatin1String("---"))
                break;
            const int colon = line.indexOf(QLatin1Char(':'));
            if (colon < 0)
                continue;
            const QString field = line.left(colon).trimmed();
            const QString value = line.mid(colon + 1).trimmed();
            if (field == QLatin1String("title"))
                page.title = value;
            else if (field == QLatin1String("nav_order"))
                page.order = value.toInt();
        }
    } else {
        page.markdown = line + QLatin1Char('\n');
    }
    page.markdown += in.readAll();

    if (page.title.isEmpty())
        page.title = QFileInfo(path).completeBaseName();
    return page;
}

} // namespace

QString HelpWindow::documentationFolder()
{
    const QString found = assetFolder(QStringLiteral("docs"));
    if (found.isEmpty())
        return QString();
    if (QDir(found).entryList({QStringLiteral("*.md")}, QDir::Files).isEmpty())
        return QString();
    return found;
}

HelpWindow::HelpWindow(QWidget *parent) : QWidget(parent, Qt::Window)
{
    setWindowTitle(tr("Physalis Documentation"));
    resize(900, 700);

    m_folder = documentationFolder();
    m_contents = new QListWidget(this);
    m_contents->setMaximumWidth(220);
    m_view = new QTextBrowser(this);
    // So `images/overview.png` and the links between pages resolve against the
    // folder the pages are in rather than the working directory.
    m_view->setSearchPaths({m_folder});
    m_view->setOpenLinks(false);

    auto *split = new QSplitter(this);
    split->addWidget(m_contents);
    split->addWidget(m_view);
    split->setStretchFactor(1, 1);
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(split);

    // In the order the website puts them, which is the order they are meant to
    // be read in; anything without a position goes last, by name.
    QVector<QPair<Page, QString>> pages;
    for (const QFileInfo &file : QDir(m_folder).entryInfoList({QStringLiteral("*.md")},
                                                              QDir::Files, QDir::Name)) {
        pages.append({readPage(file.absoluteFilePath()), file.fileName()});
    }
    std::sort(pages.begin(), pages.end(), [](const auto &a, const auto &b) {
        if (a.first.order != b.first.order)
            return a.first.order < b.first.order;
        return a.second < b.second;
    });
    for (const auto &one : pages) {
        auto *item = new QListWidgetItem(one.first.title, m_contents);
        item->setData(Qt::UserRole, one.second);
    }

    connect(m_contents, &QListWidget::currentItemChanged, this,
            [this](QListWidgetItem *item, QListWidgetItem *) {
                if (item)
                    load(item->data(Qt::UserRole).toString());
            });
    // A link to another page opens it here; anything else is the browser's.
    connect(m_view, &QTextBrowser::anchorClicked, this, [this](const QUrl &url) {
        const QString target = url.toString();
        if (target.endsWith(QLatin1String(".md"), Qt::CaseInsensitive)) {
            showPage(target.section(QLatin1Char('/'), -1));
            return;
        }
        if (target.startsWith(QLatin1Char('#'))) {
            m_view->scrollToAnchor(target.mid(1));
            return;
        }
        QDesktopServices::openUrl(url);
    });
}

void HelpWindow::load(const QString &fileName)
{
    const Page page = readPage(QDir(m_folder).absoluteFilePath(fileName));
    m_view->setMarkdown(page.markdown);
    m_view->setProperty("page", fileName);
}

void HelpWindow::showPage(const QString &fileName)
{
    for (int i = 0; i < m_contents->count(); ++i) {
        if (m_contents->item(i)->data(Qt::UserRole).toString() == fileName) {
            m_contents->setCurrentRow(i);
            break;
        }
    }
    show();
    raise();
    activateWindow();
}
