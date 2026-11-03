// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ruslan Muhlinin. See LICENSE.
#include "Theme.h"

#include <QApplication>
#include <QStyle>
#include <QStyleFactory>
#include <QStyleHints>

#include <algorithm>
#include <iterator>

namespace {

// A theme is ten colours; everything else is worked out from them, so adding
// one is adding a line here.
struct Spec {
    const char *key;
    const char *label;
    bool dark;
    QRgb window;    // panels and dialogs
    QRgb surface;   // the bars around them
    QRgb base;      // fields, lists, cards
    QRgb alternate; // every other row
    QRgb text;
    QRgb muted;
    QRgb border;
    QRgb soft;      // dividers, and borders that should not be noticed
    QRgb accent;
    QRgb accentText;
};

// The two plain ones first, then the palettes people know by name -- each
// family's light before its dark, so a pair sits together in the list.
const Spec kSpecs[] = {
    { "light", QT_TRANSLATE_NOOP("Theme", "Light"), false,
      0xEEF1F5, 0xF8F9FB, 0xFFFFFF, 0xF5F7FA, 0x1F2328, 0x667085,
      0xCDD3DC, 0xE3E7ED, 0x2F6FEB, 0xFFFFFF },
    { "dark", QT_TRANSLATE_NOOP("Theme", "Dark"), true,
      0x2B2D30, 0x1F2023, 0x1E1F22, 0x313338, 0xDFE1E5, 0x8C9099,
      0x4A4D52, 0x3A3C40, 0x3574F0, 0xFFFFFF },
    { "latte", QT_TRANSLATE_NOOP("Theme", "Catppuccin Latte"), false,
      0xE6E9EF, 0xDCE0E8, 0xEFF1F5, 0xE6E9EF, 0x4C4F69, 0x6C6F85,
      0xBCC0CC, 0xCCD0DA, 0x8839EF, 0xFFFFFF },
    { "mocha", QT_TRANSLATE_NOOP("Theme", "Catppuccin Mocha"), true,
      0x1E1E2E, 0x181825, 0x181825, 0x313244, 0xCDD6F4, 0xA6ADC8,
      0x45475A, 0x313244, 0xCBA6F7, 0x1E1E2E },
    { "dracula", QT_TRANSLATE_NOOP("Theme", "Dracula"), true,
      0x282A36, 0x21222C, 0x21222C, 0x44475A, 0xF8F8F2, 0x9AA3C7,
      0x565A72, 0x44475A, 0xBD93F9, 0x282A36 },
    { "everforest-light", QT_TRANSLATE_NOOP("Theme", "Everforest Light"), false,
      0xF2EFDF, 0xFFFBEF, 0xFFFBEF, 0xF8F5E4, 0x5C6A72, 0x939F91,
      0xDDD8C0, 0xEDEADA, 0x8DA101, 0x2D353B },
    { "everforest", QT_TRANSLATE_NOOP("Theme", "Everforest Dark"), true,
      0x2D353B, 0x232A2E, 0x232A2E, 0x343F44, 0xD3C6AA, 0x9DA9A0,
      0x4F585E, 0x3D484D, 0xA7C080, 0x232A2E },
    { "gruvbox-light", QT_TRANSLATE_NOOP("Theme", "Gruvbox Light"), false,
      0xF2E5BC, 0xFBF1C7, 0xFBF1C7, 0xEBDBB2, 0x3C3836, 0x7C6F64,
      0xD5C4A1, 0xE4D5AC, 0xAF3A03, 0xFBF1C7 },
    { "gruvbox", QT_TRANSLATE_NOOP("Theme", "Gruvbox Dark"), true,
      0x282828, 0x1D2021, 0x1D2021, 0x3C3836, 0xEBDBB2, 0xA89984,
      0x504945, 0x3C3836, 0xFE8019, 0x1D2021 },
    { "monokai", QT_TRANSLATE_NOOP("Theme", "Monokai"), true,
      0x272822, 0x1E1F1C, 0x1E1F1C, 0x3E3D32, 0xF8F8F2, 0xA59F85,
      0x4F4E43, 0x3E3D32, 0xF92672, 0x1E1F1C },
    { "nord", QT_TRANSLATE_NOOP("Theme", "Nord"), true,
      0x3B4252, 0x2E3440, 0x2E3440, 0x434C5E, 0xECEFF4, 0xA3ABB9,
      0x56607A, 0x4C566A, 0x88C0D0, 0x2E3440 },
    { "one-light", QT_TRANSLATE_NOOP("Theme", "One Light"), false,
      0xF0F0F0, 0xFAFAFA, 0xFFFFFF, 0xF5F5F5, 0x383A42, 0x8B8C92,
      0xD3D3D6, 0xE5E5E6, 0x4078F2, 0xFFFFFF },
    { "one-dark", QT_TRANSLATE_NOOP("Theme", "One Dark"), true,
      0x282C34, 0x21252B, 0x21252B, 0x2F343E, 0xABB2BF, 0x8A919E,
      0x4B5263, 0x3A3F4B, 0x61AFEF, 0x21252B },
    { "rose-pine-dawn", QT_TRANSLATE_NOOP("Theme", "Rosé Pine Dawn"), false,
      0xF2E9E1, 0xFFFAF3, 0xFFFAF3, 0xFAF4ED, 0x575279, 0x797593,
      0xDFDAD9, 0xEDE5DF, 0x907AA9, 0xFFFAF3 },
    { "rose-pine", QT_TRANSLATE_NOOP("Theme", "Rosé Pine"), true,
      0x1F1D2E, 0x191724, 0x191724, 0x26233A, 0xE0DEF4, 0x908CAA,
      0x403D52, 0x26233A, 0xC4A7E7, 0x191724 },
    { "solarized-light", QT_TRANSLATE_NOOP("Theme", "Solarized Light"), false,
      0xEEE8D5, 0xFDF6E3, 0xFDF6E3, 0xF5EFDC, 0x586E75, 0x8A9A98,
      0xC9C2AE, 0xE0DAC6, 0x268BD2, 0xFDF6E3 },
    { "solarized", QT_TRANSLATE_NOOP("Theme", "Solarized Dark"), true,
      0x002B36, 0x00212B, 0x00212B, 0x073642, 0x93A1A1, 0x768C8C,
      0x46606A, 0x073642, 0x268BD2, 0x002B36 },
    { "tokyo-night", QT_TRANSLATE_NOOP("Theme", "Tokyo Night"), true,
      0x1A1B26, 0x16161E, 0x16161E, 0x292E42, 0xC0CAF5, 0x8B95C4,
      0x3B4261, 0x292E42, 0x7AA2F7, 0x16161E },
};

// The shape of the controls, apart from their colour: how round, where the
// border is, how a field says it has focus. Most of it is corner radii put
// into the one sheet; what a style does differently beyond that is a few
// rules of its own, laid over the shared ones.
struct Shape {
    const char *key;
    const char *label;
    int field;      // fields and buttons
    int small;      // tool buttons, tabs, menu items
    int check;      // a checkbox
    int handle;     // a slider's handle
    int card;       // group boxes and the panels' cards
    int scroll;     // a scroll bar's handle
    int spin;       // the corners of a spin box's arrows
    const char *fieldPadding;
    const char *buttonPadding;
    const char *extra;
};

// Tabs as a line under the one chosen, rather than a filled pill.
#define UNDERLINE_TABS \
    "QTabBar::tab { border-radius: 0; margin: 0 2px; padding: 6px 12px;" \
    " border-bottom: 2px solid transparent; }" \
    "QTabBar::tab:hover { background: transparent; color: palette(window-text); }" \
    "QTabBar::tab:selected { background: transparent; color: palette(window-text);" \
    " border-bottom: 2px solid palette(highlight); }"

// What separates these is not how round they are. Each sets its own density
// -- the paddings below run from tight to roomy -- its own way of filling a
// button, and its own tick, radio, slider and scroll bar. A style that left
// those to the shared sheet would be the same controls at another radius,
// which is what these all were once.
//
// System is not one of them: it is the absence of all of them, so it has no
// row here any more than a theme has one in kSpecs.
//
// Qt draws a sub-control square when its border-radius is more than half the
// box, and a checked radio is a different box from an unchecked one -- its
// thick border stands in for the dot. Size the two to match, or the tick goes
// round and the dot comes out a square.
const Shape kShapes[] = {
    // The middle of the road, and what a new installation wears: the shared
    // sheet as it stands, which is why this one adds nothing.
    { "rounded", QT_TRANSLATE_NOOP("Theme", "Rounded"),
      6, 6, 4, 8, 8, 4, 6, "3px 8px", "5px 16px", "" },
    // Dense and flat, the way an IDE is: a third of the padding of Material,
    // small square ticks, a slider that is a square grip on a thin line, and
    // a scroll bar with a channel it runs in. The tab in hand is marked by a
    // bar above it.
    { "sharp", QT_TRANSLATE_NOOP("Theme", "Sharp"),
      0, 0, 0, 0, 0, 0, 0, "1px 5px", "3px 10px",
      "QPushButton { background: palette(base); border: 1px solid @borderStrong;"
      " min-height: 14px; }"
      "QPushButton:hover { background: @accentSoft; border-color: palette(highlight); }"
      "QLineEdit, QAbstractSpinBox, QComboBox, QTextEdit, QPlainTextEdit {"
      " border: 1px solid @borderStrong; }"
      "QCheckBox::indicator, QGroupBox::indicator { width: 12px; height: 12px;"
      " border: 1px solid @borderStrong; border-radius: 0; }"
      "QRadioButton::indicator { width: 12px; height: 12px; border-radius: 0;"
      " border: 1px solid @borderStrong; }"
      "QRadioButton::indicator:checked { width: 6px; height: 6px;"
      " border: 4px solid palette(highlight); border-radius: 0; }"
      "QSlider::groove:horizontal { height: 2px; border-radius: 0; }"
      "QSlider::sub-page:horizontal { border-radius: 0; }"
      "QSlider::handle:horizontal { width: 7px; height: 16px; margin: -8px 0;"
      " border-radius: 0; border: 1px solid @borderStrong; background: palette(base); }"
      "QScrollBar:vertical { width: 11px; background: palette(window);"
      " border-left: 1px solid palette(midlight); }"
      "QScrollBar:horizontal { height: 11px; background: palette(window);"
      " border-top: 1px solid palette(midlight); }"
      "QScrollBar::handle { background: palette(mid); border-radius: 0; margin: 0; }"
      "QGroupBox { border: 1px solid @borderStrong; margin-top: 11px;"
      " padding: 7px 6px 6px 6px; }"
      "QTabBar::tab { border-radius: 0; margin: 0 1px; padding: 5px 12px;"
      " border-top: 2px solid transparent; }"
      "QTabBar::tab:hover { background: @hover; color: palette(window-text); }"
      "QTabBar::tab:selected { background: palette(base); color: palette(window-text);"
      " border-top: 2px solid palette(highlight); }" },
    // Windows 11's: a button lit from the top and darker along its bottom
    // edge, a field whose bottom stroke thickens and turns accent as it takes
    // focus, a rounded-square tick, and the ringed thumb WinUI slides along a
    // thin rail. Its scroll bar runs in a pale channel.
    { "fluent", QT_TRANSLATE_NOOP("Theme", "Fluent"),
      4, 4, 4, 9, 7, 3, 3, "5px 9px", "6px 18px",
      UNDERLINE_TABS
      "QPushButton { background: qlineargradient(x1:0, y1:0, x2:0, y2:1,"
      " stop:0 palette(base), stop:1 palette(alternate-base));"
      " border-bottom-color: @borderStrong; min-height: 20px; }"
      "QPushButton:hover { background: @hover; }"
      "QLineEdit, QAbstractSpinBox, QComboBox, QTextEdit, QPlainTextEdit {"
      " border: 1px solid palette(mid); border-bottom: 2px solid @borderStrong; }"
      "QLineEdit:hover, QAbstractSpinBox:hover, QComboBox:hover {"
      " background: palette(alternate-base); border-color: palette(mid);"
      " border-bottom-color: @borderStrong; }"
      "QLineEdit:focus, QAbstractSpinBox:focus, QComboBox:focus, QTextEdit:focus,"
      " QPlainTextEdit:focus { border-color: palette(mid);"
      " border-bottom: 2px solid palette(highlight); }"
      "QLineEdit:disabled, QAbstractSpinBox:disabled, QComboBox:disabled {"
      " border-color: palette(midlight); border-bottom-color: palette(midlight); }"
      "QCheckBox::indicator, QGroupBox::indicator { width: 17px; height: 17px; }"
      "QRadioButton::indicator { width: 17px; height: 17px; border-radius: 9px; }"
      "QRadioButton::indicator:checked { width: 9px; height: 9px; border-radius: 9px;"
      " border: 5px solid palette(highlight); }"
      "QSlider::groove:horizontal { height: 4px; }"
      "QSlider::handle:horizontal { width: 10px; height: 10px; margin: -7px 0;"
      " border: 4px solid palette(base); border-radius: 9px;"
      " background: palette(highlight); }"
      "QSlider::handle:horizontal:hover { border-width: 3px; }"
      "QScrollBar:vertical { width: 12px; background: palette(base);"
      " border-left: 1px solid palette(midlight); }"
      "QScrollBar:horizontal { height: 12px; background: palette(base);"
      " border-top: 1px solid palette(midlight); }"
      "QScrollBar::handle { margin: 4px; }" },
    // Material 3: the roomiest of them, its button a tonal capsule with no
    // edge at all and its label in medium, its field filled, square along the
    // bottom and underlined there. Its slider is the 2024 one -- a tall pill
    // of a track with a thin upright handle standing in a gap of its own --
    // and its tick carries the thick unchecked border Material draws.
    { "material", QT_TRANSLATE_NOOP("Theme", "Material"),
      4, 8, 2, 2, 12, 5, 0, "8px 12px", "9px 22px",
      UNDERLINE_TABS
      "QLineEdit, QAbstractSpinBox, QComboBox, QTextEdit, QPlainTextEdit {"
      " background: palette(alternate-base); border: none;"
      " border-bottom: 2px solid palette(mid);"
      " border-bottom-left-radius: 0; border-bottom-right-radius: 0; }"
      "QLineEdit:hover, QAbstractSpinBox:hover, QComboBox:hover {"
      " border-bottom-color: @borderStrong; }"
      "QLineEdit:focus, QAbstractSpinBox:focus, QComboBox:focus, QTextEdit:focus,"
      " QPlainTextEdit:focus { border-bottom-color: palette(highlight); }"
      "QLineEdit:disabled, QAbstractSpinBox:disabled, QComboBox:disabled {"
      " border-bottom-color: palette(midlight); }"
      "QPushButton { border: none; border-radius: 16px; background: palette(midlight);"
      " font-weight: 600; min-height: 22px; }"
      "QPushButton:hover { background: @accentSoft; }"
      "QPushButton:default { background: palette(highlight); }"
      "QPushButton:disabled { background: palette(window); }"
      "QCheckBox::indicator, QGroupBox::indicator { width: 16px; height: 16px;"
      " border: 2px solid @borderStrong; }"
      "QRadioButton::indicator { width: 16px; height: 16px; border: 2px solid @borderStrong;"
      " border-radius: 10px; }"
      "QRadioButton::indicator:checked { width: 10px; height: 10px; border-radius: 10px;"
      " border: 5px solid palette(highlight); }"
      "QSlider::groove:horizontal { height: 16px; border-radius: 8px; }"
      "QSlider::sub-page:horizontal { border-radius: 8px; }"
      "QSlider::handle:horizontal { width: 4px; height: 26px; margin: -5px 0;"
      " border-radius: 2px; background: palette(highlight);"
      " border-left: 6px solid palette(window); border-right: 6px solid palette(window); }"
      "QScrollBar:vertical { width: 14px; }"
      "QScrollBar:horizontal { height: 14px; }"
      "QScrollBar::handle { margin: 3px; }"
      "QGroupBox { border: none; border-top: 2px solid palette(midlight); border-radius: 0;"
      " margin-top: 18px; padding: 14px 8px 8px 8px; }" },
    // Nothing drawn until it is wanted: tinted fields that outline on focus,
    // buttons that are only their words, a tick with no edge at all, a slider
    // reduced to a hairline and a dot, and a scroll bar that is barely there.
    { "minimal", QT_TRANSLATE_NOOP("Theme", "Minimal"),
      8, 8, 5, 7, 10, 3, 0, "6px 10px", "7px 18px",
      "QLineEdit, QAbstractSpinBox, QComboBox, QTextEdit, QPlainTextEdit {"
      " background: palette(midlight); border: 1px solid transparent; }"
      "QLineEdit:hover, QAbstractSpinBox:hover, QComboBox:hover {"
      " background: @hover; border-color: transparent; }"
      "QLineEdit:focus, QAbstractSpinBox:focus, QComboBox:focus, QTextEdit:focus,"
      " QPlainTextEdit:focus { background: palette(base); border-color: palette(highlight); }"
      "QLineEdit:disabled, QAbstractSpinBox:disabled, QComboBox:disabled {"
      " background: palette(window); border-color: transparent; }"
      "QPushButton { background: transparent; border: 1px solid transparent; color: @accent;"
      " font-weight: 600; }"
      "QPushButton:hover { background: @accentSoft; border-color: transparent; }"
      "QPushButton:default { background: palette(highlight); color: palette(highlighted-text); }"
      "QPushButton:disabled { background: transparent; color: @faint; border-color: transparent; }"
      "QTabBar::tab, QTabBar::tab:hover, QTabBar::tab:selected { background: transparent; }"
      "QTabBar::tab:selected { color: @accent; }"
      "QCheckBox::indicator, QGroupBox::indicator { width: 16px; height: 16px;"
      " border: none; background: palette(midlight); }"
      "QCheckBox::indicator:hover, QGroupBox::indicator:hover { background: @accentSoft; }"
      "QRadioButton::indicator { width: 16px; height: 16px; border: none;"
      " border-radius: 8px; background: palette(midlight); }"
      "QRadioButton::indicator:hover { background: @accentSoft; }"
      "QRadioButton::indicator:checked { width: 6px; height: 6px; border-radius: 8px;"
      " border: 5px solid palette(highlight); }"
      "QSlider::groove:horizontal { height: 2px; background: palette(mid); }"
      "QSlider::handle:horizontal { width: 10px; height: 10px; margin: -5px 0;"
      " border: none; border-radius: 5px; background: palette(highlight); }"
      "QScrollBar:vertical { width: 7px; }"
      "QScrollBar:horizontal { height: 7px; }"
      "QScrollBar::handle { margin: 1px; background: palette(midlight); }"
      "QScrollBar::handle:hover { background: palette(mid); }"
      "QGroupBox { border: none; border-radius: 0; margin-top: 20px;"
      " padding: 12px 2px 6px 2px; }" },
};

#undef UNDERLINE_TABS

const QString kSystem = QStringLiteral("system");

QString s_key = kSystem;
// The shape as chosen, so "system" stays "system": s_shape is then the first
// of them, for the card radii the panels still ask for.
QString s_controls = QLatin1String(kShapes[0].key);
const Spec *s_spec = &kSpecs[0];
const Shape *s_shape = &kShapes[0];
bool s_styled = false;
// The palette the platform handed over before Fusion and ours replaced it:
// where Qt cannot say whether the desktop is dark, its own window colour can.
// Both are what the System controls go back to.
QPalette s_platformPalette;
QString s_platformStyle;

QColor mix(const QColor &a, const QColor &b, qreal t)
{
    return QColor::fromRgbF(a.redF() + (b.redF() - a.redF()) * t,
                            a.greenF() + (b.greenF() - a.greenF()) * t,
                            a.blueF() + (b.blueF() - a.blueF()) * t);
}

QPalette makePalette(const Spec &spec)
{
    const QColor window(spec.window), base(spec.base), text(spec.text),
        border(spec.border), soft(spec.soft), accent(spec.accent);
    const QColor faint = mix(text, window, 0.55);

    QPalette p;
    p.setColor(QPalette::Window, window);
    p.setColor(QPalette::WindowText, text);
    p.setColor(QPalette::Base, base);
    p.setColor(QPalette::AlternateBase, QColor(spec.alternate));
    p.setColor(QPalette::Text, text);
    p.setColor(QPalette::Button, spec.dark ? QColor(spec.alternate) : base);
    p.setColor(QPalette::ButtonText, text);
    p.setColor(QPalette::BrightText, Qt::white);
    p.setColor(QPalette::Light, spec.dark ? mix(soft, text, 0.15) : QColor(Qt::white));
    p.setColor(QPalette::Midlight, soft);
    p.setColor(QPalette::Mid, border);
    p.setColor(QPalette::Dark, spec.dark ? mix(window, Qt::black, 0.4) : mix(border, text, 0.3));
    p.setColor(QPalette::Shadow, spec.dark ? QColor(Qt::black) : mix(border, text, 0.5));
    p.setColor(QPalette::Highlight, accent);
    p.setColor(QPalette::HighlightedText, QColor(spec.accentText));
    p.setColor(QPalette::Link, accent);
    p.setColor(QPalette::LinkVisited, accent);
    p.setColor(QPalette::ToolTipBase, spec.dark ? QColor(spec.alternate) : QColor(0x2B2D31));
    p.setColor(QPalette::ToolTipText, spec.dark ? text : QColor(0xF0F1F3));
    p.setColor(QPalette::PlaceholderText, QColor(spec.muted));

    p.setColor(QPalette::Disabled, QPalette::WindowText, faint);
    p.setColor(QPalette::Disabled, QPalette::Text, faint);
    p.setColor(QPalette::Disabled, QPalette::ButtonText, faint);
    p.setColor(QPalette::Disabled, QPalette::Highlight, border);
    p.setColor(QPalette::Disabled, QPalette::Base, window);
    return p;
}

Theme::Colors makeColors(const Spec &spec)
{
    const QColor window(spec.window), base(spec.base), text(spec.text), accent(spec.accent);

    Theme::Colors c;
    c.surface = QColor(spec.surface);
    c.hover = mix(QColor(spec.surface), text, spec.dark ? 0.10 : 0.07);
    c.accent = spec.dark ? mix(accent, Qt::white, 0.15) : accent;
    c.accentSoft = mix(window, accent, spec.dark ? 0.28 : 0.16);
    c.borderStrong = mix(QColor(spec.border), text, 0.3);
    c.mutedText = QColor(spec.muted);
    c.faintText = mix(text, window, 0.55);
    c.glyph = mix(text, window, 0.25);
    c.glyphDisabled = mix(text, window, 0.75);

    c.error = spec.dark ? QColor(0xE0, 0x6C, 0x75) : QColor(0xD9, 0x53, 0x4F);
    c.errorText = spec.dark ? QColor(0xF2, 0x8B, 0x82) : QColor(0xA3, 0x3A, 0x35);
    c.errorBackground = mix(base, c.error, 0.08);
    c.errorHeader = mix(base, c.error, spec.dark ? 0.28 : 0.22);
    c.errorOff = mix(c.error, window, 0.5);
    c.errorTextOff = mix(c.errorText, window, 0.4);
    c.errorBackgroundOff = mix(window, c.error, 0.05);
    c.errorHeaderOff = mix(window, c.error, 0.12);
    return c;
}

Theme::Colors s_colors = makeColors(kSpecs[0]);

// Colours come from palette() wherever a role fits, so a widget given a
// palette of its own -- the overlays on the canvas -- is drawn in it rather
// than in the application's. Only what no role means is spelled out.
QString makeStyleSheet(const Spec &spec, const Shape &shape, const Theme::Colors &c)
{
    // The arrows and the tick are images, and an image has one colour: light
    // ink for a dark theme, dark ink for a light one, and the tick in
    // whichever reads on the accent.
    const QString ink = spec.dark ? QStringLiteral("light") : QStringLiteral("dark");
    const QString tick = QColor(spec.accentText).lightness() > 128 ? QStringLiteral("light")
                                                                   : QStringLiteral("dark");

    QString sheet = QStringLiteral(R"(
QMainWindow::separator { background: palette(window); width: 4px; height: 4px; }
QMainWindow::separator:hover { background: palette(highlight); }

QToolBar { background: @surface; border: none; border-bottom: 1px solid palette(midlight);
           padding: 4px 8px; spacing: 4px; }
QToolBar::separator { background: palette(mid); width: 1px; margin: 7px 6px; }
QToolButton { background: transparent; border: 1px solid transparent; border-radius: @rs; padding: 3px; }
QToolButton:hover { background: @hover; }
QToolButton:pressed, QToolButton:checked { background: @accentSoft; border-color: transparent; }
QToolButton:disabled { background: transparent; }

QMenuBar { background: @surface; padding: 2px 4px; border-bottom: 1px solid palette(midlight); }
QMenuBar::item { background: transparent; padding: 5px 10px; border-radius: @rs; }
QMenuBar::item:selected { background: @hover; }
QMenu { background: palette(base); border: 1px solid palette(mid); padding: 5px; }
QMenu::item { padding: 6px 28px 6px 12px; border-radius: @rs; }
QMenu::item:selected { background: palette(highlight); color: palette(highlighted-text); }
QMenu::item:disabled { color: @faint; }
QMenu::separator { height: 1px; background: palette(midlight); margin: 5px 8px; }

QStatusBar { background: @surface; border-top: 1px solid palette(midlight); }
QStatusBar::item { border: none; }
QDockWidget::title { background: @surface; padding: 6px; }

QTabWidget::pane { border: none; border-top: 1px solid palette(midlight); }
QTabBar::tab { background: transparent; color: @muted; border: none; border-radius: @rs;
               padding: 5px 10px; margin: 4px 2px; }
QTabBar::tab:hover { background: @hover; color: palette(window-text); }
QTabBar::tab:selected { background: @accentSoft; color: @accent; }

/* A strip of icons with no words under them needs none of the room a word
   would have taken: the padding here is for a label, and Qt adds its own
   frame on top of it. */
QTabBar[iconsOnly="true"]::tab { padding: 4px 2px; margin: 4px 1px; }

QPushButton { background: palette(button); color: palette(button-text); border: 1px solid palette(mid);
              border-radius: @r; padding: @bpad; min-height: 18px; }
QPushButton:hover { background: @hover; border-color: @borderStrong; }
QPushButton:pressed { background: palette(midlight); }
QPushButton:checked { background: @accentSoft; border-color: palette(highlight); }
QPushButton:default { background: palette(highlight); color: palette(highlighted-text);
                      border-color: palette(highlight); }
QPushButton:default:hover { background: @accentHover; }
QPushButton:disabled { background: palette(window); color: @faint; border-color: palette(midlight); }

QLineEdit, QAbstractSpinBox, QComboBox, QTextEdit, QPlainTextEdit {
    background: palette(base); color: palette(text); border: 1px solid palette(mid);
    border-radius: @r; padding: @fpad;
    selection-background-color: palette(highlight); selection-color: palette(highlighted-text); }
QLineEdit:hover, QAbstractSpinBox:hover, QComboBox:hover { border-color: @borderStrong; }
QLineEdit:focus, QAbstractSpinBox:focus, QComboBox:focus, QTextEdit:focus, QPlainTextEdit:focus {
    border-color: palette(highlight); }
QLineEdit:disabled, QAbstractSpinBox:disabled, QComboBox:disabled {
    background: palette(window); color: @faint; border-color: palette(midlight); }
QAbstractSpinBox QLineEdit, QComboBox QLineEdit {
    background: transparent; border: none; padding: 0; border-radius: 0; }

QAbstractSpinBox::up-button, QAbstractSpinBox::down-button {
    subcontrol-origin: border; width: 18px; border: none; background: transparent; }
QAbstractSpinBox::up-button { subcontrol-position: top right; border-top-right-radius: @rspin; }
QAbstractSpinBox::down-button { subcontrol-position: bottom right; border-bottom-right-radius: @rspin; }
QAbstractSpinBox::up-button:hover, QAbstractSpinBox::down-button:hover { background: @hover; }
QAbstractSpinBox::up-arrow { image: url(:/theme/up-@ink.svg); width: 9px; height: 9px; }
QAbstractSpinBox::down-arrow { image: url(:/theme/down-@ink.svg); width: 9px; height: 9px; }

QComboBox { padding-right: 4px; }
QComboBox::drop-down { subcontrol-origin: padding; subcontrol-position: center right;
                       width: 20px; border: none; }
QComboBox::down-arrow { image: url(:/theme/down-@ink.svg); width: 10px; height: 10px; }
QComboBox QAbstractItemView { background: palette(base); border: 1px solid palette(mid); padding: 4px;
                              selection-background-color: palette(highlight);
                              selection-color: palette(highlighted-text); outline: 0; }

QCheckBox, QRadioButton { spacing: 7px; }
QCheckBox::indicator, QGroupBox::indicator { width: 15px; height: 15px; border: 1px solid @borderStrong;
                                             border-radius: @rc; background: palette(base); }
QCheckBox::indicator:hover, QGroupBox::indicator:hover { border-color: palette(highlight); }
QCheckBox::indicator:checked, QGroupBox::indicator:checked {
    background: palette(highlight); border-color: palette(highlight); image: url(:/theme/check-@tick.svg); }
QCheckBox::indicator:indeterminate { background: palette(highlight); border-color: palette(highlight); }
QCheckBox::indicator:disabled { background: palette(window); border-color: palette(midlight); }
QRadioButton::indicator { width: 15px; height: 15px; border: 1px solid @borderStrong;
                          border-radius: 8px; background: palette(base); }
QRadioButton::indicator:hover { border-color: palette(highlight); }
QRadioButton::indicator:checked { width: 9px; height: 9px; border: 4px solid palette(highlight); }

QSlider::groove:horizontal { height: 4px; background: palette(midlight); border-radius: 2px; }
QSlider::sub-page:horizontal { background: palette(highlight); border-radius: 2px; }
QSlider::groove:vertical { width: 4px; background: palette(midlight); border-radius: 2px; }
QSlider::add-page:vertical { background: palette(highlight); border-radius: 2px; }
QSlider::handle { background: palette(base); border: 2px solid palette(highlight);
                  width: 12px; height: 12px; border-radius: @rh; }
QSlider::handle:horizontal { margin: -6px 0; }
QSlider::handle:vertical { margin: 0 -6px; }
QSlider::handle:hover { background: @accentSoft; }

QAbstractItemView { background: palette(base); alternate-background-color: palette(alternate-base);
                    selection-background-color: palette(highlight);
                    selection-color: palette(highlighted-text); outline: 0; }
QTreeView, QListView, QTableView { border: none; }
QTableView { gridline-color: palette(midlight); }
QTreeView::item, QListView::item { padding: 3px 2px; }
QTreeView::item:hover, QListView::item:hover { background: @hover; }
QTreeView::item:selected, QListView::item:selected { background: palette(highlight);
                                                     color: palette(highlighted-text); }
QHeaderView::section { background: palette(window); color: @muted; border: none;
                       border-bottom: 1px solid palette(midlight); border-right: 1px solid palette(midlight);
                       padding: 4px 8px; }

QGroupBox { border: 1px solid palette(midlight); border-radius: @rk; margin-top: 14px;
            padding: 10px 8px 8px 8px; }
QGroupBox::title { subcontrol-origin: margin; subcontrol-position: top left; left: 12px;
                   padding: 0 4px; color: @muted; }

QScrollBar:vertical { background: transparent; width: 12px; margin: 0; }
QScrollBar:horizontal { background: transparent; height: 12px; margin: 0; }
QScrollBar::handle { background: palette(mid); border-radius: @rsb; margin: 2px; }
QScrollBar::handle:vertical { min-height: 32px; }
QScrollBar::handle:horizontal { min-width: 32px; }
QScrollBar::handle:hover { background: @borderStrong; }
QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }
QScrollBar::add-page, QScrollBar::sub-page { background: none; }

QToolTip { background: @tipBase; color: @tipText; border: 1px solid @tipBase; border-radius: @rs;
           padding: 5px 8px; }

QLabel[muted="true"] { color: @muted; }
)");
    // After the shared rules, so where both say something the style wins.
    sheet += QLatin1String(shape.extra);

