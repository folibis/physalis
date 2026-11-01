// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ruslan Muhlinin. See LICENSE.
#include "Theme.h"

#include <QApplication>
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

const Spec kSpecs[] = {
    { "light", QT_TRANSLATE_NOOP("Theme", "Light"), false,
      0xEEF1F5, 0xF8F9FB, 0xFFFFFF, 0xF5F7FA, 0x1F2328, 0x667085,
      0xCDD3DC, 0xE3E7ED, 0x2F6FEB, 0xFFFFFF },
    { "dark", QT_TRANSLATE_NOOP("Theme", "Dark"), true,
      0x2B2D30, 0x1F2023, 0x1E1F22, 0x313338, 0xDFE1E5, 0x8C9099,
      0x4A4D52, 0x3A3C40, 0x3574F0, 0xFFFFFF },
    { "nord", QT_TRANSLATE_NOOP("Theme", "Nord"), true,
      0x3B4252, 0x2E3440, 0x2E3440, 0x434C5E, 0xECEFF4, 0xA3ABB9,
      0x56607A, 0x4C566A, 0x88C0D0, 0x2E3440 },
    { "latte", QT_TRANSLATE_NOOP("Theme", "Catppuccin Latte"), false,
      0xE6E9EF, 0xDCE0E8, 0xEFF1F5, 0xE6E9EF, 0x4C4F69, 0x6C6F85,
      0xBCC0CC, 0xCCD0DA, 0x8839EF, 0xFFFFFF },
    { "mocha", QT_TRANSLATE_NOOP("Theme", "Catppuccin Mocha"), true,
      0x1E1E2E, 0x181825, 0x181825, 0x313244, 0xCDD6F4, 0xA6ADC8,
      0x45475A, 0x313244, 0xCBA6F7, 0x1E1E2E },
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

const Shape kShapes[] = {
    { "rounded", QT_TRANSLATE_NOOP("Theme", "Rounded"),
      6, 6, 4, 8, 8, 4, 6, "2px 6px", "5px 16px", "" },
    { "pill", QT_TRANSLATE_NOOP("Theme", "Pill"),
      11, 12, 8, 8, 14, 5, 0, "2px 10px", "5px 18px", "" },
    { "sharp", QT_TRANSLATE_NOOP("Theme", "Sharp"),
      0, 0, 0, 0, 0, 0, 0, "2px 6px", "5px 14px", UNDERLINE_TABS },
    // Filled fields with a line under them that lights up, and flat buttons.
    { "material", QT_TRANSLATE_NOOP("Theme", "Material"),
      4, 4, 2, 8, 6, 4, 0, "3px 8px", "6px 16px",
      UNDERLINE_TABS
      "QLineEdit, QAbstractSpinBox, QComboBox, QTextEdit, QPlainTextEdit {"
      " background: palette(alternate-base); border: none; border-bottom: 2px solid palette(mid);"
      " border-bottom-left-radius: 0; border-bottom-right-radius: 0; }"
      "QLineEdit:hover, QAbstractSpinBox:hover, QComboBox:hover { border-bottom-color: @borderStrong; }"
      "QLineEdit:focus, QAbstractSpinBox:focus, QComboBox:focus, QTextEdit:focus,"
      " QPlainTextEdit:focus { border-bottom-color: palette(highlight); }"
      "QLineEdit:disabled, QAbstractSpinBox:disabled, QComboBox:disabled {"
      " border-bottom-color: palette(midlight); }"
      "QPushButton { border: none; background: palette(midlight); }"
      "QPushButton:hover { background: @accentSoft; }"
      "QPushButton:default { background: palette(highlight); }"
      "QPushButton:disabled { background: palette(window); }"
      "QGroupBox { border: none; border-top: 2px solid palette(midlight); border-radius: 0; }" },
    // Nothing drawn until it is wanted: tinted fields that outline on focus,
    // buttons that are only their words.
    { "minimal", QT_TRANSLATE_NOOP("Theme", "Minimal"),
      8, 8, 4, 8, 10, 4, 0, "3px 8px", "5px 16px",
      "QLineEdit, QAbstractSpinBox, QComboBox, QTextEdit, QPlainTextEdit {"
      " background: palette(midlight); border: 1px solid transparent; }"
      "QLineEdit:hover, QAbstractSpinBox:hover, QComboBox:hover {"
      " background: @hover; border-color: transparent; }"
      "QLineEdit:focus, QAbstractSpinBox:focus, QComboBox:focus, QTextEdit:focus,"
      " QPlainTextEdit:focus { background: palette(base); border-color: palette(highlight); }"
      "QLineEdit:disabled, QAbstractSpinBox:disabled, QComboBox:disabled {"
      " background: palette(window); border-color: transparent; }"
      "QPushButton { background: transparent; border: 1px solid transparent; color: @accent; }"
      "QPushButton:hover { background: @accentSoft; border-color: transparent; }"
      "QPushButton:default { background: palette(highlight); color: palette(highlighted-text); }"
      "QPushButton:disabled { background: transparent; color: @faint; border-color: transparent; }"
      "QTabBar::tab, QTabBar::tab:hover, QTabBar::tab:selected { background: transparent; }"
      "QTabBar::tab:selected { color: @accent; }"
      "QCheckBox::indicator, QRadioButton::indicator { border-color: palette(mid); }"
      "QGroupBox { border: none; border-top: 1px solid palette(midlight); border-radius: 0; }" },
};

#undef UNDERLINE_TABS

const QString kSystem = QStringLiteral("system");

QString s_key = kSystem;
const Spec *s_spec = &kSpecs[0];
const Shape *s_shape = &kShapes[0];
bool s_styled = false;
// The palette the platform handed over before Fusion and ours replaced it:
// where Qt cannot say whether the desktop is dark, its own window colour can.
QPalette s_platformPalette;

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
    QVector<Choice> choices;
    for (const Shape &shape : kShapes)
        choices.append({ QString::fromLatin1(shape.key), tr(shape.label) });
    return choices;
}

void Theme::apply(const QString &theme, const QString &controls)
{
    const QString chosen = findSpec(theme) ? theme : kSystem;
    const QString shape = findShape(controls) ? controls : QString::fromLatin1(kShapes[0].key);
    // Options hands every setting over each time it closes, and polishing
    // every widget again is not free.
    if (s_styled && chosen == s_key && shape == QLatin1String(s_shape->key))
        return;

    if (!s_styled) {
        s_platformPalette = QApplication::palette();
        QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
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
    s_spec = findSpec(theme);
    if (!s_spec)
        s_spec = findSpec(systemIsDark() ? QStringLiteral("dark") : QStringLiteral("light"));
    s_shape = findShape(controls);
    s_colors = makeColors(*s_spec);

    QApplication::setPalette(makePalette(*s_spec));
    // Set after the palette, and set every time: it is what makes Qt polish
    // every widget again, and so what makes the palette() references in it
    // read the new one.
    qApp->setStyleSheet(makeStyleSheet(*s_spec, *s_shape, s_colors));

    emit instance()->changed();
}

QString Theme::current()
{
    return s_key;
}

QString Theme::currentControls()
{
    return QString::fromLatin1(s_shape->key);
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
