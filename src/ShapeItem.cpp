#include "ShapeItem.h"
#include "CanvasScene.h"
#include "PhysicsBody.h"

#include <QPainter>
#include <QPainterPathStroker>
#include <QStyleOptionGraphicsItem>
#include <QTransform>
#include <QtMath>

ShapeItem::ShapeItem()
    : m_rect(0, 0, 0, 0)
    , m_origin(0, 0)
{
    setTransformOriginPoint(m_origin);
}

void ShapeItem::setMode(ShapeMode mode)
{
    if (m_mode == mode)
        return;
    m_mode = mode;
    update();
    emit propertyChanged();
}

void ShapeItem::setRect(const QRectF &rect)
{
    prepareGeometryChange();
    m_rect = rect;
    update();
    emit propertyChanged();
}

void ShapeItem::setOrigin(const QPointF &origin)
{
    if (origin == m_origin)
        return;

    // Qt rotates/scales the item around transformOriginPoint, then applies
    // Qt rotates around transformOriginPoint and applies pos() on top, so
    // moving the origin shifts the shape unless pos() compensates.
    QTransform rot;
    rot.rotate(rotation());
    const QPointF compensation = (m_origin - rot.map(m_origin)) - (origin - rot.map(origin));

    prepareGeometryChange();
    m_origin = origin;
    setTransformOriginPoint(m_origin);
    setPos(pos() + compensation);
    update();
    emit propertyChanged();
}

void ShapeItem::setCoSelected(bool coSelected)
{
    if (m_coSelected == coSelected)
        return;
    m_coSelected = coSelected;
    update();
}

ShapeItem::~ShapeItem()
{
    if (m_body)
        m_body->removeShape(this);
}

void ShapeItem::setName(const QString &name)
{
    if (m_name == name)
        return;
    const auto *canvas = qobject_cast<const CanvasScene *>(scene());
    m_name = canvas ? canvas->uniqueName(name, this) : name;
    emit propertyChanged();
}

void ShapeItem::setBody(PhysicsBody *body)
{
    if (m_body == body)
        return;
    m_body = body;
    update();
    emit propertyChanged();
}

QRectF ShapeItem::boundingRect() const
{
    qreal selectionWidth = 2.0;
    qreal handleSize = kHandleSize;
    if (auto *canvasScene = static_cast<CanvasScene *>(scene())) {
        selectionWidth = canvasScene->selectionLineWidth();
        handleSize = canvasScene->handleSize();
    }
    const qreal margin = qMax(handleSize, qMax(m_borderWidth, 1.0) + selectionWidth + 8.0);
    QRectF r = m_rect.adjusted(-margin, -margin, margin, margin);
    QRectF originMargin(m_origin.x() - kOriginRadius - margin, m_origin.y() - kOriginRadius - margin,
                         2 * (kOriginRadius + margin), 2 * (kOriginRadius + margin));
    return r.united(originMargin);
}

QPainterPath ShapeItem::shape() const
{
    return hitTestPath();
}

QPainterPath ShapeItem::localShapePath() const
{
    QPainterPath path;
    const qreal radius = cornerRadius();
    if (radius > 0.0)
        path.addRoundedRect(m_rect, radius, radius);
    else
        path.addRect(m_rect);
    return path;
}

std::vector<HandleId> ShapeItem::activeHandles() const
{
    return { HandleId::TopLeft, HandleId::Top, HandleId::TopRight, HandleId::Right,
             HandleId::BottomRight, HandleId::Bottom, HandleId::BottomLeft, HandleId::Left };
}

void ShapeItem::setBodyColor(const QColor &color)
{
    m_bodyColor = color;
    update();
    emit propertyChanged();
}

void ShapeItem::setBorderColor(const QColor &color)
{
    m_borderColor = color;
    update();
    emit propertyChanged();
}

qreal ShapeItem::maxCornerRadius() const
{
    return qMin(qAbs(m_rect.width()), qAbs(m_rect.height())) / 2.0;
}

qreal ShapeItem::cornerRadius() const
{
    // Clamped on the way out too, so resizing a shape smaller cannot leave a
    // radius bigger than the shape that holds it.
    return qBound(0.0, m_cornerRadius, maxCornerRadius());
}

