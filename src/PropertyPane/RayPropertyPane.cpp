#include "RayPropertyPane.h"

#include "CanvasScene.h"
#include "RayItem.h"

void RayPropertyPane::attach(QObject *target)
{
    m_scene = qobject_cast<CanvasScene *>(target);
}

std::vector<PropertyRow> RayPropertyPane::rows(EditorMode mode) const
{
    Q_UNUSED(mode);

    std::vector<PropertyRow> result;
    RayItem *ray = m_scene ? m_scene->selectedRay() : nullptr;
    if (!ray)
        return result;

    CanvasScene *scene = m_scene;
    const QString section = QObject::tr("Ray");

    PropertyRow name;
    name.label = QObject::tr("Name");
    name.type = PropertyFieldType::String;
    name.getter = [ray] { return ray->name(); };
    name.setter = [ray, scene](const QVariant &v) {
        ray->setName(scene->uniqueName(v.toString(), ray));
    };
    name.section = section;
    result.push_back(std::move(name));

    const auto coordinate = [&](const QString &label, bool horizontal) {
        PropertyRow row;
        row.label = label;
        row.type = PropertyFieldType::Numeric;
        row.getter = [ray, horizontal] {
            return horizontal ? ray->pos().x() : ray->pos().y();
        };
        row.setter = [ray, horizontal](const QVariant &v) {
            QPointF p = ray->pos();
            if (horizontal)
                p.setX(v.toDouble());
            else
                p.setY(v.toDouble());
            ray->setPos(p);
        };
        row.minValue = -1e6;
        row.maxValue = 1e6;
        row.decimals = 1;
        row.step = 1.0;
        row.section = section;
        result.push_back(std::move(row));
    };
    coordinate(QObject::tr("X"), true);
    coordinate(QObject::tr("Y"), false);

    PropertyRow angle;
    angle.label = QObject::tr("Angle (deg)");
    angle.type = PropertyFieldType::Numeric;
    angle.getter = [ray] { return ray->angleDegrees(); };
    angle.setter = [ray](const QVariant &v) { ray->setAngleDegrees(v.toDouble()); };
    angle.minValue = -3600.0;
    angle.maxValue = 3600.0;
    angle.decimals = 1;
    angle.step = 5.0;
    angle.section = section;
    angle.defaultValue = 0.0;
    angle.tooltip = QObject::tr("Which way it looks. 0 points right, 90 points down.");
    result.push_back(std::move(angle));

    PropertyRow length;
    length.label = QObject::tr("Length");
    length.type = PropertyFieldType::Numeric;
    length.getter = [ray] { return ray->length(); };
    length.setter = [ray](const QVariant &v) { ray->setLength(v.toDouble()); };
    length.minValue = 1.0;
    length.maxValue = 1e6;
    length.decimals = 0;
    length.step = 10.0;
    length.section = section;
    length.defaultValue = 300.0;
    length.tooltip = QObject::tr("How far it can see. Beyond this it reports nothing.");
    result.push_back(std::move(length));

    PropertyRow mask;
    mask.label = QObject::tr("Notices Groups");
    mask.type = PropertyFieldType::String;
    mask.getter = [ray] {
        return QStringLiteral("0x%1").arg(ray->maskBits(), 16, 16, QLatin1Char('0'));
    };
    mask.setter = [ray](const QVariant &v) {
        bool ok = false;
        const quint64 bits = v.toString().trimmed().toULongLong(&ok, 0);
        if (ok)
            ray->setMaskBits(bits);
    };
    mask.section = section;
    mask.tooltip = QObject::tr("Which groups it can see, by the same bits a shape filters with.");
    result.push_back(std::move(mask));

    // Filled by the run. Read-only, and loggable like any other row.
    const auto reading = [&](const QString &label, const QString &key,
                             PropertyFieldType type, const QString &tip) {
        PropertyRow row;
        row.label = label;
        row.key = key;
        row.type = type;
        row.section = section;
        row.readOnly = true;
        row.decimals = type == PropertyFieldType::Numeric ? 1 : -1;
        row.minValue = -1e9;
        row.maxValue = 1e9;
        row.getter = [] { return QVariant(); };   // the engine answers while running
        row.setter = [](const QVariant &) {};
        row.tooltip = tip;
        result.push_back(std::move(row));
    };
    reading(QObject::tr("Distance"), QStringLiteral("distance"), PropertyFieldType::Numeric,
            QObject::tr("How far to the first thing in the way, while a run is going. "
                        "Reads its full length when nothing is."));
    reading(QObject::tr("Hit"), QStringLiteral("hit"), PropertyFieldType::Boolean,
            QObject::tr("Whether anything is within reach."));
    reading(QObject::tr("Hit X"), QStringLiteral("hitX"), PropertyFieldType::Numeric,
            QObject::tr("Where it struck."));
    reading(QObject::tr("Hit Y"), QStringLiteral("hitY"), PropertyFieldType::Numeric,
            QObject::tr("Where it struck."));

    return result;
}

std::vector<PropertyRow> RayPropertyPane::defaultRows(EditorMode mode) const
{
    Q_UNUSED(mode);
    return {};
}
