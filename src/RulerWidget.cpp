#include "RulerWidget.h"

#include <QGraphicsView>
#include <QScrollBar>
#include <QPainter>
#include <cmath>

namespace {
constexpr qreal kMinorStep = 20.0;
constexpr int kMajorStep = 100;
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
    }
    m_view = view;
    if (m_view) {
        connect(m_view->horizontalScrollBar(), &QScrollBar::valueChanged, this,
                [this] { update(); });
        connect(m_view->verticalScrollBar(), &QScrollBar::valueChanged, this,
                [this] { update(); });
    }
    update();
}

QSize RulerWidget::sizeHint() const
{
    return m_orientation == Orientation::Horizontal ? QSize(200, kThickness) : QSize(kThickness, 200);
}

void RulerWidget::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.fillRect(rect(), QColor(248, 248, 248));
    painter.setPen(QColor(150, 150, 150));
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
    const qreal firstTick = std::floor(sceneFrom / kMinorStep) * kMinorStep;

    for (qreal s = firstTick; s <= sceneTo; s += kMinorStep) {
        const QPointF scenePoint = horizontal ? QPointF(s, 0) : QPointF(0, s);
        const QPoint viewportPoint = m_view->mapFromScene(scenePoint);
        const int p = horizontal ? viewportPoint.x() : viewportPoint.y();

        const int roundedCoord = qRound(s);
        const bool isMajor = (roundedCoord % kMajorStep == 0);

        const int tickLen = isMajor ? 10 : 5;
        painter.setPen(QColor(120, 120, 120));
        if (horizontal)
            painter.drawLine(p, height() - tickLen, p, height() - 1);
        else
            painter.drawLine(width() - tickLen, p, width() - 1, p);

        if (isMajor) {
            const QString label = QString::number(roundedCoord);
            painter.setPen(QColor(90, 90, 90));
            const int gap = 2;
            if (horizontal) {
                painter.drawText(QRect(p - 20, 0, 40, height() - tickLen - gap),
                                  Qt::AlignHCenter | Qt::AlignBottom, label);
            } else {
                painter.save();
                painter.translate(0, p);
                painter.rotate(-90);
                painter.drawText(QRect(-40, 0, 80, width() - tickLen - gap),
                                  Qt::AlignHCenter | Qt::AlignBottom, label);
                painter.restore();
            }
        }
    }
}
