// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ruslan Muhlinin. See LICENSE.
#include "PlotPanel.h"

#include "CanvasScene.h"
#include "SimulationController.h"

#include <QPainter>
#include <QPaintEvent>
#include <QContextMenuEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QFileDialog>
#include <QFile>
#include <QMessageBox>
#include <QPixmap>
#include <QTextStream>
#include <QDir>
#include <QtMath>
#include <algorithm>
#include <cmath>

namespace {

// Enough to tell traces apart at a glance, and the same wherever they are
// drawn, so a series keeps its colour between runs.
constexpr int kSeriesColorCount = 6;
const QColor kSeriesColors[kSeriesColorCount] = {
    QColor(0xE8, 0x8A, 0x6A), QColor(0x6A, 0xA8, 0xE8), QColor(0x7A, 0xC0, 0x8A),
    QColor(0xE0, 0x6A, 0xA8), QColor(0xE8, 0xC4, 0x6A), QColor(0x9A, 0x8A, 0xD8),
};

// One trace gets a band of its own rather than the whole panel: a force and a
// position share no vertical axis worth sharing, and a single plot stretched
// down the tab says nothing more than a short one.
constexpr int kRowHeightMin = 90;
constexpr int kRowHeightMax = 190;
// Air between one trace and the next. Stacked flush they read as one tall
// chart with lines through it rather than as separate measurements.
constexpr int kRowGap = 16;
// Below this a band has no room for its own caption, so it is where sharing
// stops and the panel simply runs out.
constexpr int kRowHeightFloor = 44;

// A step that lands on 1, 2 or 5 times a power of ten, so the number beside a
// gridline is one a reader can hold in their head.
qreal niceStep(qreal span, int wanted)
{
    if (span <= 0.0 || wanted <= 0)
        return 1.0;
    const qreal rough = span / wanted;
    const qreal power = std::pow(10.0, std::floor(std::log10(rough)));
    const qreal scaled = rough / power;
    if (scaled < 1.5)
        return power;
    if (scaled < 3.0)
        return 2.0 * power;
    if (scaled < 7.0)
        return 5.0 * power;
    return 10.0 * power;
}

QString shortNumber(qreal value)
{
    if (qFuzzyIsNull(value))
        return QStringLiteral("0");
    const qreal size = qAbs(value);
    if (size >= 1e5 || size < 1e-3)
        return QString::number(value, 'g', 3);
    return QString::number(value, 'g', 4);
}

} // namespace

PlotPanel::PlotPanel(QWidget *parent) : QWidget(parent)
{
    setMinimumHeight(120);
}

void PlotPanel::attach(CanvasScene *scene, SimulationController *simulation)
{
    m_scene = scene;
    m_simulation = simulation;
    update();
}

void PlotPanel::refresh()
{
    update();
}

