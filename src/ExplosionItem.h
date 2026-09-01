#pragma once

#include <QGraphicsObject>
#include <QString>
#include <QVariantMap>

// A named point in the scene, with no geometry and no body. Rules aim
// position-based actions at one -- an explosion needs somewhere to happen, and
// nothing else about a body is relevant to that.
class ExplosionItem : public QGraphicsObject
{
    Q_OBJECT

public:
    explicit ExplosionItem(QGraphicsItem *parent = nullptr);

    enum { Type = UserType + 20 };
    int type() const override { return Type; }

    QString name() const { return m_name; }
    void setName(const QString &name);

    static QString typeName() { return QStringLiteral("explosion"); }

    // Drawn radius, and the reach of a click.
    static qreal radius() { return 12.0; }

    QRectF boundingRect() const override;
    QPainterPath shape() const override;
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option,
               QWidget *widget) override;

    // Impulse, radius and falloff. The engine declares which keys exist and
    // what they mean; this only stores them.
    const QVariantMap &params() const { return m_params; }
    QVariantMap &params() { return m_params; }
    void setParam(const QString &key, const QVariant &value);

    bool isSelected() const { return m_selected; }
    void setSelectedForPhysics(bool selected);

signals:
    void propertyChanged();

private:
    QString m_name;
    QVariantMap m_params;
    bool m_selected = false;
};
