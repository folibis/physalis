#include "OptionsDialog.h"

#include <QApplication>
#include <gtest/gtest.h>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QPainter>
#include <QPixmap>
#include <QSpinBox>
#include <QTabWidget>
#include <QLabel>
#include <QSlider>
#include <QLabel>
#include <QSlider>
#include <QToolButton>
#include <cstdio>

static const char *kScratch = "";

static int inked(const QPixmap &p)
{
    const QImage im = p.toImage();
    int n = 0;
    for (int y = 0; y < im.height(); ++y)
        for (int x = 0; x < im.width(); ++x) {
            const QColor c = im.pixelColor(x, y);
            if (c.red() < 220 || c.green() < 220 || c.blue() < 220) ++n;
        }
    return n;
}

TEST(OptionsVisual, Behaves)
{
    OptionsDialog::Settings s;
    s.fieldWidth = 2000; s.fieldHeight = 600; s.gridCellSize = 20;
    s.snapStep = 10; s.snapSensitivity = 4; s.undoDepth = 50;
    s.simulationEngineName = QStringLiteral("Box2D");

    OptionsDialog dialog(s, nullptr);
    dialog.resize(560, 900);
    dialog.show();
    for (int i = 0; i < 40; ++i) QCoreApplication::processEvents();

    auto *tabs = dialog.findChild<QTabWidget *>("tabs");
    EXPECT_TRUE(tabs && tabs->count() == 4) << "four tabs" << " -- " << (tabs ? QStringLiteral("%1").arg(tabs->count()) : QString()).toStdString();
    for (int i = 0; tabs && i < tabs->count(); ++i) {
        tabs->setCurrentIndex(i);
        for (int n = 0; n < 20; ++n) QCoreApplication::processEvents();
        const int ink = inked(tabs->widget(i)->grab());
        EXPECT_TRUE(ink > 2000) << tabs->tabText(i).toUtf8().constData() << " -- " << (QStringLiteral("%1 inked").arg(ink)).toStdString();
        dialog.grab().save(QString::fromLatin1(kScratch)
                           + QStringLiteral("options_%1.png").arg(tabs->tabText(i).toLower()));
    }

    EXPECT_TRUE(dialog.findChildren<QDoubleSpinBox *>().size()
              + dialog.findChildren<QSpinBox *>().size()
              + dialog.findChildren<QComboBox *>().size()
              + dialog.findChildren<QCheckBox *>().size() >= 25) << "51 form widgets found";
    EXPECT_TRUE(dialog.findChild<QDoubleSpinBox *>("fieldWidth")->value() == 2000.0) << "field width kept its value";
    EXPECT_TRUE(dialog.findChild<QSpinBox *>("undoDepth")->value() == 50) << "undo depth kept its value";
    EXPECT_TRUE(qFuzzyCompare(dialog.findChild<QDoubleSpinBox *>("snapSensitivity")->maximum(), 5.0)) << "snap sensitivity capped at half the step";

    // The joint-type rows are built from the engine, not the form.
    auto *jointGroup = dialog.findChild<QWidget *>("jointTypeColorsGroup");
    const int jointSwatches = jointGroup ? jointGroup->findChildren<QToolButton *>().size() : 0;
    EXPECT_TRUE(jointSwatches >= 6) << "joint type colour rows built from the engine" << " -- " << (QStringLiteral("%1 swatches").arg(jointSwatches)).toStdString();

    for (const char *name : {"defaultTransparency", "physicsFillAlpha", "sleepShiftPercent"}) {
        auto *sl = dialog.findChild<QSlider *>(name);
        auto *lb = dialog.findChild<QLabel *>(QString::fromLatin1(name) + "Label");
        const bool shows = sl && lb && lb->text().startsWith(QString::number(sl->value()));
        const bool left = sl && lb
            && lb->mapTo(&dialog, QPoint(0, 0)).x() < sl->mapTo(&dialog, QPoint(0, 0)).x();
        EXPECT_TRUE(shows && left) << name << " -- " << (lb ? QStringLiteral("reads %1").arg(lb->text()) : QStringLiteral("no label")).toStdString();
    }

    const OptionsDialog::Settings out = dialog.settings();
    EXPECT_TRUE(out.fieldWidth == 2000.0 && out.fieldHeight == 600.0) << "field size round-trips";
    EXPECT_TRUE(out.undoDepth == 50) << "undo depth round-trips";
    EXPECT_TRUE(out.gridCellSize == 20.0) << "grid cell size round-trips";
    EXPECT_TRUE(!out.jointTypeColors.isEmpty()) << "a joint type colour came back" << " -- " << (QStringLiteral("%1 types").arg(out.jointTypeColors.size())).toStdString();

    dialog.close();
}
