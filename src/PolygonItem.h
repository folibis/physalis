#pragma once

#include "ShapeItem.h"
#include <QPolygonF>
#include <QSet>

class PolygonItem : public ShapeItem
{
public:
    PolygonItem(const QPolygonF &localPoints, bool closed);

    physics::Geometry physicsGeometry() const override;

    PropertyPane *makePropertyPane() const override;

    void resizeByHandle(HandleId id, const QPointF &localPos) override;
    void applyRect(const QRectF &rect) override;

    bool supportsNodeEditing() const override { return true; }
    int nodeAt(const QPointF &localPos) const override;
    QPointF nodePosition(int index) const override;
    void moveNode(int index, const QPointF &localPos) override;
    void deleteNode(int index) override;
    void insertNodeBetween(int i, int j) override;
    bool isClosed() const override { return m_closed; }
    // An open polygon is a polyline, and worth naming as one.
    QString typeName() const override
    {
        return m_closed ? QStringLiteral("polygon") : QStringLiteral("polyline");
    }
    const QPolygonF &points() const { return m_points; }
    void closeShape() override;
    int nodeCount() const override { return m_points.size(); }
    void setSelectedNodes(const QSet<int> &indices) override;

protected:
    void paintShape(QPainter *painter, const QRectF &rect) const override;
    QPainterPath localShapePath() const override;
    void paintEditNodes(QPainter *painter) const override;

private:
    QPolygonF m_points;
    bool m_closed;
    QSet<int> m_selectedNodeIndices;

    static constexpr qreal kNodeRadius = 5.0;
    static constexpr int kMinPoints = 2;
};
