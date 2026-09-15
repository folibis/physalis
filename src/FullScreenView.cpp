#include "FullScreenView.h"

#include "CanvasScene.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <cmath>
#include <QLabel>
#include <QStringList>
#include <QResizeEvent>

FullScreenView::FullScreenView(CanvasScene *scene, QWidget *parent)
    : QGraphicsView(scene, parent)
{
    setWindowFlag(Qt::Window);
    setWindowTitle(tr("Physalis — Simulation"));
    setFrameShape(QFrame::NoFrame);
    setRenderHint(QPainter::Antialiasing, true);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    // Nothing in the scene is draggable, selectable or editable: a click during
    // a run would otherwise pick a shape up. Dragging moves the view instead.
    setInteractive(false);
    setDragMode(QGraphicsView::ScrollHandDrag);
    // Zooming keeps whatever is under the pointer under the pointer.
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    setFocusPolicy(Qt::StrongFocus);

    // Which keys do what, said once. There is no toolbar in here to find out
    // from, and a full screen with no way back is a trap.
    // Each part translated whole, joined by a bar rather than a bullet -- the
    // label is rich text for the sake of the bar being bold.
    const QStringList parts { tr("Drag to pan"), tr("Wheel to zoom"),
                              tr("0 fits the field"), tr("Space holds"),
                              tr("→ steps a frame"), tr("Esc ends the run") };
    m_hint = new QLabel(parts.join(QStringLiteral("  <b>|</b>  ")), this);
    m_hint->setTextFormat(Qt::RichText);
    m_hint->setStyleSheet(QStringLiteral(
        "color: #F0F0F0; background: rgba(0, 0, 0, 140); padding: 6px 12px;"
        " border-radius: 6px;"));
    m_hint->adjustSize();
}

void FullScreenView::fitField()
{
    if (!scene())
        return;
    // The whole field, as large as it goes, in the middle of the screen. The
    // centring is not what fitInView leaves behind in every case -- a viewport
    // that has not settled at its full-screen size yet fits against the wrong
    // one -- so it is said outright, and again once the window is up.
    fitInView(scene()->sceneRect(), Qt::KeepAspectRatio);
    centerOn(scene()->sceneRect().center());
    m_moved = false;
}

void FullScreenView::placeHint()
{
    if (!m_hint)
        return;
    m_hint->adjustSize();
    m_hint->move((width() - m_hint->width()) / 2, height() - m_hint->height() - 24);
}

void FullScreenView::resizeEvent(QResizeEvent *event)
{
    QGraphicsView::resizeEvent(event);
    // Only while the view is still showing the whole field. Refitting after
    // the viewer has zoomed in would undo it.
    if (!m_moved)
        fitField();
    placeHint();
}

void FullScreenView::wheelEvent(QWheelEvent *event)
{
    const int steps = event->angleDelta().y();
    if (steps == 0) {
        QGraphicsView::wheelEvent(event);
        return;
    }

    // A notch is 120 eighths of a degree; a tenth per notch, and a limit at
    // each end so the field cannot be lost off the edge of the world.
    const qreal factor = std::pow(1.1, steps / 120.0);
    const qreal scale = transform().m11() * factor;
    if (scale > 0.02 && scale < 60.0)
        this->scale(factor, factor);
    m_moved = true;
    event->accept();
}

void FullScreenView::showEvent(QShowEvent *event)
{
    QGraphicsView::showEvent(event);
    fitField();
    placeHint();
    setFocus();
    // Full screen is granted by the window manager, not by asking: the first
    // fit is against whatever size the window had at the time. This one is
    // against the size it ended up with.
    QMetaObject::invokeMethod(this, [this] {
        if (!m_moved)
            fitField();
        placeHint();
    }, Qt::QueuedConnection);
}

// This view is not interactive -- a click must not pick a shape up -- so the
// scene never sees its mouse, and the slingshot is passed on by hand. A press
// on a body that can be shot is a shot; anywhere else it is the drag that pans.
void FullScreenView::mousePressEvent(QMouseEvent *event)
{
    auto *canvas = qobject_cast<CanvasScene *>(scene());
    if (canvas && canvas->isAimingShot()) {
        if (event->button() != Qt::LeftButton)
            canvas->cancelShot();
        event->accept();
        return;
    }
    if (canvas && event->button() == Qt::LeftButton
        && canvas->beginShot(mapToScene(event->position().toPoint()))) {
        event->accept();
        return;
    }
    QGraphicsView::mousePressEvent(event);
}

void FullScreenView::mouseMoveEvent(QMouseEvent *event)
{
    auto *canvas = qobject_cast<CanvasScene *>(scene());
    if (canvas && canvas->isAimingShot()) {
        canvas->aimShot(mapToScene(event->position().toPoint()));
        event->accept();
        return;
    }
    QGraphicsView::mouseMoveEvent(event);
}

void FullScreenView::mouseReleaseEvent(QMouseEvent *event)
{
    auto *canvas = qobject_cast<CanvasScene *>(scene());
    if (canvas && canvas->isAimingShot()) {
        if (event->button() == Qt::LeftButton)
            canvas->releaseShot();
        event->accept();
        return;
    }
    QGraphicsView::mouseReleaseEvent(event);
}

void FullScreenView::keyPressEvent(QKeyEvent *event)
{
    switch (event->key()) {
    case Qt::Key_Escape:
        // Mid-aim, escape calls the shot off rather than ending the run.
        if (auto *canvas = qobject_cast<CanvasScene *>(scene()); canvas && canvas->isAimingShot()) {
            canvas->cancelShot();
            return;
        }
        emit closeRequested();
        return;
    case Qt::Key_Space:
        emit holdRequested();
        return;
    case Qt::Key_Right:
        emit stepRequested();
        return;
    case Qt::Key_0:
        // Back to the whole field, for a viewer who has zoomed into a corner.
        fitField();
        return;
    default:
        break;
    }
    QGraphicsView::keyPressEvent(event);
}
