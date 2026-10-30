#include "FullScreenView.h"

#include "CanvasScene.h"
#include "PhysicsBody.h"

#include <QCoreApplication>
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
    // The same as the main view, which sets it in the form. Joints, body axes
    // and the rest of the run are painted in the scene's foreground, which
    // belongs to no item -- so nothing marks the ground they cover as dirty,
    // and the default minimal update leaves the last frame's joints behind as
    // the bodies move out from under them.
    setViewportUpdateMode(QGraphicsView::FullViewportUpdate);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    // Nothing in the scene is draggable, selectable or editable: a click during
    // a run would otherwise pick a shape up. Dragging moves the view instead.
    setInteractive(false);
    setDragMode(QGraphicsView::ScrollHandDrag);
    // Zooming keeps whatever is under the pointer under the pointer.
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    setFocusPolicy(Qt::StrongFocus);

    // What this view still does, said once: a full screen with no way back is
    // a trap and there is no toolbar in here to find Escape from. It says
    // nothing about Space, the arrows or 0 any more -- those keys belong to
    // the scene now, and a hint naming them would be claiming keys a rule may
    // have given quite another meaning.
    // Each part translated whole, joined by a bar rather than a bullet -- the
    // label is rich text for the sake of the bar being bold.
    const QStringList parts { tr("Drag to pan"), tr("Wheel to zoom"),
                              tr("Esc ends the run") };
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
        && canvas->beginShot(mapToScene(event->pos()))) {
        event->accept();
        return;
    }
    QGraphicsView::mousePressEvent(event);
}

void FullScreenView::mouseMoveEvent(QMouseEvent *event)
{
    auto *canvas = qobject_cast<CanvasScene *>(scene());
    if (canvas && canvas->isAimingShot()) {
        canvas->aimShot(mapToScene(event->pos()));
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
    // Escape is the way out and stays the way out: without it a full-screen
    // run cannot be left from the keyboard at all. Everything else the view
    // used to keep -- Space to hold, Right to step, 0 to fit the field -- is
    // gone, so a scene is free to put a rule on any of them.
    switch (event->key()) {
    case Qt::Key_Escape:
        // Mid-aim, escape calls the shot off rather than ending the run.
        if (auto *canvas = qobject_cast<CanvasScene *>(scene()); canvas && canvas->isAimingShot()) {
            canvas->cancelShot();
            return;
        }
        emit closeRequested();
        return;
    default:
        break;
    }

    // Handed to the scene rather than left to the base class. This view is not
    // interactive -- it must never let a stray click drag a body around -- and
    // a QGraphicsView that is not interactive forwards nothing to its scene at
    // all. Keys are not clicks: a rule watching the keyboard has to hear them
    // here as much as in the editor, and heard nothing until this.
    if (scene())
        QCoreApplication::sendEvent(scene(), event);
}

// The scene moves around the body rather than the body across the screen.
void FullScreenView::followFocusedBody()
{
    auto *canvas = qobject_cast<CanvasScene *>(scene());
    if (!canvas)
        return;
    PhysicsBody *body = canvas->focusedBody();
    if (!body)
        return;
    // centerOn stops at the scene's rectangle, so a body that has left the
    // field would slide away with nothing following it; this view is given a
    // rectangle of its own that holds both.
    const QPointF at = body->originScenePos();
    const QRectF seen = mapToScene(viewport()->rect()).boundingRect();
    setSceneRect(canvas->sceneRect().united(
        QRectF(at, at).adjusted(-seen.width(), -seen.height(), seen.width(), seen.height())));
    centerOn(at);
}

void FullScreenView::keyReleaseEvent(QKeyEvent *event)
{
    if (scene())
        QCoreApplication::sendEvent(scene(), event);
}
