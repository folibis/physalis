#include "SensorPropertyPane.h"

#include "CanvasScene.h"
#include "PhysicsBody.h"
#include "ShapeItem.h"
#include "CatalogueRows.h"
#include "EngineRegistry.h"

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
    isSensor.getter = [scene, shape] { return scene->isSensorShape(shape); };
    isSensor.setter = [scene, shape, changed, pane = const_cast<SensorPropertyPane *>(this)](
                          const QVariant &v) {
        // Only when it actually turns over. Rebuilding the pane builds this
        // box again and sets it to what it already is; asking for another
        // rebuild from that would never stop.
        if (v.toBool() == scene->isSensorShape(shape))
            return;
        scene->setSensorShape(shape, v.toBool());
        changed();
        // Turning it off makes this a solid shape again, which is the physics
        // pane's business rather than this one's. Queued: the rebuild deletes
        // the very checkbox whose signal is still on the stack.
        QMetaObject::invokeMethod(pane, [pane] { emit pane->paneKindChanged(); },
                                  Qt::QueuedConnection);
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

    // Everything else a shape has, as the engine describes it -- the groups it
    // belongs to and notices among them. The sensor switch itself is already
    // the first row, so it is not repeated here.
    if (auto engine = physics::EngineRegistry::create(scene->simulationEngineName())) {
        const QString sensorKey = scene->sensorPropertyKey();
        for (PropertyRow &row : rowsFromCatalogue(engine->shapeProperties(), &shape->part().params,
                                                  changed, section)) {
            if (row.key == sensorKey)
                continue;
            row.section = section;
            result.push_back(std::move(row));
        }
    }

    return result;
}

std::vector<PropertyRow> SensorPropertyPane::defaultRows(EditorMode mode) const
{
    Q_UNUSED(mode);
    return {};
}
