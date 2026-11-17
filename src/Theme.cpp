// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ruslan Muhlinin. See LICENSE.
#include "Theme.h"

#include "AssetFolder.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QMap>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>
#include <QStyle>
#include <QStyleFactory>
#include <QStyleHints>

#include <algorithm>
#include <iterator>
#include <utility>

namespace {

// A colour scheme is ten colours and whether it is a dark one; everything else
// -- the palette, the derived colours, the sheet -- is worked out from them.
struct Scheme {
    QString key;
    QString label;
    int order = 0;
    bool dark = false;
    QColor window;    // panels and dialogs
    QColor surface;   // the bars around them
    QColor base;      // fields, lists, cards
    QColor alternate; // every other row
    QColor text;
    QColor muted;
    QColor border;
    QColor soft;      // dividers, and borders that should not be noticed
    QColor accent;
    QColor accentText;
    // What the error shades are mixed from, where a scheme would rather not
    // leave it to the two below.
    QColor error;
    QColor errorText;
};

// The shape of the controls, apart from their colour: how round, where the
// border is, how a field says it has focus. Most of it is the radii and
// paddings that fill the shared sheet's placeholders; what a style does
// differently beyond that is a few rules of its own, laid over the shared ones.
struct Style {
    QString key;
    QString label;
    int order = 0;
    QMap<QString, int> radius;      // @r<name>
    QMap<QString, QString> padding; // @pad<name>
    QString sheet;
};

const QString kSystem = QStringLiteral("system");
// Where a file that names no order of its own goes: after everything that
// ships, in with the rest of its kind by label.
const int kLast = 1000;

QColor mix(const QColor &a, const QColor &b, qreal t)
{
    return QColor::fromRgbF(a.redF() + (b.redF() - a.redF()) * t,
                            a.greenF() + (b.greenF() - a.greenF()) * t,
                            a.blueF() + (b.blueF() - a.blueF()) * t);
}

QString readText(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning("Theme: cannot read %s", qUtf8Printable(path));
        return {};
    }
    return QString::fromUtf8(file.readAll());
}

QJsonObject readObject(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning("Theme: cannot read %s", qUtf8Printable(path));
        return {};
    }
    QJsonParseError error {};
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &error);
    if (!doc.isObject()) {
        qWarning("Theme: %s is not a definition: %s", qUtf8Printable(path),
                 qUtf8Printable(error.errorString()));
        return {};
    }
    return doc.object();
}

// A colour a scheme does not give is refused rather than guessed at: half a
// palette is worse to look at than a scheme that never appears in the list.
bool readColor(const QJsonObject &from, const char *name, QColor &into, const QString &path)
{
    const QColor color(from.value(QLatin1String(name)).toString());
    if (!color.isValid()) {
        qWarning("Theme: %s says nothing usable for \"%s\"", qUtf8Printable(path), name);
        return false;
    }
    into = color;
    return true;
}

bool readScheme(const QString &path, Scheme &scheme)
{
    const QJsonObject from = readObject(path);
    if (from.isEmpty())
        return false;

    // The file's name is the key the settings store, the way a converter is
    // known by its folder: one place a scheme's identity is written, so the
    // file and what is in it cannot disagree about which scheme it is.
    scheme.key = QFileInfo(path).completeBaseName();
    scheme.label = from.value(QStringLiteral("label")).toString(scheme.key);
    scheme.order = from.value(QStringLiteral("order")).toInt(kLast);
    scheme.dark = from.value(QStringLiteral("dark")).toBool();

    static const char *const names[] = { "window", "surface", "base", "alternate", "text",
                                         "muted", "border", "soft", "accent", "accentText" };
    QColor *const into[] = { &scheme.window, &scheme.surface, &scheme.base, &scheme.alternate,
                             &scheme.text, &scheme.muted, &scheme.border, &scheme.soft,
                             &scheme.accent, &scheme.accentText };
    for (size_t i = 0; i < std::size(names); ++i) {
        if (!readColor(from, names[i], *into[i], path))
            return false;
    }
    // These two are the scheme's to name and nobody has to.
    scheme.error = QColor(from.value(QStringLiteral("error")).toString());
    scheme.errorText = QColor(from.value(QStringLiteral("errorText")).toString());
    return true;
}

