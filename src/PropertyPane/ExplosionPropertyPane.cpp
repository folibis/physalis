#include "ExplosionPropertyPane.h"

#include "CanvasScene.h"
#include "ExplosionItem.h"
#include "EngineRegistry.h"

void ExplosionPropertyPane::attach(QObject *target)
{
    m_scene = qobject_cast<CanvasScene *>(target);
}

std::vector<PropertyRow> ExplosionPropertyPane::rows(EditorMode mode) const
{
    Q_UNUSED(mode);

    std::vector<PropertyRow> result;
    ExplosionItem *explosion = m_scene ? m_scene->selectedExplosion() : nullptr;
    if (!explosion)
        return result;

    const QString section = QObject::tr("Explosion");
    CanvasScene *scene = m_scene;

    PropertyRow name;
    name.label = QObject::tr("Name");
    name.type = PropertyFieldType::String;
    name.getter = [explosion] { return explosion->name(); };
    name.setter = [explosion, scene](const QVariant &v) {
        explosion->setName(scene->uniqueName(v.toString(), explosion));
    };
    name.section = section;
    result.push_back(std::move(name));

    const auto coordinate = [&](const QString &label, bool horizontal) {
        PropertyRow row;
        row.label = label;
        row.type = PropertyFieldType::Numeric;
        row.getter = [explosion, horizontal] {
            return horizontal ? explosion->pos().x() : explosion->pos().y();
        };
        row.setter = [explosion, horizontal](const QVariant &v) {
            QPointF p = explosion->pos();
            if (horizontal)
                p.setX(v.toDouble());
            else
                p.setY(v.toDouble());
            explosion->setPos(p);
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

    // The blast's own settings, described by the engine rather than named
    // here -- the app has no business knowing what an explosion needs.
    auto engine = physics::EngineRegistry::create(m_scene->simulationEngineName());
    if (engine) {
        for (const physics::ActionType &action : engine->bodyActions()) {
            if (action.id != QLatin1String("explode"))
                continue;
            for (const physics::JointParam &param : action.params) {
                PropertyRow row;
                row.label = param.label;
                row.type = PropertyFieldType::Numeric;
                const QString key = param.key;
                const QVariant fallback = param.defaultValue;
                row.getter = [explosion, key, fallback] {
                    return explosion->params().value(key, fallback);
                };
                row.setter = [explosion, key](const QVariant &v) {
                    explosion->setParam(key, v.toDouble());
                };
                row.minValue = param.minValue;
                row.maxValue = param.maxValue;
                row.decimals = param.decimals;
                row.step = param.step;
                row.section = section;
                row.defaultValue = param.defaultValue;
                row.tooltip = param.tooltip;
                result.push_back(std::move(row));
            }
        }
    }

    return result;
}

std::vector<PropertyRow> ExplosionPropertyPane::defaultRows(EditorMode mode) const
{
    Q_UNUSED(mode);
    return {};
}
