#include "RulerWidget.h"

#include "CanvasScene.h"
#include "Theme.h"

#include <QActionGroup>
#include <QContextMenuEvent>
#include <QFontMetrics>
#include <QGraphicsView>
#include <QMenu>
#include <QScrollBar>
#include <QPainter>
#include <cmath>

namespace {
// What the ticks are allowed to crowd to, in device pixels. The step is then
// the next round number in the chosen unit that clears them -- a fixed stride
// in scene units cannot work, since one scene unit is a different distance in
// every unit and at every zoom, and at 10% it put a tick every two pixels.
constexpr qreal kMinLabelGap = 64.0;
constexpr qreal kMinMinorGap = 6.0;
}

RulerWidget::RulerWidget(QWidget *parent)
    : QWidget(parent)
{
    setOrientation(m_orientation);
}

RulerWidget::RulerWidget(Orientation orientation, QGraphicsView *view, QWidget *parent)
    : QWidget(parent)
{
    setOrientation(orientation);
    setView(view);
}

void RulerWidget::setOrientation(Orientation orientation)
{
    m_orientation = orientation;

    // The previous orientation must be lifted first: setFixedHeight pins
    // minimum and maximum alike, so a ruler built horizontal and then turned
    // vertical would keep that height and collapse to a square.
    setMinimumSize(0, 0);
    setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
    if (m_orientation == Orientation::Horizontal)
        setFixedHeight(kThickness);
    else
        setFixedWidth(kThickness);
    updateGeometry();
    update();
}

void RulerWidget::setView(QGraphicsView *view)
{
    if (m_view == view)
        return;
    if (m_view) {
        disconnect(m_view->horizontalScrollBar(), nullptr, this, nullptr);
        disconnect(m_view->verticalScrollBar(), nullptr, this, nullptr);
        if (CanvasScene *was = scene())
            disconnect(was, nullptr, this, nullptr);
    }
    m_view = view;
    if (m_view) {
        connect(m_view->horizontalScrollBar(), &QScrollBar::valueChanged, this,
                [this] { update(); });
        connect(m_view->verticalScrollBar(), &QScrollBar::valueChanged, this,
                [this] { update(); });
        if (CanvasScene *now = scene()) {
            // Zooming moves every tick, and so does re-declaring how big a
            // metre is -- neither necessarily touches a scroll bar.
            connect(now, &CanvasScene::scaleChanged, this, [this] { update(); });
            connect(now, &CanvasScene::fieldPropertyChanged, this, [this] { update(); });
        }
    }
    update();
}

void RulerWidget::setUnit(Units::Unit unit)
{
    if (m_unit == unit)
        return;
    m_unit = unit;
    update();
}

CanvasScene *RulerWidget::scene() const
{
    return m_view ? qobject_cast<CanvasScene *>(m_view->scene()) : nullptr;
}

qreal RulerWidget::viewScale() const
{
    if (!m_view)
        return 1.0;
    const qreal scale = m_view->transform().m11();
    return scale > 0.0 ? scale : 1.0;
}

QSize RulerWidget::sizeHint() const
{
    return m_orientation == Orientation::Horizontal ? QSize(200, kThickness) : QSize(kThickness, 200);
}

void RulerWidget::contextMenuEvent(QContextMenuEvent *event)
{
    QMenu menu(this);
    menu.addSection(tr("Measure in"));
    auto *group = new QActionGroup(&menu);
    group->setExclusive(true);
    for (const Units::Entry &entry : Units::all()) {
        QAction *action = menu.addAction(entry.label);
        action->setCheckable(true);
        action->setChecked(entry.unit == m_unit);
        group->addAction(action);
        const Units::Unit picked = entry.unit;
        connect(action, &QAction::triggered, this, [this, picked] { emit unitPicked(picked); });
    }
    menu.exec(event->globalPos());
    event->accept();
}

