#include "ShapePropertyPaneBase.h"

#include <QHash>
#include "../CanvasScene.h"

namespace {

const Qt::PenCapStyle kCapStyles[] = { Qt::FlatCap, Qt::SquareCap, Qt::RoundCap };
const Qt::PenJoinStyle kJoinStyles[] = { Qt::MiterJoin, Qt::BevelJoin, Qt::RoundJoin };

template <typename Style, size_t N>
int indexOfStyle(const Style (&table)[N], Style value)
{
    for (size_t i = 0; i < N; ++i) {
        if (table[i] == value)
            return int(i);
    }
    return 0;
}

} // namespace


namespace {

// The names the log uses for a shape's own geometry. These are not engine
// properties -- the solver moves the shape, and the shape is where the result
// lands -- so they are read back off the item itself.
void tagShapeKeys(std::vector<PropertyRow> &rows)
{
    static const QHash<QString, QString> keys = {
        { QObject::tr("Left"),     QStringLiteral("shape.x") },
        { QObject::tr("Top"),      QStringLiteral("shape.y") },
        { QObject::tr("Width"),    QStringLiteral("shape.width") },
        { QObject::tr("Height"),   QStringLiteral("shape.height") },
        { QObject::tr("Rotation"), QStringLiteral("shape.rotation") },
        { QObject::tr("Origin X"), QStringLiteral("shape.originX") },
        { QObject::tr("Origin Y"), QStringLiteral("shape.originY") },
        { QObject::tr("Border Width"), QStringLiteral("shape.borderWidth") },
    };
    for (PropertyRow &row : rows) {
        if (row.key.isEmpty())
            row.key = keys.value(row.label);
    }
}

} // namespace

std::vector<PropertyRow> ShapePropertyPaneBase::rows(EditorMode mode) const
{
    std::vector<PropertyRow> result;
    if (!m_item)
        return result;

    ShapeItem *item = m_item;

    if (mode == EditorMode::Edit)
        result = geometryRows(item);

    tagShapeKeys(result);
    return result;
}

std::vector<PropertyRow> ShapePropertyPaneBase::sizeRows(ShapeItem *item,
                                                        const QString &section) const
{
    std::vector<PropertyRow> result;

    result.push_back({QObject::tr("Width"), PropertyFieldType::Numeric,
        [item] { return item->rect().width(); },
        [item](const QVariant &v) {
            QRectF r = item->rect();
            r.setWidth(qMax(10.0, v.toDouble()));
            item->setRect(r);
        },
        10.0, 100000.0, {}, -1, 0.0, section});

    result.push_back({QObject::tr("Height"), PropertyFieldType::Numeric,
        [item] { return item->rect().height(); },
        [item](const QVariant &v) {
            QRectF r = item->rect();
            r.setHeight(qMax(10.0, v.toDouble()));
            item->setRect(r);
        },
        10.0, 100000.0, {}, -1, 0.0, section});

    return result;
}

QVector<QPair<QString, QVariant>>
ShapePropertyPaneBase::sizeDefaults(const QRectF &created) const
{
    return {{QObject::tr("Width"), created.width()},
            {QObject::tr("Height"), created.height()}};
}