void PlotPanel::paintEvent(QPaintEvent *event)
{
    QPainter painter(this);
    painter.fillRect(event->rect(), palette().base());
    painter.setRenderHint(QPainter::Antialiasing);

    const QColor ink = palette().text().color();
    QColor faint = ink;
    faint.setAlpha(45);

    QVector<SimulationController::Series> series;
    if (m_simulation) {
        for (const SimulationController::Series &one : m_simulation->plots()) {
            if (one.points.size() > 1)
                series.append(one);
        }
    }

    if (series.isEmpty()) {
        painter.setPen(ink);
        const bool anyMarked = m_scene
                               && std::any_of(m_scene->watches().cbegin(),
                                              m_scene->watches().cend(),
                                              [](const CanvasScene::Watch &w) { return w.plotted; });
        painter.drawText(rect().adjusted(12, 0, -12, 0), Qt::AlignCenter | Qt::TextWordWrap,
                         anyMarked
                             ? tr("Play the scene to trace what was marked.")
                             : tr("Right-click a property and choose \"Add to Log as Plot\"."));
        return;
    }

    const QFontMetrics metrics(font());
    // Room on the left for the axis numbers, and once at the bottom for the
    // clock -- the one axis every trace does share.
    const int gutter = metrics.horizontalAdvance(QStringLiteral("-0000.0")) + 8;
    const int clock = metrics.height() + 4;
    const int available = height() - clock;
    // Share out what there is. The floor only applies while the bands still
    // fit: holding every band at 70 px with three traces in a short panel ran
    // the last one off the bottom, which is worse than a thin one.
    const int rows = qMax(1, int(series.size()));
    const int share = (available - kRowGap * (rows - 1)) / rows;
    const int rowHeight = share >= kRowHeightMin ? qMin(share, kRowHeightMax)
                                                 : qMax(share, kRowHeightFloor);

    qreal lastSecond = 0.0;
    for (const SimulationController::Series &one : series)
        lastSecond = qMax(lastSecond, one.points.constLast().x());
    if (lastSecond <= 0.0)
        lastSecond = 1.0;
    // A slice of the run rather than all of it, ending where the drag left it.
    const qreal shown = m_shownSeconds > 0.0 ? qMin(m_shownSeconds, lastSecond) : lastSecond;
    const qreal offset = qBound(0.0, m_endOffset, qMax(0.0, lastSecond - shown));
    const qreal windowEnd = lastSecond - offset;
    const qreal firstSecond = windowEnd - shown;
    lastSecond = windowEnd;
    // Wide enough that the numbers under the axis keep clear of each other:
    // a tick every 60 px of a narrow panel is a smear, not a scale.
    const int roomForTicks = qMax(2, (width() - 90) / qMax(40, metrics.horizontalAdvance(
                                                                  QStringLiteral("00.0s")) + 18));
    const qreal tStep = niceStep(shown, qMin(5, roomForTicks));

    int top = 0;
    for (int i = 0; i < series.size(); ++i) {
        const SimulationController::Series &one = series.at(i);
        const QColor colour = kSeriesColors[i % kSeriesColorCount];
        // The name sits above its own band, so a tall stack still says which
        // trace is which without a legend somewhere else.
        const QRectF caption(gutter, top, width() - gutter - 6, metrics.height());
        const QRectF frame(gutter, top + metrics.height() + 4,
                           qMax(10, width() - gutter - 8),
                           qMax(10, rowHeight - metrics.height() - 8));

        // The whole trace, not the part on screen. Measured over the window it
        // moved under every new sample, and a scale that shifts as you watch
        // is one you cannot read a value off at all.
        qreal low = one.points.constFirst().y();
        qreal high = low;
        for (const QPointF &point : one.points) {
            low = qMin(low, point.y());
            high = qMax(high, point.y());
        }
        // A trace that never moves still deserves to be visible, so a flat one
        // is given room either side rather than dividing by nothing.
        if (qFuzzyCompare(low, high) || qFuzzyIsNull(high - low)) {
            const qreal pad = qMax(qAbs(low) * 0.1, 1.0);
            low -= pad;
            high += pad;
        }
        // A little room above and below what the trace reaches, so the peaks
        // are inside the frame rather than drawn along its edge.
        const qreal margin = (high - low) * 0.08;
        low -= margin;
        high += margin;

        painter.setPen(colour);
        painter.drawText(caption, Qt::AlignLeft | Qt::AlignVCenter,
                         tr("%1 · %2   %3")
                             .arg(one.object.isEmpty() ? tr("World") : one.object, one.label,
                                  shortNumber(one.points.constLast().y())));

        // Its own vertical axis: the two ends, and the middle only where three
        // numbers fit without sitting on each other.
        const int vMarks = frame.height() >= metrics.height() * 3.5 ? 2 : 1;
        for (int mark = 0; mark <= vMarks; ++mark) {
            const qreal v = low + (high - low) * mark / qreal(vMarks);
            const qreal y = frame.bottom() - frame.height() * (mark / qreal(vMarks));
            painter.setPen(QPen(faint, 1.0));
            painter.drawLine(QPointF(frame.left(), y), QPointF(frame.right(), y));
            painter.setPen(ink);
            painter.drawText(QRectF(0, y - metrics.height() / 2.0, gutter - 4, metrics.height()),
                             Qt::AlignRight | Qt::AlignVCenter, shortNumber(v));
        }

        painter.setPen(QPen(faint, 1.0));
        for (qreal t = std::ceil(firstSecond / tStep) * tStep; t <= lastSecond; t += tStep) {
            const qreal x = frame.left() + frame.width() * ((t - firstSecond) / shown);
            painter.drawLine(QPointF(x, frame.top()), QPointF(x, frame.bottom()));
        }
        painter.drawRect(frame);

        QPainterPath path;
        for (int p = 0; p < one.points.size(); ++p) {
            const QPointF &point = one.points.at(p);
            if (point.x() < firstSecond)
                continue;
            const qreal x = frame.left() + frame.width() * ((point.x() - firstSecond) / shown);
            const qreal y = frame.bottom() - frame.height() * ((point.y() - low) / (high - low));
            if (path.elementCount() == 0)
                path.moveTo(x, y);
            else
                path.lineTo(x, y);
        }
        painter.setPen(QPen(colour, 1.6));
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(path);

        // The clock, under the last band only: they all share it.
        if (i == series.size() - 1) {
            painter.setPen(ink);
            for (qreal t = std::ceil(firstSecond / tStep) * tStep;
                 t <= lastSecond + tStep * 0.5; t += tStep) {
                const qreal x = frame.left() + frame.width() * ((t - firstSecond) / shown);
                painter.drawText(QRectF(x - 40, frame.bottom() + 2, 80, clock),
                                 Qt::AlignHCenter | Qt::AlignTop,
                                 tr("%1s").arg(shortNumber(t)));
            }
        }

        m_secondsPerPixel = frame.width() > 0.0 ? shown / frame.width() : 0.0;
        top += rowHeight + kRowGap;
    }
}

void PlotPanel::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(event);
        return;
    }
    m_dragging = true;
    m_dragFrom = event->pos();
    m_dragOffsetFrom = m_endOffset;
    setCursor(Qt::ClosedHandCursor);
    event->accept();
}

