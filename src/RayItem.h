#pragma once

#include <QGraphicsObject>
#include <QPointF>
#include <QString>

// A rangefinder: a line that reports how far it is to the first thing in the
// way. It never collides and never reaches the physics world as an object --
// the engine is only asked to measure along it.
class RayItem : public QGraphicsObject
{
    Q_OBJECT

public:
    explicit RayItem(QGraphicsItem *parent = nullptr);

    enum { Type = UserType + 22 };
    int type() const override { return Type; }

    QString name() const { return m_name; }
    void setName(const QString &name);

    static QString typeName() { return QStringLiteral("ray"); }

    // Where it points, in degrees clockwise from pointing right.
    qreal angleDegrees() const { return m_angleDegrees; }
    void setAngleDegrees(qreal degrees);

    // How far it can see, in scene units.
    qreal length() const { return m_length; }
    void setLength(qreal length);

    // Which groups it can see, the same bits a shape filters with.
    quint64 maskBits() const { return m_maskBits; }
    void setMaskBits(quint64 bits);

    // Where the far end lands when nothing is in the way, in scene units
    // relative to the ray's own origin.
    QPointF reach() const;

    // Filled in by the run. Cleared when it stops, so a stale reading is never
    // left on screen.
    void setReading(bool hit, const QPointF &point, qreal distance,
                    const QString &shapeName);
    void clearReading();
    bool hasHit() const { return m_hit; }
    // What it is looking at, empty when it sees nothing.
    QString hitName() const { return m_hit ? m_hitName : QString(); }
    QPointF hitPoint() const { return m_hitPoint; }
    qreal distance() const { return m_hit ? m_distance : m_length; }

    QRectF boundingRect() const override;
    QPainterPath shape() const override;
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option,
               QWidget *widget) override;

    bool isSelected() const { return m_selected; }
    void setSelectedForPhysics(bool selected);

signals:
    void propertyChanged();

private:
    QString m_name;
    qreal m_angleDegrees = 0.0;
    qreal m_length = 300.0;
    quint64 m_maskBits = ~quint64(0);

    bool m_hit = false;
    QPointF m_hitPoint;
    QString m_hitName;
    qreal m_distance = 0.0;

    bool m_selected = false;
};