    const auto px = [](int value) { return QStringLiteral("%1px").arg(value); };
    const QPalette palette = makePalette(spec);
    std::pair<QString, QString> values[] = {
        { "@rspin", px(shape.spin) },
        { "@rsb", px(shape.scroll) },
        { "@rs", px(shape.small) },
        { "@rc", px(shape.check) },
        { "@rh", px(shape.handle) },
        { "@rk", px(shape.card) },
        { "@r", px(shape.field) },
        { "@fpad", QLatin1String(shape.fieldPadding) },
        { "@bpad", QLatin1String(shape.buttonPadding) },
        { "@surface", c.surface.name() },
        { "@hover", c.hover.name() },
        { "@accentSoft", c.accentSoft.name() },
        { "@accentHover", mix(QColor(spec.accent), QColor(spec.text), 0.12).name() },
        { "@accent", c.accent.name() },
        { "@borderStrong", c.borderStrong.name() },
        { "@muted", c.mutedText.name() },
        { "@faint", c.faintText.name() },
        { "@tipBase", palette.color(QPalette::ToolTipBase).name() },
        { "@tipText", palette.color(QPalette::ToolTipText).name() },
        { "@ink", ink },
        { "@tick", tick },
    };
    // Longest first: @r is the start of every other radius, @accent of two
    // more names.
    std::sort(std::begin(values), std::end(values),
              [](const auto &a, const auto &b) { return a.first.size() > b.first.size(); });
    for (const auto &[name, value] : values)
        sheet.replace(name, value);
    return sheet;
}