// A sheet may be one string or a list of lines. A style of any size is written
// as lines, and joining them here is what lets its file keep one rule to a row.
QString sheetValue(const QJsonValue &value)
{
    if (!value.isArray())
        return value.toString();
    QStringList lines;
    for (const QJsonValue &line : value.toArray())
        lines << line.toString();
    return lines.join(QLatin1Char('\n'));
}

bool readStyle(const QString &path, Style &style)
{
    const QJsonObject from = readObject(path);
    if (from.isEmpty())
        return false;

    style.key = QFileInfo(path).completeBaseName();
    style.label = from.value(QStringLiteral("label")).toString(style.key);
    style.order = from.value(QStringLiteral("order")).toInt(kLast);

    const QJsonObject radius = from.value(QStringLiteral("radius")).toObject();
    for (auto it = radius.begin(); it != radius.end(); ++it)
        style.radius.insert(it.key(), it.value().toInt());
    const QJsonObject padding = from.value(QStringLiteral("padding")).toObject();
    for (auto it = padding.begin(); it != padding.end(); ++it)
        style.padding.insert(it.key(), it.value().toString());

    // A run of rules two styles share is a file they both name rather than a
    // copy in each. Before the style's own, so its own still wins.
    const QDir snippets(QFileInfo(path).dir().absoluteFilePath(QStringLiteral("snippets")));
    for (const QJsonValue &name : from.value(QStringLiteral("include")).toArray())
        style.sheet += readText(snippets.absoluteFilePath(name.toString())) + QLatin1Char('\n');
    style.sheet += sheetValue(from.value(QStringLiteral("sheet")));
    return true;
}

// A definition read from a folder further down the list replaces the one of the
// same name read before it, rather than appearing beside it.
template <class T>
void adopt(QVector<T> &into, const T &what)
{
    for (T &have : into) {
        if (have.key == what.key) {
            have = what;
            return;
        }
    }
    into.append(what);
}

template <class T, class Read>
void readAll(const QDir &dir, QVector<T> &into, Read read)
{
    for (const QFileInfo &file : dir.entryInfoList({ QStringLiteral("*.json") }, QDir::Files)) {
        T one;
        if (read(file.absoluteFilePath(), one))
            adopt(into, one);
    }
}

struct Catalogue {
    QVector<Scheme> schemes;
    QVector<Style> styles;
    QString base; // the sheet every style starts from
};

Catalogue s_catalogue;
bool s_read = false;

// Read when they are first wanted, and again on reload(). The files are the
// program's own data, like the example scenes.
const Catalogue &catalogue()
{
    if (!s_read) {
        s_read = true;
        Catalogue c;
        for (const QString &path : assetFolders(QStringLiteral("themes"))) {
            const QDir dir(path);
            if (!dir.exists())
                continue;
            if (dir.exists(QStringLiteral("base.qss")))
                c.base = readText(dir.absoluteFilePath(QStringLiteral("base.qss")));
            readAll(QDir(dir.absoluteFilePath(QStringLiteral("schemes"))), c.schemes, readScheme);
            readAll(QDir(dir.absoluteFilePath(QStringLiteral("styles"))), c.styles, readStyle);
        }
        const auto before = [](const auto &a, const auto &b) {
            return a.order != b.order ? a.order < b.order
                                      : a.label.localeAwareCompare(b.label) < 0;
        };
        std::sort(c.schemes.begin(), c.schemes.end(), before);
        std::sort(c.styles.begin(), c.styles.end(), before);
        if (c.schemes.isEmpty() || c.styles.isEmpty() || c.base.isEmpty())
            qWarning("Theme: no definitions found; wearing the platform's own look");
        s_catalogue = c;
    }
    return s_catalogue;
}

const Scheme *findScheme(const QString &key)
{
    for (const Scheme &scheme : catalogue().schemes) {
        if (scheme.key == key)
            return &scheme;
    }
    return nullptr;
}

const Style *findStyle(const QString &key)
{
    for (const Style &style : catalogue().styles) {
        if (style.key == key)
            return &style;
    }
    return nullptr;
}

