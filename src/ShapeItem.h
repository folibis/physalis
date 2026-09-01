#pragma once

#include <QGraphicsObject>
#include <QColor>
#include <QSet>
#include <QString>
#include <vector>

#include "PhysicsTypes.h"

class PropertyPane;
class PhysicsBody;
class CanvasScene;

enum class ShapeMode {
    Idle,
    Selected,
    Editing,  // node-level editing (move/add/delete vertices); only meaningful
              // for shapes where supportsNodeEditing() is true
    Rotating
};

enum class HandleId {
    None,
    TopLeft,
    Top,
    TopRight,
    Right,
    BottomRight,
    Bottom,
    BottomLeft,
    Left
};

class ShapeItem : public QGraphicsObject
{
    Q_OBJECT

public:
    enum { Type = QGraphicsItem::UserType + 1 };
    int type() const override { return Type; }

    ShapeItem();
    ~ShapeItem() override;

    ShapeMode mode() const { return m_mode; }
    void setMode(ShapeMode mode);

    QRectF rect() const { return m_rect; }
    void setRect(const QRectF &rect);

    // Picked alongside the active shape. It carries no handles of its own --
    // only the selection outline -- but it moves and turns with the group.
    bool isCoSelected() const { return m_coSelected; }
    void setCoSelected(bool coSelected);

    QPointF origin() const { return m_origin; }
    void setOrigin(const QPointF &origin);

    QColor bodyColor() const { return m_bodyColor; }
    void setBodyColor(const QColor &color);

    QColor borderColor() const { return m_borderColor; }
    void setBorderColor(const QColor &color);

    qreal borderWidth() const { return m_borderWidth; }
    void setBorderWidth(qreal width);

    // Rounds the corners. Capped at half the shorter side, where the shape
    // becomes a capsule and any more would be meaningless.
    qreal cornerRadius() const;
    qreal maxCornerRadius() const;
    void setCornerRadius(qreal radius);

    // Drawn lines only: one continuous surface rather than separate edges.
    bool smoothChain() const { return m_smoothChain; }
    void setSmoothChain(bool smooth);

    bool filled() const { return m_filled; }
    void setFilled(bool filled);

    Qt::PenCapStyle capStyle() const { return m_capStyle; }
    void setCapStyle(Qt::PenCapStyle style);

    Qt::PenJoinStyle joinStyle() const { return m_joinStyle; }
    void setJoinStyle(Qt::PenJoinStyle style);

    QRectF boundingRect() const override;
    QPainterPath shape() const override;
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override;

    // Hit-testing helpers used by CanvasScene, all in item-local coordinates.
    HandleId handleAt(const QPointF &localPos) const;
    QRectF handleRect(HandleId id) const;
    bool originHandleContains(const QPointF &localPos) const;
    bool shapeContains(const QPointF &localPos) const;

    virtual void resizeByHandle(HandleId id, const QPointF &localPos);

    // Take a new local rect, bringing whatever geometry the shape keeps of its
    // own along with it. Resizing by a handle and scaling as part of a group
    // both end here.
    virtual void applyRect(const QRectF &rect) { setRect(rect); }

    virtual bool supportsNodeEditing() const;
    // Index of the node at localPos (within its hit radius), or -1.
    virtual int nodeAt(const QPointF &localPos) const;
    virtual QPointF nodePosition(int index) const;
    virtual void moveNode(int index, const QPointF &localPos);
    virtual void deleteNode(int index);
    virtual void insertNodeBetween(int i, int j);
    virtual bool isClosed() const;
    virtual void closeShape();
    virtual int nodeCount() const;

    virtual void setSelectedNodes(const QSet<int> &indices);

    static constexpr Qt::PenCapStyle kDefaultCapStyle = Qt::RoundCap;
    static constexpr Qt::PenJoinStyle kDefaultJoinStyle = Qt::RoundJoin;
    static constexpr bool kDefaultFilled = true;

    virtual QRectF defaultRect() const { return QRectF(); }

    QString name() const { return m_name; }
    void setName(const QString &name);

    virtual QString typeName() const = 0;

    // --- Physics ---------------------------------------------------
    // A shape carries only what Box2D calls shape properties: its collision
    // outline, that outline's surface material and density, its collision
    // filter, and which events it reports. Everything about how it *moves* --
    // body type, velocity, damping, sleep -- lives on the PhysicsBody it
    // belongs to, because those are properties of the body as a whole.
    //
    // A freshly drawn shape belongs to no body and is inert until the user
    // groups it into one in Physics mode.

    PhysicsBody *body() const { return m_body; }
    void setBody(PhysicsBody *body);

    const physics::ShapePart &part() const { return m_part; }
    physics::ShapePart &part() { return m_part; }
    // Panes write into part() and props() directly and then call this, so the
    // repaint has to happen here -- there is no setter to do it for them.
    void notifyPropertyChanged() { update(); emit propertyChanged(); }

    virtual physics::Geometry physicsGeometry() const = 0;

    virtual PropertyPane *makePropertyPane() const = 0;

signals:
    void propertyChanged();

protected:
    virtual void paintShape(QPainter *painter, const QRectF &rect) const;

    void paintPhysicsView(QPainter *painter, const CanvasScene *canvas) const;

    virtual QPainterPath localShapePath() const;

    virtual std::vector<HandleId> activeHandles() const;

    virtual void paintEditNodes(QPainter *painter) const;

    QRectF adjustedRectForHandle(HandleId id, const QPointF &localPos) const;

private:
    QPainterPath hitTestPath() const;

    QPainterPath selectionIndicatorPath() const;

    QRectF m_rect;
    QPointF m_origin;
    bool m_coSelected = false;
    ShapeMode m_mode = ShapeMode::Idle;

    QString m_name;
    // Simulation properties; the geometry field inside is unused (derived).
    physics::ShapePart m_part;
    // Not owned; the body owns the relationship and clears this on destruction.
    PhysicsBody *m_body = nullptr;

    QColor m_bodyColor { 173, 216, 230, 128 };
    QColor m_borderColor { 100, 170, 220, 204 };
    qreal m_cornerRadius = 0.0;
    bool m_smoothChain = false;
    qreal m_borderWidth = 2.0;
    bool m_filled = kDefaultFilled;
    Qt::PenCapStyle m_capStyle = kDefaultCapStyle;
    Qt::PenJoinStyle m_joinStyle = kDefaultJoinStyle;

    static constexpr qreal kHandleSize = 8.0;
    static constexpr qreal kOriginRadius = 5.0;
    static constexpr qreal kMinSize = 10.0;
};