std::vector<PropertyRow> ShapePropertyPaneBase::geometryRows(ShapeItem *item) const
{
    std::vector<PropertyRow> result;

    const QString geometry = QObject::tr("Geometry");
    const QString appearance = QObject::tr("Appearance");

    result.push_back({QObject::tr("Name"), PropertyFieldType::String,
        [item] { return item->name(); },
        [item](const QVariant &v) { item->setName(v.toString()); },
        -100000.0, 100000.0, {}, -1, 0.0, geometry});

    for (PropertyRow &row : sizeRows(item, geometry))
        result.push_back(std::move(row));

    result.push_back({QObject::tr("Left"), PropertyFieldType::Numeric,
        [item] { return item->pos().x() + item->rect().left(); },
        [item](const QVariant &v) {
            const double delta = v.toDouble() - (item->pos().x() + item->rect().left());
            item->setPos(item->pos() + QPointF(delta, 0));
        },
        -100000.0, 100000.0, {}, -1, 0.0, geometry});

    result.push_back({QObject::tr("Top"), PropertyFieldType::Numeric,
        [item] { return item->pos().y() + item->rect().top(); },
        [item](const QVariant &v) {
            const double delta = v.toDouble() - (item->pos().y() + item->rect().top());
            item->setPos(item->pos() + QPointF(0, delta));
        },
        -100000.0, 100000.0, {}, -1, 0.0, geometry});

    if (item->mode() == ShapeMode::Rotating) {
        result.push_back({QObject::tr("Origin X"), PropertyFieldType::Numeric,
            [item] { return item->origin().x(); },
            [item](const QVariant &v) { item->setOrigin(QPointF(v.toDouble(), item->origin().y())); },
            -100000.0, 100000.0, {}, -1, 0.0, geometry});

        result.push_back({QObject::tr("Origin Y"), PropertyFieldType::Numeric,
            [item] { return item->origin().y(); },
            [item](const QVariant &v) { item->setOrigin(QPointF(item->origin().x(), v.toDouble())); },
            -100000.0, 100000.0, {}, -1, 0.0, geometry});
    }

    result.push_back({QObject::tr("Rotation"), PropertyFieldType::Numeric,
        [item] { return item->rotation(); },
        [item](const QVariant &v) { item->setRotation(v.toDouble()); },
        -3600.0, 3600.0, {}, -1, 0.0, geometry});

    result.push_back({QObject::tr("Body Color"), PropertyFieldType::Color,
        [item] { return item->bodyColor(); },
        [item](const QVariant &v) { item->setBodyColor(v.value<QColor>()); },
        -100000.0, 100000.0, {}, -1, 0.0, appearance});

    result.push_back({QObject::tr("Transparency"), PropertyFieldType::Slider,
        [item] { return 100 - qRound(item->bodyColor().alpha() / 255.0 * 100.0); },
        [item](const QVariant &v) {
            QColor c = item->bodyColor();
            const int transparency = v.toInt();
            c.setAlpha(qBound(0, qRound((100 - transparency) / 100.0 * 255.0), 255));
            item->setBodyColor(c);
        },
        0, 100, {}, -1, 0.0, appearance});

    for (PropertyRow &row : extraRows(item))
        result.push_back(std::move(row));

    result.push_back({QObject::tr("Border Width"), PropertyFieldType::Numeric,
        [item] { return item->borderWidth(); },
        [item](const QVariant &v) { item->setBorderWidth(qMax(0.0, v.toDouble())); },
        0.0, 100.0, {}, -1, 0.0, appearance});

    result.push_back({QObject::tr("Border Color"), PropertyFieldType::Color,
        [item] { return item->borderColor(); },
        [item](const QVariant &v) { item->setBorderColor(v.value<QColor>()); },
        -100000.0, 100000.0, {}, -1, 0.0, appearance});

    result.push_back({QObject::tr("Line Cap"), PropertyFieldType::Choice,
        [item] {
            int index = 0;
            for (int i = 0; i < 3; ++i) {
                if (kCapStyles[i] == item->capStyle())
                    index = i;
            }
            return index;
        },
        [item](const QVariant &v) { item->setCapStyle(kCapStyles[qBound(0, v.toInt(), 2)]); },
        -100000.0, 100000.0,
        {QObject::tr("Flat"), QObject::tr("Square"), QObject::tr("Round")}, -1, 0.0, appearance});

    result.push_back({QObject::tr("Line Join"), PropertyFieldType::Choice,
        [item] {
            int index = 0;
            for (int i = 0; i < 3; ++i) {
                if (kJoinStyles[i] == item->joinStyle())
                    index = i;
            }
            return index;
        },
        [item](const QVariant &v) { item->setJoinStyle(kJoinStyles[qBound(0, v.toInt(), 2)]); },
        -100000.0, 100000.0,
        {QObject::tr("Miter"), QObject::tr("Bevel"), QObject::tr("Round")}, -1, 0.0, appearance});

    return result;
}

std::vector<PropertyRow> ShapePropertyPaneBase::defaultRows(EditorMode mode) const
{
    std::vector<PropertyRow> result;
    if (!m_item || mode != EditorMode::Edit)
        return result;

    const QString section = QStringLiteral("Geometry");
    const auto constant = [&result, &section](const QString &label, const QVariant &value) {
        result.push_back({label, PropertyFieldType::Numeric,
            [value] { return value; }, [](const QVariant &) {},
            -100000.0, 100000.0, {}, -1, 0.0, section});
    };

    const QRectF created = m_item->defaultRect();
    if (created.isValid()) {
        for (const auto &pair : sizeDefaults(created))
            constant(pair.first, pair.second);
    }

    constant(QObject::tr("Rotation"), 0.0);

    if (const auto *scene = qobject_cast<const CanvasScene *>(m_item->scene())) {
        constant(QObject::tr("Border Width"), scene->defaultBorderWidth());
        constant(QObject::tr("Body Color"), scene->defaultBodyColor());
        constant(QObject::tr("Border Color"), scene->defaultBorderColor());
        constant(QObject::tr("Transparency"),
                 100 - qRound(scene->defaultBodyColor().alpha() / 255.0 * 100.0));
    }

    constant(QObject::tr("Line Cap"), indexOfStyle(kCapStyles, ShapeItem::kDefaultCapStyle));
    constant(QObject::tr("Line Join"), indexOfStyle(kJoinStyles, ShapeItem::kDefaultJoinStyle));

    for (PropertyRow &row : extraDefaultRows(m_item))
        result.push_back(std::move(row));

    return result;
}

void ShapePropertyPaneBase::attach(QObject *target)
{
    if (m_item)
        disconnect(m_item, nullptr, this, nullptr);

    m_item = qobject_cast<ShapeItem *>(target);
    m_lastOriginRowsVisible = m_item && m_item->mode() == ShapeMode::Rotating;

    if (m_item) {
        connect(m_item, &ShapeItem::propertyChanged, this, &ShapePropertyPaneBase::onItemPropertyChanged);
        connect(m_item, &QGraphicsObject::xChanged, this, &PropertyPane::valueChanged);
        connect(m_item, &QGraphicsObject::yChanged, this, &PropertyPane::valueChanged);
        connect(m_item, &QGraphicsObject::rotationChanged, this, &PropertyPane::valueChanged);
    }

    emit rowsChanged();
}

void ShapePropertyPaneBase::onItemPropertyChanged()
{
    const bool originRowsVisible = m_item && m_item->mode() == ShapeMode::Rotating;
    if (originRowsVisible != m_lastOriginRowsVisible) {
        m_lastOriginRowsVisible = originRowsVisible;
        emit rowsChanged();
    } else {
        emit valueChanged();
    }
}