// With no definitions to read, the program still has to answer for every colour
// something asks it for. They come off the platform's own palette, which is the
// look it falls back to anyway.
Scheme schemeFromPalette(const QPalette &p)
{
    Scheme s;
    s.key = kSystem;
    s.dark = p.color(QPalette::Window).lightness() < p.color(QPalette::WindowText).lightness();
    s.window = p.color(QPalette::Window);
    s.surface = p.color(QPalette::Window);
    s.base = p.color(QPalette::Base);
    s.alternate = p.color(QPalette::AlternateBase);
    s.text = p.color(QPalette::WindowText);
    s.muted = mix(s.text, s.window, 0.35);
    s.border = p.color(QPalette::Mid);
    s.soft = p.color(QPalette::Midlight);
    s.accent = p.color(QPalette::Highlight);
    s.accentText = p.color(QPalette::HighlightedText);
    return s;
}

QString s_key = kSystem;
// The style as chosen, so "system" stays "system": s_style is then the first of
// them, for the card radii the panels still ask for.
QString s_controls;
// Held by value rather than as a pointer into the catalogue: where there is
// nothing to read, one worked out from the platform's palette stands in.
Scheme s_scheme;
Style s_style;
bool s_styled = false;
// The palette the platform handed over before Fusion and ours replaced it:
// where Qt cannot say whether the desktop is dark, its own window colour can.
// Both are what the System controls go back to.
QPalette s_platformPalette;
QString s_platformStyle;

// What is being worn, or what would be if nothing had been put on yet: the
// panels ask for a card's radius and for colours as they are built, which can
// be before the first apply(). One place answers for that, rather than every
// accessor having an opinion about it.
const Scheme &currentScheme()
{
    if (!s_scheme.accent.isValid())
        s_scheme = schemeFromPalette(QApplication::palette());
    return s_scheme;
}

const Style &currentStyle()
{
    if (s_style.key.isEmpty() && !catalogue().styles.isEmpty())
        s_style = catalogue().styles.constFirst();
    return s_style;
}

QPalette makePalette(const Scheme &scheme)
{
    const QColor faint = mix(scheme.text, scheme.window, 0.55);

    QPalette p;
    p.setColor(QPalette::Window, scheme.window);
    p.setColor(QPalette::WindowText, scheme.text);
    p.setColor(QPalette::Base, scheme.base);
    p.setColor(QPalette::AlternateBase, scheme.alternate);
    p.setColor(QPalette::Text, scheme.text);
    p.setColor(QPalette::Button, scheme.dark ? scheme.alternate : scheme.base);
    p.setColor(QPalette::ButtonText, scheme.text);
    p.setColor(QPalette::BrightText, Qt::white);
    p.setColor(QPalette::Light,
               scheme.dark ? mix(scheme.soft, scheme.text, 0.15) : QColor(Qt::white));
    p.setColor(QPalette::Midlight, scheme.soft);
    p.setColor(QPalette::Mid, scheme.border);
    p.setColor(QPalette::Dark, scheme.dark ? mix(scheme.window, Qt::black, 0.4)
                                           : mix(scheme.border, scheme.text, 0.3));
    p.setColor(QPalette::Shadow,
               scheme.dark ? QColor(Qt::black) : mix(scheme.border, scheme.text, 0.5));
    p.setColor(QPalette::Highlight, scheme.accent);
    p.setColor(QPalette::HighlightedText, scheme.accentText);
    p.setColor(QPalette::Link, scheme.accent);
    p.setColor(QPalette::LinkVisited, scheme.accent);
    p.setColor(QPalette::ToolTipBase, scheme.dark ? scheme.alternate : QColor(0x2B2D31));
    p.setColor(QPalette::ToolTipText, scheme.dark ? scheme.text : QColor(0xF0F1F3));
    p.setColor(QPalette::PlaceholderText, scheme.muted);

    p.setColor(QPalette::Disabled, QPalette::WindowText, faint);
    p.setColor(QPalette::Disabled, QPalette::Text, faint);
    p.setColor(QPalette::Disabled, QPalette::ButtonText, faint);
    p.setColor(QPalette::Disabled, QPalette::Highlight, scheme.border);
    p.setColor(QPalette::Disabled, QPalette::Base, scheme.window);
    return p;
}

