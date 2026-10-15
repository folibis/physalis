// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ruslan Muhlinin. See LICENSE.
#pragma once

#include <QWidget>

class QTextBrowser;
class QListWidget;

// The documentation, read from the `docs` folder beside the program. The pages
// are the same Markdown the website is built from -- Qt renders it as it
// stands, so there is nothing to convert and nothing to keep in step.
class HelpWindow : public QWidget
{
    Q_OBJECT

public:
    // Where the pages are, or an empty string where there are none.
    static QString documentationFolder();

    explicit HelpWindow(QWidget *parent = nullptr);

    // Shows the window and opens one page by its file name.
    void showPage(const QString &fileName);

private:
    void load(const QString &fileName);

    QListWidget *m_contents = nullptr;
    QTextBrowser *m_view = nullptr;
    QString m_folder;
};
