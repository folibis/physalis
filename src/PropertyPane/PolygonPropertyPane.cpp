#include "PolygonPropertyPane.h"

std::vector<PropertyRow> PolygonPropertyPane::extraRows(ShapeItem *item) const
{
    std::vector<PropertyRow> result;

    result.push_back(PropertyRow{
        QObject::tr("Filled"), PropertyFieldType::Boolean,
        [item] { return item->filled(); },
        [item](const QVariant &v) { item->setFilled(v.toBool()); },
        -100000.0, 100000.0, {}
    });

    PropertyRow smooth;
    smooth.label = QObject::tr("Smooth Surface");
    smooth.type = PropertyFieldType::Boolean;
    smooth.getter = [item] { return item->smoothChain(); };
    smooth.setter = [item](const QVariant &v) { item->setSmoothChain(v.toBool()); };
    smooth.defaultValue = false;
    smooth.key = QStringLiteral("shape.smoothChain");
    smooth.tooltip = QObject::tr(
        "Joins the edges into one continuous surface, so things sliding along "
        "do not catch where two of them meet. The surface then only collides "
        "from one side, and it needs at least four points.");
    result.push_back(std::move(smooth));

    return result;
}

std::vector<PropertyRow> PolygonPropertyPane::extraDefaultRows(ShapeItem *item) const
{
    Q_UNUSED(item);
    return { PropertyRow{
        QObject::tr("Filled"), PropertyFieldType::Boolean,
        [] { return ShapeItem::kDefaultFilled; }, [](const QVariant &) {},
        -100000.0, 100000.0, {}
    } };
}