void RulerWidget::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.fillRect(rect(), palette().window());
    painter.setPen(palette().color(QPalette::Mid));
    if (m_orientation == Orientation::Horizontal)
        painter.drawLine(0, height() - 1, width() - 1, height() - 1);
    else
        painter.drawLine(width() - 1, 0, width() - 1, height() - 1);

    QFont font = painter.font();
    font.setPixelSize(9);
    painter.setFont(font);

    if (!m_view)
        return;

    const bool horizontal = (m_orientation == Orientation::Horizontal);

    // Scene positions mapped to screen, not the reverse: a fixed screen-pixel
    // stride almost never lands on a round scene coordinate under zoom.
    QRectF visibleScene = m_view->mapToScene(m_view->viewport()->rect()).boundingRect();
    if (m_view->scene())
        visibleScene = visibleScene.intersected(m_view->scene()->sceneRect());
    const qreal sceneFrom = horizontal ? visibleScene.left() : visibleScene.top();
    const qreal sceneTo = horizontal ? visibleScene.right() : visibleScene.bottom();
    if (!(sceneTo > sceneFrom))
        return;

    const qreal pixelsPerMeter = scene() ? scene()->pixelsPerMeter() : 100.0;
    // One of the chosen unit, in scene units and then on screen. Everything
    // below is stepped in the unit being shown, which is the only way the
    // numbers come out round in it.
    const qreal scenePerUnit = Units::sceneUnitsPer(m_unit, pixelsPerMeter);
    const qreal screenPerUnit = viewScale() * scenePerUnit;
    if (!(screenPerUnit > 0.0) || !std::isfinite(screenPerUnit))
        return;

    const qreal labelStep = Units::niceStep(kMinLabelGap / screenPerUnit);
    // Fifths of the labelled step, unless they would be too close together to
    // tell apart -- then the labelled step is the only tick there is.
    qreal minorStep = labelStep / 5.0;
    if (minorStep * screenPerUnit < kMinMinorGap)
        minorStep = labelStep;

    const qreal fromUnits = sceneFrom / scenePerUnit;
    const qreal toUnits = sceneTo / scenePerUnit;
    const qreal firstTick = std::floor(fromUnits / minorStep) * minorStep;
    const int ticks = int((toUnits - firstTick) / minorStep) + 1;
    // However far out the view is, the ruler is at most a tick per pixel.
    if (ticks <= 0 || ticks > 20000)
        return;

    const QFontMetrics metrics(font);
    const QString unitSuffix = Units::suffix(m_unit);
    const int gap = 2;

    for (int i = 0; i < ticks; ++i) {
        const qreal inUnits = firstTick + i * minorStep;
        const qreal sceneValue = inUnits * scenePerUnit;
        const QPointF scenePoint = horizontal ? QPointF(sceneValue, 0) : QPointF(0, sceneValue);
        const QPoint viewportPoint = m_view->mapFromScene(scenePoint);
        const int p = horizontal ? viewportPoint.x() : viewportPoint.y();

        // Measured against the labelled step rather than by counting ticks:
        // the fifths are exact in neither binary nor the scene's own units,
        // and a count drifts off the round numbers over a long ruler.
        const qreal offLabel = std::fmod(std::fabs(inUnits), labelStep);
        const bool isMajor = offLabel < minorStep * 0.01
                             || std::fabs(offLabel - labelStep) < minorStep * 0.01;

        const int tickLen = isMajor ? 10 : 5;
        painter.setPen(Theme::colors().mutedText);
        if (horizontal)
            painter.drawLine(p, height() - tickLen, p, height() - 1);
        else
            painter.drawLine(width() - tickLen, p, width() - 1, p);

        if (!isMajor)
            continue;

        // The unit rides on the zero mark, where it says what the whole ruler
        // is in without repeating itself down the length of it. Found by
        // value: at a fine step the origin is written "0.0" or "0.000", and
        // matching the text left the unit off exactly where it belongs.
        const bool atOrigin = std::fabs(inUnits) < minorStep * 0.01;
        const QString label = atOrigin
                                  ? unitSuffix
                                  : Units::format(sceneValue, m_unit, pixelsPerMeter, labelStep);
        painter.setPen(palette().color(QPalette::WindowText));
        if (horizontal) {
            const int half = qMax(20, metrics.horizontalAdvance(label) / 2 + 4);
            painter.drawText(QRect(p - half, 0, half * 2, height() - tickLen - gap),
                             Qt::AlignHCenter | Qt::AlignBottom, label);
        } else {
            painter.save();
            painter.translate(0, p);
            painter.rotate(-90);
            const int half = qMax(40, metrics.horizontalAdvance(label) / 2 + 4);
            painter.drawText(QRect(-half, 0, half * 2, width() - tickLen - gap),
                             Qt::AlignHCenter | Qt::AlignBottom, label);
            painter.restore();
        }
    }
}