void ShapeItem::setCornerRadius(qreal radius)
{
    m_cornerRadius = qMax(0.0, radius);
    prepareGeometryChange();
    update();
    emit propertyChanged();
}

void ShapeItem::setSmoothChain(bool smooth)
{
    m_smoothChain = smooth;
    emit propertyChanged();
}

void ShapeItem::setBorderWidth(qreal width)
{
    m_borderWidth = width;
    update();
    emit propertyChanged();
}

void ShapeItem::setFilled(bool filled)
{
    if (m_filled == filled)
        return;
    m_filled = filled;
    update();
    emit propertyChanged();
}

void ShapeItem::setCapStyle(Qt::PenCapStyle style)
{
    if (m_capStyle == style)
        return;
    m_capStyle = style;
    update();
    emit propertyChanged();
}

void ShapeItem::setJoinStyle(Qt::PenJoinStyle style)
{
    if (m_joinStyle == style)
        return;
    m_joinStyle = style;
    update();
    emit propertyChanged();
}

void ShapeItem::paintShape(QPainter *painter, const QRectF &rect) const
{
    const qreal radius = cornerRadius();
    if (radius > 0.0)
        painter->drawRoundedRect(rect, radius, radius);
    else
        painter->drawRect(rect);
}

void ShapeItem::paintPhysicsView(QPainter *painter, const CanvasScene *canvas) const
{
    const bool assigned = m_body != nullptr;
    const bool picked = canvas->isSelectedForPhysics(const_cast<ShapeItem *>(this));

    QBrush brush;
    QColor outline;
    if (assigned) {
        outline = canvas->bodyColor(m_body->props().type);

        if (canvas->simulationRunning() && canvas->debugView()) {
            const int shift = 100 + canvas->sleepShiftPercent();
            outline = m_body->isAsleep() ? outline.darker(shift) : outline.lighter(shift);
        }

        QColor fill = outline;
        fill.setAlpha(canvas->physicsFillAlpha());
        brush = QBrush(fill);
    } else {
        outline = canvas->unassignedShapeColor();
        brush = QBrush(outline, Qt::DiagCrossPattern);
    }

    QPen pen(outline);
    pen.setWidthF(canvas->physicsBorderWidth());
    pen.setCosmetic(true);
    painter->setPen(pen);
    painter->setBrush(brush);
    paintShape(painter, m_rect);

    // A sensor is an ordinary shape wearing a flag, so it keeps the colour of
    // the body it belongs to and gains hatching on top -- which says you can
    // pass through it without hiding what kind of body it is part of.
    if (assigned && m_part.isSensor) {
        painter->save();
        painter->setPen(Qt::NoPen);
        painter->setBrush(QBrush(canvas->sensorColor(), Qt::BDiagPattern));
        paintShape(painter, m_rect);
        painter->restore();
    }

    // Nothing is selectable during a run, so nothing shows as selected.
    if (picked && !canvas->simulationRunning()) {
        QPen selectionPen(canvas->physicsSelectionColor());
        selectionPen.setWidthF(canvas->physicsSelectionLineWidth());
        selectionPen.setStyle(canvas->physicsSelectionLineStyle());
        selectionPen.setCosmetic(true);
        painter->setPen(selectionPen);
        painter->setBrush(Qt::NoBrush);
        painter->drawPath(selectionIndicatorPath());
    }
}

void ShapeItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *, QWidget *)
{
    painter->setRenderHint(QPainter::Antialiasing, true);

    const auto *canvas = qobject_cast<const CanvasScene *>(scene());
    if (canvas && canvas->editorMode() != EditorMode::Edit) {
        paintPhysicsView(painter, canvas);
        return;
    }

    QPen pen(m_borderColor);
    pen.setWidthF(m_borderWidth);
    pen.setCosmetic(true);
    pen.setCapStyle(m_capStyle);
    pen.setJoinStyle(m_joinStyle);
    painter->setPen(pen);
    // Both branches must already be QBrush: a QColor/Qt::NoBrush ternary
    // resolves via QColor(QRgb) and turns NoBrush into opaque black.
    painter->setBrush(m_filled ? QBrush(m_bodyColor) : QBrush(Qt::NoBrush));
    paintShape(painter, m_rect);

    const bool showSelection = !(canvas && canvas->simulationRunning());

    if ((m_mode != ShapeMode::Idle || m_coSelected) && showSelection) {
        QColor indicatorColor(230, 140, 40);
        qreal indicatorWidth = 2.0;
        Qt::PenStyle indicatorStyle = Qt::DotLine;
        if (auto *canvasScene = static_cast<CanvasScene *>(scene())) {
            indicatorColor = canvasScene->selectionColor();
            indicatorWidth = canvasScene->selectionLineWidth();
            indicatorStyle = canvasScene->selectionLineStyle();
        }

        QPen dotPen(indicatorColor);
        dotPen.setWidthF(indicatorWidth);
        dotPen.setStyle(indicatorStyle);
        dotPen.setCosmetic(true);
        painter->setPen(dotPen);
        painter->setBrush(Qt::NoBrush);
        painter->drawPath(selectionIndicatorPath());
    }

    // While several shapes are picked the group has one pivot of its own, and
    // a second marker on this shape would only compete with it.
    const bool inGroup = canvas && !canvas->editSelection().isEmpty();

    if (m_mode == ShapeMode::Rotating && showSelection && !inGroup) {
        painter->setPen(QPen(QColor(64, 64, 64), 1));
        painter->setBrush(QColor(76, 175, 80));
        painter->drawEllipse(m_origin, kOriginRadius, kOriginRadius);

        const qreal reach = kOriginRadius + 4.0;
        painter->setPen(QPen(QColor(220, 50, 50), 1.5)); // X axis, red
        painter->drawLine(m_origin + QPointF(-reach, 0), m_origin + QPointF(reach, 0));
        painter->setPen(QPen(QColor(40, 160, 60), 1.5)); // Y axis, green
        painter->drawLine(m_origin + QPointF(0, -reach), m_origin + QPointF(0, reach));
    }

    if (m_mode == ShapeMode::Selected && showSelection) {
        if (!inGroup) {
            painter->setPen(QPen(QColor(64, 64, 64), 1));
            painter->setBrush(QColor(220, 50, 50));
            painter->drawEllipse(m_origin, kOriginRadius * 0.4, kOriginRadius * 0.4);
        }

        QColor handleColor(222, 184, 135);
        qreal handleBorderWidth = 1.0;
        QColor handleBorderColor(64, 64, 64);
        HandleShape handleShape = HandleShape::Square;
        if (auto *canvasScene = static_cast<CanvasScene *>(scene())) {
            handleColor = canvasScene->handleColor();
            handleBorderWidth = canvasScene->handleBorderWidth();
            handleBorderColor = canvasScene->handleBorderColor();
            handleShape = canvasScene->handleShape();
        }

        painter->setPen(QPen(handleBorderColor, handleBorderWidth));
        painter->setBrush(handleColor);
        const bool showHandles = canvas && canvas->geometryEditingAllowed()
                                 && !canvas->movingShape() && !inGroup;
        for (HandleId id : showHandles ? activeHandles() : std::vector<HandleId>{}) {
            const QRectF r = handleRect(id);
            if (handleShape == HandleShape::Circle)
                painter->drawEllipse(r);
            else
                painter->drawRect(r);
        }
    }

    if (m_mode == ShapeMode::Editing && showSelection)
        paintEditNodes(painter);
}

QRectF ShapeItem::handleRect(HandleId id) const
{
    QPointF p;
    switch (id) {
    case HandleId::TopLeft:     p = m_rect.topLeft(); break;
    case HandleId::Top:         p = QPointF(m_rect.center().x(), m_rect.top()); break;
    case HandleId::TopRight:    p = m_rect.topRight(); break;
    case HandleId::Right:       p = QPointF(m_rect.right(), m_rect.center().y()); break;
    case HandleId::BottomRight: p = m_rect.bottomRight(); break;
    case HandleId::Bottom:      p = QPointF(m_rect.center().x(), m_rect.bottom()); break;
    case HandleId::BottomLeft:  p = m_rect.bottomLeft(); break;
    case HandleId::Left:        p = QPointF(m_rect.left(), m_rect.center().y()); break;
    case HandleId::None:        return QRectF();
    }
    qreal handleSize = kHandleSize;
    if (auto *canvasScene = static_cast<CanvasScene *>(scene()))
        handleSize = canvasScene->handleSize();
    return QRectF(p.x() - handleSize / 2, p.y() - handleSize / 2, handleSize, handleSize);
}