bool systemIsDark()
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    const Qt::ColorScheme scheme = QGuiApplication::styleHints()->colorScheme();
    if (scheme != Qt::ColorScheme::Unknown)
        return scheme == Qt::ColorScheme::Dark;
#endif
    return s_platformPalette.color(QPalette::Window).lightness() < 128;
}

const Spec *findSpec(const QString &key)
{
    for (const Spec &spec : kSpecs) {
        if (key == QLatin1String(spec.key))
            return &spec;
    }
    return nullptr;
}

const Shape *findShape(const QString &key)
{
    for (const Shape &shape : kShapes) {
        if (key == QLatin1String(shape.key))
            return &shape;
    }
    return nullptr;
}

} // namespace

Theme *Theme::instance()
{
    static Theme theme;
    return &theme;
}

QVector<Theme::Choice> Theme::available()
{
    QVector<Choice> choices { { kSystem, tr("System") } };
    for (const Spec &spec : kSpecs)
        choices.append({ QString::fromLatin1(spec.key), tr(spec.label) });
    return choices;
}

QVector<Theme::Choice> Theme::availableControls()
{
    QVector<Choice> choices { { kSystem, tr("System") } };
    for (const Shape &shape : kShapes)
        choices.append({ QString::fromLatin1(shape.key), tr(shape.label) });
    return choices;
}

