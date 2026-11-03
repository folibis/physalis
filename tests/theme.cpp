// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ruslan Muhlinin. See LICENSE.
#include "Icons.h"
#include "MainWindow.h"
#include "Theme.h"

#include <QApplication>
#include <QComboBox>
#include <QImage>
#include <QLabel>
#include <QSettings>
#include <QTabBar>
#include <QTabWidget>
#include <QVector>
#include <gtest/gtest.h>

namespace {

// The theme is the whole application's, so a test that changes it puts back
// what the next test would otherwise inherit.
struct KeepTheme {
    QString was = Theme::current();
    QString wasControls = Theme::currentControls();
    ~KeepTheme() { Theme::apply(was, wasControls); }
};

// How light an icon is drawn, over the pixels it actually covers.
double inkLightness(const QIcon &icon)
{
    const QImage image = icon.pixmap(32, 32).toImage();
    double sum = 0;
    int count = 0;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            const QColor pixel = image.pixelColor(x, y);
            if (pixel.alpha() > 200) {
                sum += pixel.lightness();
                ++count;
            }
        }
    }
    return count ? sum / count : 0;
}

} // namespace

TEST(Theme, OptionsSwitchesItWhileTheWindowIsOpen)
{
    KeepTheme keep;
    MainWindow window;

    OptionsDialog::Settings s = window.settingsSnapshotForTest();
    s.theme = QStringLiteral("dark");
    window.applySettingsForTest(s);
    EXPECT_TRUE(Theme::isDark());
    EXPECT_LT(QApplication::palette().color(QPalette::Window).lightness(), 80)
        << "Dark was chosen and the windows are still light";
    EXPECT_EQ(window.settingsSnapshotForTest().theme, QStringLiteral("dark"))
        << "the choice would not be written back to the settings";

    s.theme = QStringLiteral("light");
    window.applySettingsForTest(s);
    EXPECT_FALSE(Theme::isDark());
    EXPECT_GT(QApplication::palette().color(QPalette::Window).lightness(), 200)
        << "Light was chosen and the windows are still dark";
}

// Every theme Options offers has to be one that can be put on: a key that
// fell through to System would leave the user looking at something else. With a
// list this long the cheap mistakes are a copied line and a dark palette whose
// row still says light, so both are checked here rather than by eye.
TEST(Theme, EveryThemeOfferedCanBeWorn)
{
    KeepTheme keep;
    QVector<QColor> windows;
    for (const Theme::Choice &choice : Theme::available()) {
        Theme::apply(choice.key);
        EXPECT_EQ(Theme::current(), choice.key) << choice.key.toStdString() << " was not taken";
        EXPECT_FALSE(qApp->styleSheet().contains(QLatin1Char('@')))
            << choice.key.toStdString() << " left a placeholder in the stylesheet";

        const QColor window = QApplication::palette().color(QPalette::Window);
        const QColor text = QApplication::palette().color(QPalette::WindowText);
        if (choice.key != QStringLiteral("system")) {
            EXPECT_FALSE(windows.contains(window))
                << choice.key.toStdString() << " is the same colour as a theme before it";
            windows.append(window);
        }
        EXPECT_EQ(Theme::isDark(), window.lightness() < text.lightness())
            << choice.key.toStdString() << " disagrees with its own palette about being dark";
        EXPECT_GT(qAbs(window.lightness() - text.lightness()), 100)
            << choice.key.toStdString() << " writes in a colour too close to its background";
    }
    Theme::apply(QStringLiteral("no such theme"));
    EXPECT_EQ(Theme::current(), QStringLiteral("system")) << "an unknown key was kept";
}

// The controls are a choice of their own, worn with any theme, and each one
// has to change something: a style that left the sheet as it was would be a
// name in a list and nothing else.
TEST(Theme, EveryControlsStyleCanBeWornAndDiffers)
{
    KeepTheme keep;
    QStringList sheets;
    for (const Theme::Choice &choice : Theme::availableControls()) {
        Theme::apply(QStringLiteral("light"), choice.key);
        EXPECT_EQ(Theme::currentControls(), choice.key) << choice.key.toStdString() << " was not taken";
        EXPECT_FALSE(qApp->styleSheet().contains(QLatin1Char('@')))
            << choice.key.toStdString() << " left a placeholder in the stylesheet";
        EXPECT_FALSE(sheets.contains(qApp->styleSheet()))
            << choice.key.toStdString() << " looks exactly like a style before it";
        sheets.append(qApp->styleSheet());
    }

    // The first of them is not a shape at all: Qt draws the controls and
    // nothing of ours is laid over them.
    Theme::apply(QStringLiteral("light"), QStringLiteral("system"));
    EXPECT_TRUE(qApp->styleSheet().isEmpty())
        << "System controls still carried a sheet of ours";
    Theme::apply(QStringLiteral("light"), QStringLiteral("sharp"));
    EXPECT_FALSE(qApp->styleSheet().isEmpty())
        << "a style after System never got its sheet back";

    // A key nobody knows is a typo in the settings file, and falls back to the
    // first real shape rather than to no styling at all.
    Theme::apply(QStringLiteral("light"), QStringLiteral("no such style"));
    EXPECT_EQ(Theme::currentControls(), QStringLiteral("rounded"))
        << "an unknown controls style was kept, or left the controls unstyled";
}