void PlotPanel::mouseMoveEvent(QMouseEvent *event)
{
    if (!m_dragging) {
        QWidget::mouseMoveEvent(event);
        return;
    }
    // Dragging right walks back through the run, the way dragging a page does.
    m_endOffset = qMax(0.0, m_dragOffsetFrom
                                + (event->pos().x() - m_dragFrom.x()) * m_secondsPerPixel);
    update();
    event->accept();
}

void PlotPanel::mouseReleaseEvent(QMouseEvent *event)
{
    if (m_dragging) {
        m_dragging = false;
        unsetCursor();
        event->accept();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

void PlotPanel::wheelEvent(QWheelEvent *event)
{
    if (!m_simulation || m_simulation->plots().isEmpty()) {
        QWidget::wheelEvent(event);
        return;
    }

    qreal longest = 0.0;
    for (const SimulationController::Series &one : m_simulation->plots()) {
        if (!one.points.isEmpty())
            longest = qMax(longest, one.points.constLast().x());
    }
    if (longest <= 0.0) {
        QWidget::wheelEvent(event);
        return;
    }

    const qreal steps = event->angleDelta().y() / 120.0;
    if (qFuzzyIsNull(steps)) {
        QWidget::wheelEvent(event);
        return;
    }
    const qreal was = m_shownSeconds > 0.0 ? m_shownSeconds : longest;
    // A notch is a fifth either way, and widening past the run shows all of it
    // again rather than stopping short of the start.
    qreal wanted = was * std::pow(0.8, steps);
    if (wanted >= longest) {
        m_shownSeconds = 0.0;
    } else {
        // Never so narrow that there is nothing between two samples to draw.
        m_shownSeconds = qMax(wanted, 0.1);
    }
    update();
    event->accept();
}

void PlotPanel::contextMenuEvent(QContextMenuEvent *event)
{
    QMenu menu;
    QAction *clear = menu.addAction(tr("Clear Traces"));
    clear->setEnabled(m_simulation && !m_simulation->plots().isEmpty());
    QAction *all = menu.addAction(tr("Show Whole Run"));
    all->setEnabled(m_shownSeconds > 0.0);
    menu.addSeparator();
    const bool anything = m_simulation && !m_simulation->plots().isEmpty();
    QAction *csv = menu.addAction(tr("Save as CSV..."));
    csv->setEnabled(anything);
    QAction *image = menu.addAction(tr("Save as Image..."));
    image->setEnabled(anything);

    QAction *chosen = menu.exec(event->globalPos());
    if (chosen == clear && m_simulation) {
        m_simulation->clearPlots();
        update();
    } else if (chosen == all) {
        m_shownSeconds = 0.0;
        m_endOffset = 0.0;
        update();
    } else if (chosen == csv) {
        saveCsv();
    } else if (chosen == image) {
        saveImage();
    }
    event->accept();
}

// The numbers behind the picture. Every series was sampled on the same step,
// so they share one time column rather than repeating it per trace.
void PlotPanel::saveCsv()
{
    if (!m_simulation || m_simulation->plots().isEmpty())
        return;
    const QString path = QFileDialog::getSaveFileName(this, tr("Save Traces"), QString(),
                                                      tr("Comma-separated values (*.csv)"));
    if (path.isEmpty())
        return;

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, tr("Save Traces"),
                             tr("Could not write %1.").arg(QDir::toNativeSeparators(path)));
        return;
    }

    QVector<SimulationController::Series> series;
    for (const SimulationController::Series &one : m_simulation->plots())
        series.append(one);

    QTextStream out(&file);
    out << "time";
    int longest = 0;
    for (const SimulationController::Series &one : series) {
        QString label = one.label;
        label.replace(QLatin1Char('"'), QLatin1String("\"\""));
        out << ",\"" << label << '"';
        longest = qMax(longest, int(one.points.size()));
    }
    out << QLatin1Char('\n');

    for (int row = 0; row < longest; ++row) {
        // The time of whichever series has reached this row; they advance
        // together, so the first that has it is the time for all of them.
        qreal when = 0.0;
        for (const SimulationController::Series &one : series) {
            if (row < one.points.size()) {
                when = one.points.at(row).x();
                break;
            }
        }
        out << QString::number(when, 'g', 10);
        for (const SimulationController::Series &one : series) {
            out << ',';
            if (row < one.points.size())
                out << QString::number(one.points.at(row).y(), 'g', 10);
        }
        out << QLatin1Char('\n');
    }
}

void PlotPanel::saveImage()
{
    if (!m_simulation || m_simulation->plots().isEmpty())
        return;
    const QString path = QFileDialog::getSaveFileName(this, tr("Save Plot"), QString(),
                                                      tr("PNG image (*.png)"));
    if (path.isEmpty())
        return;

    // Twice the size on screen, so the numbers are still readable in whatever
    // it is pasted into.
    QPixmap shot(size() * 2);
    shot.setDevicePixelRatio(2.0);
    shot.fill(palette().base().color());
    render(&shot);
    if (!shot.save(path)) {
        QMessageBox::warning(this, tr("Save Plot"),
                             tr("Could not write %1.").arg(QDir::toNativeSeparators(path)));
    }
}
