// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ruslan Muhlinin. See LICENSE.
#pragma once

#include <QColor>
#include <QObject>
#include <QPalette>
#include <QString>
#include <QVector>

// How the application's own widgets look: one style, one palette and one
// stylesheet for every platform, from a colour scheme and a control style
// chosen independently of each other. The canvas is not part of it -- the
// field's background is the scene's, saved in its file.
//
// Neither is written here. Both are read at startup from the `themes` folder
// beside the program -- one JSON file per scheme and per style, over one shared
// style sheet -- so adding either is adding a file, not a line of C++. The
// format and the folders searched are in themes/README.md.
//
// A colour a widget needs is a palette role where one fits and a field of
// Colors where none does; nothing outside this file writes a hex literal for
// the chrome. Widgets that bake a colour into something -- a per-panel sheet,
// a painted pixmap -- listen to changed() and do it again.
class Theme : public QObject
{
    Q_OBJECT

public:
    struct Choice {
        QString key;    // what the settings file stores
        QString label;  // what Options shows
    };

    struct Colors {
        QColor surface;         // the toolbar, menu bar and status bar
        QColor hover;           // under the pointer
        QColor accent;          // headings, a watched property, links
        QColor accentSoft;      // a selected tab, a checked tool
        QColor borderStrong;    // a field under the pointer
        QColor mutedText;       // notes and secondary labels
        QColor faintText;       // the same, on something switched off
        QColor glyph;           // painted marks on buttons
        QColor glyphDisabled;
        QColor error;           // the border of an unfinished rule
        QColor errorText;
        QColor errorBackground;
        QColor errorHeader;
        QColor errorOff;        // the same, on a rule switched off
        QColor errorTextOff;
        QColor errorBackgroundOff;
        QColor errorHeaderOff;
    };

    static Theme *instance();

    // System first, then every colour scheme that was found, in the order each
    // one's file asks for.
    static QVector<Choice> available();
    // The shapes the controls can take -- corners, borders, how a field shows
    // focus -- which go with any of the schemes. System first, which is no
    // shape at all: the platform's style, with no sheet of ours over it.
    static QVector<Choice> availableControls();

    // Sets the style, palette and sheet on the whole application. "system"
    // follows the desktop where Qt can tell, and the platform's own palette
    // where it cannot; a theme key nobody knows is taken as "system", and a
    // controls key nobody knows as the first of them.
    static void apply(const QString &theme, const QString &controls = currentControls());
    // Reads every definition folder again and puts back on what is being worn.
    // The files are read once, when they are first wanted; this is for when one
    // of them has been written since.
    static void reload();
    // The key as chosen, so "system" stays "system" whichever it resolved to.
    static QString current();
    static QString currentControls();
    static bool isDark();
    static const Colors &colors();
    // How round a card's corners are, for the panels that draw cards.
    static int cardRadius();

    // For what sits on the canvas -- the caption, the log, the scene's
    // controls: they are drawn over the scene's own background, which is
    // light whatever the application is wearing.
    static QPalette lightPalette();

signals:
    void changed();

private:
    Theme() = default;
    static void restyle(const QString &theme, const QString &controls);
};
