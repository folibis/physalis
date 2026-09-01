#include "SensorPropertyPane.h"

#include "CanvasScene.h"
#include "PhysicsBody.h"
#include "ShapeItem.h"

void SensorPropertyPane::attach(QObject *target)
{
    m_scene = qobject_cast<CanvasScene *>(target);
}

std::vector<PropertyRow> SensorPropertyPane::rows(EditorMode mode) const
{
    Q_UNUSED(mode);

    std::vector<PropertyRow> result;
    if (!m_scene || m_scene->physicsSelection().isEmpty())
        return result;

    ShapeItem *shape = m_scene->physicsSelection().first();
    PhysicsBody *body = shape->body();
    if (!body)
        return result;

    physics::ShapePart *part = &shape->part();
    physics::BodyDesc *props = &body->props();
    CanvasScene *scene = m_scene;
    const auto changed = [body] { body->notifyPropertyChanged(); };
    const QString section = QObject::tr("Sensor");

    PropertyRow isSensor;
    isSensor.label = QObject::tr("Sensor");
    isSensor.type = PropertyFieldType::Boolean;
    isSensor.getter = [part] { return part->isSensor; };
    isSensor.setter = [part, changed](const QVariant &v) {
        part->isSensor = v.toBool();
        changed();
    };
    isSensor.section = section;
    isSensor.tooltip = QObject::tr(
        "An area that notices what enters it instead of colliding. Turn this "
        "off to make it a solid shape again.");
    result.push_back(std::move(isSensor));

    PropertyRow name;
    name.label = QObject::tr("Name");
    name.type = PropertyFieldType::String;
    name.getter = [shape] { return shape->name(); };
    name.setter = [shape, scene](const QVariant &v) {
        shape->setName(scene->uniqueName(v.toString(), shape));
    };
    name.section = section;
    result.push_back(std::move(name));

    PropertyRow enabled;
    enabled.label = QObject::tr("Enabled");
    enabled.type = PropertyFieldType::Boolean;
    enabled.getter = [props] { return props->isEnabled; };
    enabled.setter = [props, changed](const QVariant &v) {
        props->isEnabled = v.toBool();
        changed();
    };
    enabled.section = section;
    enabled.key = QStringLiteral("isEnabled");
    enabled.tooltip = QObject::tr("A disabled sensor notices nothing.");
    result.push_back(std::move(enabled));

    // The one body-ish thing that matters here: whether the area stays put or
    // is carried around. A falling trigger zone is almost never wanted.
    PropertyRow movement;
    movement.label = QObject::tr("Movement");
    movement.type = PropertyFieldType::Choice;
    movement.choices = { QObject::tr("Fixed in place"),
                         QObject::tr("Moved by rules"),
                         QObject::tr("Falls and is pushed") };
    movement.getter = [props] { return int(props->type); };
    movement.setter = [props, changed](const QVariant &v) {
        props->type = static_cast<physics::BodyType>(v.toInt());
        changed();
    };
    movement.section = section;
    movement.tooltip = QObject::tr(
        "Fixed is the usual choice. The last one obeys gravity, so the area "
        "falls out of the scene as soon as a run starts.");
    result.push_back(std::move(movement));

    // What it can notice. The same filtering every shape has, named for what
    // it does here rather than for the bits behind it.
    const auto bits = [&](const QString &label, quint64 physics::Filter::*field,
                          const QString &tip) {
        PropertyRow row;
        row.label = label;
        row.type = PropertyFieldType::String;
        row.getter = [part, field] {
            return QStringLiteral("0x%1").arg(part->filter.*field, 16, 16, QLatin1Char('0'));
        };
        row.setter = [part, field, changed](const QVariant &v) {
            bool ok = false;
            const quint64 value = v.toString().trimmed().toULongLong(&ok, 0);
            if (!ok)
                return;
            part->filter.*field = value;
            changed();
        };
        row.section = section;
        row.tooltip = tip;
        result.push_back(std::move(row));
    };

    bits(QObject::tr("Is In Group"), &physics::Filter::categoryBits,
         QObject::tr("Which group this area belongs to."));
    bits(QObject::tr("Notices Groups"), &physics::Filter::maskBits,
         QObject::tr("Which groups it can notice. Whatever enters must also "
                     "have Sensor Events turned on before it is reported."));

    return result;
}

std::vector<PropertyRow> SensorPropertyPane::defaultRows(EditorMode mode) const
{
    Q_UNUSED(mode);
    return {};
}
