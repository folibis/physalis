#include "PolygonPropertyPane.h"

#include "../CanvasScene.h"
#include "../PolygonItem.h"
#include "PhysicsTypes.h"

std::vector<PropertyRow> PolygonPropertyPane::extraRows(ShapeItem *item) const
{
    std::vector<PropertyRow> result;

    // What this outline is built as. Four rules used to decide it between them
    // -- closed or not, convex or not, the point cap, and a "smooth" tick --
    // and nothing said which had won. It is a choice now, and the choices that
    // cannot be built are not offered.
    const auto *polygon = dynamic_cast<const PolygonItem *>(item);
    const int points = polygon ? polygon->points().size() : 0;
    const int segments = polygon ? (polygon->isClosed() ? points : points - 1) : 0;

    int cap = 8;
    if (auto *canvas = qobject_cast<CanvasScene *>(item->scene()))
        cap = canvas->maxPolygonVertices();

    QVector<QPointF> outline;
    outline.reserve(points);
    if (polygon) {
        for (const QPointF &p : polygon->points())
            outline.append(p);
    }
    const bool canBeSolid =
        polygon && polygon->isClosed() && points <= cap && physics::isConvex(outline);
    const bool canBeChain = points >= 4;

    QStringList kinds;
    if (canBeSolid)
        kinds << QObject::tr("Polygon");
    if (canBeChain)
        kinds << QObject::tr("Chain");
    kinds << (points == 2 ? QObject::tr("Segment") : QObject::tr("Segments"));

    const auto currentKind = [item, canBeSolid, canBeChain]() -> QString {
        if (canBeSolid && !item->preferOutline())
            return QObject::tr("Polygon");
        if (canBeChain && item->smoothChain())
            return QObject::tr("Chain");
        return QObject::tr("Segments");
    };

    // Only a filled shape can be filled. A chain and a run of segments are
    // outlines -- there is no inside to colour, and offering the choice says
    // there is.
    if (canBeSolid && !item->preferOutline()) {
        result.push_back(PropertyRow{
            QObject::tr("Filled"), PropertyFieldType::Boolean,
            [item] { return item->filled(); },
            [item](const QVariant &v) { item->setFilled(v.toBool()); },
            -100000.0, 100000.0, {}
        });
    }

    PropertyRow kind;
    kind.label = QObject::tr("Built as");
    kind.type = PropertyFieldType::Choice;
    kind.choices = kinds;
    kind.key = QStringLiteral("shape.builtAs");
    kind.getter = [kinds, currentKind] {
        const int at = kinds.indexOf(currentKind());
        // The kind it resolves to is always one of the offered ones; the index
        // is what the box shows.
        return at < 0 ? kinds.size() - 1 : at;
    };
    kind.setter = [item, kinds](const QVariant &v) {
        const int at = v.toInt();
        const QString picked = at >= 0 && at < kinds.size() ? kinds.at(at) : QString();
        if (picked == QObject::tr("Polygon")) {
            item->setPreferOutline(false);
            item->setSmoothChain(false);
        } else if (picked == QObject::tr("Chain")) {
            item->setPreferOutline(true);
            item->setSmoothChain(true);
        } else {
            item->setPreferOutline(true);
            item->setSmoothChain(false);
        }
    };
    kind.tooltip = QObject::tr(
        "Polygon: one filled shape. It is the only kind with area, so the only kind that can "
        "belong to a moving body -- and it has to be closed, convex and within the point limit.\n\n"
        "Chain: the edges joined into one surface, so nothing catches where two of them meet. "
        "It stops things from one side only, and needs at least four points.\n\n"
        "Segments: each edge on its own. They stop things from both sides, and something "
        "sliding along can catch at a join.");
    result.push_back(std::move(kind));

    PropertyRow count;
    count.label = QObject::tr("Segments");
    count.type = PropertyFieldType::String;
    count.readOnly = true;
    count.key = QStringLiteral("shape.segmentCount");
    count.getter = [item, canBeSolid, segments]() -> QVariant {
        if (canBeSolid && !item->preferOutline())
            return QObject::tr("none — it is one filled shape");
        return QString::number(segments);
    };
    count.setter = [](const QVariant &) {};
    count.tooltip = QObject::tr("How many straight edges the engine builds this outline from."
                                " A filled polygon is one shape and has none.");
    result.push_back(std::move(count));

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