// Both choices are written when Options closes, and a window opened later --
// the next time the program starts -- wears them again.
TEST(Theme, TheChoiceIsReadBackFromTheSettingsFile)
{
    KeepTheme keep;
    const QString path = qEnvironmentVariable("PHYSALIS_SETTINGS");
    ASSERT_FALSE(path.isEmpty()) << "the suite runs without its own settings file";

    Theme::apply(QStringLiteral("light"), QStringLiteral("rounded"));
    {
        QSettings settings(path, QSettings::IniFormat);
        settings.setValue("Appearance/theme", "mocha");
        settings.setValue("Appearance/controls", "material");
    }
    {
        MainWindow window;
        EXPECT_EQ(Theme::current(), QStringLiteral("mocha"))
            << "a window opened with the theme the settings file names ignored it";
        EXPECT_EQ(Theme::currentControls(), QStringLiteral("material"))
            << "a window opened with the controls the settings file names ignored them";
    }
    QSettings settings(path, QSettings::IniFormat);
    settings.remove("Appearance/theme");
    settings.remove("Appearance/controls");
}

// The two Appearance choices are worn as they are picked, so what you are
// choosing is the dialog in front of you. OK only writes the choice down.
TEST(Theme, OptionsPutsTheChoiceOnAsItIsMade)
{
    KeepTheme keep;
    OptionsDialog::Settings s;
    s.theme = QStringLiteral("light");
    s.controls = QStringLiteral("rounded");
    OptionsDialog dialog(s, nullptr);

    auto *controls = dialog.findChild<QComboBox *>(QStringLiteral("controls"));
    ASSERT_TRUE(controls);
    const int material = controls->findData(QStringLiteral("material"));
    ASSERT_GE(material, 0);
    controls->setCurrentIndex(material);
    EXPECT_EQ(Theme::currentControls(), QStringLiteral("material"))
        << "the shape was picked in the list and never put on";

    auto *theme = dialog.findChild<QComboBox *>(QStringLiteral("theme"));
    ASSERT_TRUE(theme);
    const int mocha = theme->findData(QStringLiteral("mocha"));
    ASSERT_GE(mocha, 0);
    theme->setCurrentIndex(mocha);
    EXPECT_TRUE(Theme::isDark()) << "the theme was picked in the list and never put on";
    EXPECT_EQ(Theme::currentControls(), QStringLiteral("material"))
        << "choosing a theme threw away the shape chosen beside it";
}

// The side panel's tabs are icons with the name only on the one in hand, and
// the sheet's padding is sized for a word. Every style has to take it off them
// again, or six icons no longer fit across the panel.
TEST(Theme, AnIconTabIsNoWiderThanItsIcon)
{
    KeepTheme keep;
    MainWindow window;
    auto *tabs = window.findChild<QTabWidget *>(QStringLiteral("sidePanel"));
    ASSERT_TRUE(tabs);
    ASSERT_GT(tabs->count(), 1);

    for (const Theme::Choice &choice : Theme::availableControls()) {
        if (choice.key == QStringLiteral("system"))
            continue; // Qt's own metrics, which are not ours to answer for.
        Theme::apply(QStringLiteral("dark"), choice.key);
        QTabBar *bar = tabs->tabBar();
        const int at = tabs->currentIndex() == 0 ? 1 : 0;
        ASSERT_TRUE(bar->tabText(at).isEmpty());
        EXPECT_LE(bar->tabRect(at).width(), tabs->iconSize().width() + 24)
            << choice.key.toStdString() << " gives an icon the room of a label";
    }
}

// The grey strokes are ink: on a dark panel they would all but vanish, so
// they are drawn lighter there. The coloured ones read on either.
TEST(Theme, AnIconsGreysAreLiftedOnDark)
{
    KeepTheme keep;

    Theme::apply(QStringLiteral("light"));
    const QIcon greyIcon = Icons::clone();
    const double onLight = inkLightness(greyIcon);
    const double greenOnLight = inkLightness(Icons::simulate());

    Theme::apply(QStringLiteral("dark"));
    EXPECT_GT(inkLightness(greyIcon), onLight + 40)
        << "the same icon kept its dark grey after the theme went dark";
    EXPECT_NEAR(inkLightness(Icons::simulate()), greenOnLight, 1.0)
        << "a coloured icon was recoloured as well";
}

// The caption and the scene's controls stand on the field, and the field is
// the scene's colour, not the theme's.
TEST(Theme, WhatStandsOnTheFieldKeepsALightPalette)
{
    KeepTheme keep;
    MainWindow window;
    Theme::apply(QStringLiteral("mocha"));

    auto *caption = window.findChild<QLabel *>(QStringLiteral("SceneCaption"));
    ASSERT_TRUE(caption);
    EXPECT_LT(caption->palette().color(QPalette::WindowText).lightness(), 100)
        << "the caption went light-on-light over the field";

    auto *controls = window.findChild<QWidget *>(QStringLiteral("SceneControls"));
    ASSERT_TRUE(controls);
    EXPECT_LT(controls->palette().color(QPalette::WindowText).lightness(), 100)
        << "the scene's controls went light-on-light over the field";
}