void Theme::apply(const QString &theme, const QString &controls)
{
    const QString chosen = findSpec(theme) ? theme : kSystem;
    // An unknown shape falls back to the first real one rather than to System:
    // a key nobody knows is a typo in the settings file, not a request for the
    // platform's own look.
    const QString shape = (controls == kSystem || findShape(controls))
        ? controls : QString::fromLatin1(kShapes[0].key);
    // Options hands every setting over each time it closes, and polishing
    // every widget again is not free.
    if (s_styled && chosen == s_key && shape == s_controls)
        return;

    if (!s_styled) {
        s_platformPalette = QApplication::palette();
        s_platformStyle = QApplication::style()->objectName();
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
        QObject::connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged,
                         instance(), [] {
                             if (s_key == kSystem)
                                 restyle(kSystem, currentControls());
                         });
#endif
        s_styled = true;
    }
    restyle(chosen, shape);
}

void Theme::restyle(const QString &theme, const QString &controls)
{
    s_key = theme;
    s_controls = controls;
    s_shape = findShape(controls);
    if (!s_shape)
        s_shape = &kShapes[0];
    // The native style paints its own chrome and reads little of a palette, so
    // a theme laid over it would be half applied -- light text on a button
    // still drawn light. It wears the platform's own instead, and the cards
    // the panels paint follow the desktop so they match what is around them.
    const bool native = controls == kSystem;
    s_spec = native ? nullptr : findSpec(theme);
    if (!s_spec)
        s_spec = findSpec(systemIsDark() ? QStringLiteral("dark") : QStringLiteral("light"));
    s_colors = makeColors(*s_spec);

    // Before the palette: a style brings its own, and would undo ours.
    if (QStyle *style = QStyleFactory::create(native ? s_platformStyle
                                                     : QStringLiteral("Fusion")))
        QApplication::setStyle(style);
    QApplication::setPalette(native ? s_platformPalette : makePalette(*s_spec));
    // Set after the palette, and set every time: it is what makes Qt polish
    // every widget again, and so what makes the palette() references in it
    // read the new one.
    qApp->setStyleSheet(native ? QString() : makeStyleSheet(*s_spec, *s_shape, s_colors));

    emit instance()->changed();
}

QString Theme::current()
{
    return s_key;
}

QString Theme::currentControls()
{
    return s_controls;
}

int Theme::cardRadius()
{
    return s_shape->card;
}

bool Theme::isDark()
{
    return s_spec->dark;
}

const Theme::Colors &Theme::colors()
{
    return s_colors;
}

QPalette Theme::lightPalette()
{
    return makePalette(kSpecs[0]);
}