Theme::Colors makeColors(const Scheme &scheme)
{
    Theme::Colors c;
    c.surface = scheme.surface;
    c.hover = mix(scheme.surface, scheme.text, scheme.dark ? 0.10 : 0.07);
    c.accent = scheme.dark ? mix(scheme.accent, Qt::white, 0.15) : scheme.accent;
    c.accentSoft = mix(scheme.window, scheme.accent, scheme.dark ? 0.28 : 0.16);
    c.borderStrong = mix(scheme.border, scheme.text, 0.3);
    c.mutedText = scheme.muted;
    c.faintText = mix(scheme.text, scheme.window, 0.55);
    c.glyph = mix(scheme.text, scheme.window, 0.25);
    c.glyphDisabled = mix(scheme.text, scheme.window, 0.75);

    // The one red that is not a scheme's own unless it says so: a warning reads
    // as a warning whichever palette it is drawn in.
    c.error = scheme.error.isValid() ? scheme.error
        : (scheme.dark ? QColor(0xE0, 0x6C, 0x75) : QColor(0xD9, 0x53, 0x4F));
    c.errorText = scheme.errorText.isValid() ? scheme.errorText
        : (scheme.dark ? QColor(0xF2, 0x8B, 0x82) : QColor(0xA3, 0x3A, 0x35));
    c.errorBackground = mix(scheme.base, c.error, 0.08);
    c.errorHeader = mix(scheme.base, c.error, scheme.dark ? 0.28 : 0.22);
    c.errorOff = mix(c.error, scheme.window, 0.5);
    c.errorTextOff = mix(c.errorText, scheme.window, 0.4);
    c.errorBackgroundOff = mix(scheme.window, c.error, 0.05);
    c.errorHeaderOff = mix(scheme.window, c.error, 0.12);
    return c;
}

Theme::Colors s_colors;

// The shared sheet with the style's own laid over it, and every @name in both
// filled in.
QString fillSheet(const Scheme &scheme, const Style &style, const Theme::Colors &c)
{
    // The arrows and the tick are images, and an image has one colour: light
    // ink for a dark scheme, dark ink for a light one, and the tick in
    // whichever reads on the accent.
    const QString ink = scheme.dark ? QStringLiteral("light") : QStringLiteral("dark");
    const QString tick = scheme.accentText.lightness() > 128 ? QStringLiteral("light")
                                                             : QStringLiteral("dark");
    // After the shared rules, so where both say something the style wins.
    QString sheet = catalogue().base + QLatin1Char('\n') + style.sheet;

    const QPalette palette = makePalette(scheme);
    QVector<std::pair<QString, QString>> values {
        { QStringLiteral("@surface"), c.surface.name() },
        { QStringLiteral("@hover"), c.hover.name() },
        { QStringLiteral("@accentSoft"), c.accentSoft.name() },
        { QStringLiteral("@accentHover"), mix(scheme.accent, scheme.text, 0.12).name() },
        { QStringLiteral("@accent"), c.accent.name() },
        { QStringLiteral("@borderStrong"), c.borderStrong.name() },
        { QStringLiteral("@muted"), c.mutedText.name() },
        { QStringLiteral("@faint"), c.faintText.name() },
        { QStringLiteral("@tipBase"), palette.color(QPalette::ToolTipBase).name() },
        { QStringLiteral("@tipText"), palette.color(QPalette::ToolTipText).name() },
        { QStringLiteral("@ink"), ink },
        { QStringLiteral("@tick"), tick },
    };
    for (auto it = style.radius.begin(); it != style.radius.end(); ++it)
        values.append({ QStringLiteral("@r") + it.key(), QStringLiteral("%1px").arg(it.value()) });
    for (auto it = style.padding.begin(); it != style.padding.end(); ++it)
        values.append({ QStringLiteral("@pad") + it.key(), it.value() });

    // Longest first: @r is the start of every radius name, @accent of two more.
    std::sort(values.begin(), values.end(),
              [](const auto &a, const auto &b) { return a.first.size() > b.first.size(); });
    for (const auto &[name, value] : values)
        sheet.replace(name, value);

    // A name nobody fills leaves the rule it stands in unparseable, and Qt
    // drops that rule without saying which it was. Worth a word, now that
    // these files are the user's to write.
    static const QRegularExpression unfilled(QStringLiteral("@[A-Za-z][A-Za-z0-9]*"));
    QSet<QString> left;
    auto found = unfilled.globalMatch(sheet);
    while (found.hasNext())
        left.insert(found.next().captured());
    if (!left.isEmpty()) {
        QStringList names(left.cbegin(), left.cend());
        names.sort();
        qWarning("Theme: nothing fills %s in %s", qUtf8Printable(names.join(QStringLiteral(", "))),
                 qUtf8Printable(style.key));
    }
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

} // namespace