HandleId ShapeItem::handleAt(const QPointF &localPos) const
{
    for (HandleId id : activeHandles()) {
        if (handleRect(id).contains(localPos))
            return id;
    }
    return HandleId::None;
}

bool ShapeItem::originHandleContains(const QPointF &localPos) const
{
    const qreal grabMargin = 4.0;
    QPointF d = localPos - m_origin;
    return (d.x() * d.x() + d.y() * d.y()) <= (kOriginRadius + grabMargin) * (kOriginRadius + grabMargin);
}

bool ShapeItem::shapeContains(const QPointF &localPos) const
{
    return hitTestPath().contains(localPos);
}

QPainterPath ShapeItem::hitTestPath() const
{
    const QPainterPath path = localShapePath();
    QPainterPathStroker stroker;
    stroker.setWidth(qMax(m_borderWidth, 1.0) + 6.0);
    return path.united(stroker.createStroke(path));
}

QPainterPath ShapeItem::selectionIndicatorPath() const
{
    qreal indicatorWidth = 2.0;
    if (auto *canvasScene = static_cast<CanvasScene *>(scene()))
        indicatorWidth = canvasScene->selectionLineWidth();

    const QPainterPath path = localShapePath();
    const qreal margin = m_borderWidth / 2.0 + indicatorWidth / 2.0 + 1.0;
    QPainterPathStroker stroker;
    stroker.setWidth(margin * 2.0);
    return stroker.createStroke(path).united(path);
}

QRectF ShapeItem::adjustedRectForHandle(HandleId id, const QPointF &localPos) const
{
    QRectF r = m_rect;

    switch (id) {
    case HandleId::Left:        r.setLeft(localPos.x()); break;
    case HandleId::Right:       r.setRight(localPos.x()); break;
    case HandleId::Top:         r.setTop(localPos.y()); break;
    case HandleId::Bottom:      r.setBottom(localPos.y()); break;
    case HandleId::TopLeft:     r.setTopLeft(localPos); break;
    case HandleId::TopRight:    r.setTopRight(localPos); break;
    case HandleId::BottomLeft:  r.setBottomLeft(localPos); break;
    case HandleId::BottomRight: r.setBottomRight(localPos); break;
    case HandleId::None:        return r;
    }

    if (r.width() < kMinSize) {
        const bool draggingLeftEdge = (id == HandleId::Left || id == HandleId::TopLeft || id == HandleId::BottomLeft);
        if (draggingLeftEdge)
            r.setLeft(r.right() - kMinSize);
        else
            r.setRight(r.left() + kMinSize);
    }
    if (r.height() < kMinSize) {
        const bool draggingTopEdge = (id == HandleId::Top || id == HandleId::TopLeft || id == HandleId::TopRight);
        if (draggingTopEdge)
            r.setTop(r.bottom() - kMinSize);
        else
            r.setBottom(r.top() + kMinSize);
    }

    return r;
}

void ShapeItem::resizeByHandle(HandleId id, const QPointF &localPos)
{
    if (id == HandleId::None)
        return;
    applyRect(adjustedRectForHandle(id, localPos));
}

bool ShapeItem::supportsNodeEditing() const
{
    return false;
}

int ShapeItem::nodeAt(const QPointF &) const
{
    return -1;
}

QPointF ShapeItem::nodePosition(int) const
{
    return QPointF();
}

void ShapeItem::moveNode(int, const QPointF &)
{
}

void ShapeItem::deleteNode(int)
{
}

void ShapeItem::insertNodeBetween(int, int)
{
}

bool ShapeItem::isClosed() const
{
    return false;
}

void ShapeItem::closeShape()
{
}

int ShapeItem::nodeCount() const
{
    return 0;
}

void ShapeItem::setSelectedNodes(const QSet<int> &)
{
}

void ShapeItem::paintEditNodes(QPainter *) const
{
}