Theme *Theme::instance()
{
    static Theme theme;
    return &theme;
}

QVector<Theme::Choice> Theme::available()
{
    QVector<Choice> choices { { kSystem, tr("System") } };
    for (const Scheme &scheme : catalogue().schemes)
        choices.append({ scheme.key, scheme.label });
    return choices;
}

QVector<Theme::Choice> Theme::availableControls()
{
    QVector<Choice> choices { { kSystem, tr("System") } };
    for (const Style &style : catalogue().styles)
        choices.append({ style.key, style.label });
    return choices;
}

void Theme::apply(const QString &theme, const QString &controls)
{
    const QString chosen = findScheme(theme) ? theme : kSystem;
    // An unknown style falls back to the first real one rather than to System:
    // a key nobody knows is a typo in the settings file, not a request for the
    // platform's own look.
    QString style = controls;
    if (style != kSystem && !findStyle(style))
        style = catalogue().styles.isEmpty() ? kSystem : catalogue().styles.constFirst().key;
    // Options hands every setting over each time it closes, and polishing
    // every widget again is not free.
    if (s_styled && chosen == s_key && style == s_controls)
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
    restyle(chosen, style);
}

void Theme::restyle(const QString &theme, const QString &controls)
{
    s_key = theme;
    s_controls = controls;
    // The native style paints its own chrome and reads little of a palette, so
    // a scheme laid over it would be half applied -- light text on a button
    // still drawn light. It wears the platform's own instead, and the cards
    // the panels paint follow the desktop so they match what is around them.
    const bool native = controls == kSystem || catalogue().base.isEmpty();
    const Scheme *scheme = native ? nullptr : findScheme(theme);
    if (!scheme)
        scheme = findScheme(systemIsDark() ? QStringLiteral("dark") : QStringLiteral("light"));
    s_scheme = scheme ? *scheme : schemeFromPalette(s_platformPalette);
    if (const Style *style = findStyle(controls))
        s_style = *style;
    else if (!catalogue().styles.isEmpty())
        s_style = catalogue().styles.first();
    s_colors = makeColors(s_scheme);

    // Before the palette: a style brings its own, and would undo ours.
    if (QStyle *style = QStyleFactory::create(native ? s_platformStyle
                                                     : QStringLiteral("Fusion")))
        QApplication::setStyle(style);
    QApplication::setPalette(native ? s_platformPalette : makePalette(s_scheme));
    // Set after the palette, and set every time: it is what makes Qt polish
    // every widget again, and so what makes the palette() references in it
    // read the new one.
    qApp->setStyleSheet(native ? QString() : fillSheet(s_scheme, s_style, s_colors));

    emit instance()->changed();
}

void Theme::reload()
{
    s_read = false;
    s_catalogue = Catalogue();
    s_scheme = Scheme();
    s_style = Style();
    s_colors = Colors();
    if (!s_styled)
        return;
    // The two keys have not changed, so apply() would see no change and do
    // nothing -- but what they name has changed underneath it.
    const QString theme = s_key;
    const QString controls = s_controls;
    s_key.clear();
    s_controls.clear();
    apply(theme, controls);
}

QString Theme::current()
{
    return s_key;
}

QString Theme::currentControls()
{
    if (s_controls.isEmpty())
        return catalogue().styles.isEmpty() ? kSystem : catalogue().styles.first().key;
    return s_controls;
}

int Theme::cardRadius()
{
    return currentStyle().radius.value(QStringLiteral("card"));
}

bool Theme::isDark()
{
    return currentScheme().dark;
}

const Theme::Colors &Theme::colors()
{
    if (!s_colors.accent.isValid())
        s_colors = makeColors(currentScheme());
    return s_colors;
}

QPalette Theme::lightPalette()
{
    if (const Scheme *light = findScheme(QStringLiteral("light")))
        return makePalette(*light);
    for (const Scheme &scheme : catalogue().schemes) {
        if (!scheme.dark)
            return makePalette(scheme);
    }
    return makePalette(schemeFromPalette(QApplication::palette()));
}
