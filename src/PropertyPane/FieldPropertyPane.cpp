#include "FieldPropertyPane.h"
#include "../CanvasScene.h"

std::vector<PropertyRow> FieldPropertyPane::rows(EditorMode mode) const
{
    std::vector<PropertyRow> result;
    if (!m_scene)
        return result;

    CanvasScene *scene = m_scene;

    if (mode == EditorMode::Physics) {
        std::vector<PropertyRow> world = worldRows(&scene->world(),
                                                   [scene] { scene->notifyFieldPropertyChanged(); });

        world.push_back({QObject::tr("Solid Field Bounds"), PropertyFieldType::Boolean,
            [scene] { return scene->fieldBoundsSolid(); },
            [scene](const QVariant &v) { scene->setFieldBoundsSolid(v.toBool()); },
            -100000.0, 100000.0, {}, -1, 0.0, QObject::tr("World")});

        for (PropertyRow &row : world)
            result.push_back(std::move(row));
        return result;
    }

    if (mode != EditorMode::Edit)
        return result;

    result.push_back({QObject::tr("Field Width"), PropertyFieldType::Numeric,
        [scene] { return scene->fieldWidth(); },
        [scene](const QVariant &v) { scene->setFieldSize(qMax(100.0, v.toDouble()), scene->fieldHeight()); },
        100.0, 1000000.0, {}});

    result.push_back({QObject::tr("Field Height"), PropertyFieldType::Numeric,
        [scene] { return scene->fieldHeight(); },
        [scene](const QVariant &v) { scene->setFieldSize(scene->fieldWidth(), qMax(100.0, v.toDouble())); },
        100.0, 1000000.0, {}});

    result.push_back({QObject::tr("Background Color"), PropertyFieldType::Color,
        [scene] { return scene->backgroundColor(); },
        [scene](const QVariant &v) { scene->setBackgroundColor(v.value<QColor>()); },
        -100000.0, 100000.0, {}});

    result.push_back({QObject::tr("Show Grid"), PropertyFieldType::Boolean,
        [scene] { return scene->showGrid(); },
        [scene](const QVariant &v) { scene->setShowGrid(v.toBool()); },
        -100000.0, 100000.0, {}});

    result.push_back({QObject::tr("Grid Cell Size"), PropertyFieldType::Numeric,
        [scene] { return scene->gridCellSize(); },
        [scene](const QVariant &v) { scene->setGridCellSize(qMax(1.0, v.toDouble())); },
        1.0, 10000.0, {}});

    result.push_back({QObject::tr("Grid Color"), PropertyFieldType::Color,
        [scene] { return scene->gridColor(); },
        [scene](const QVariant &v) { scene->setGridColor(v.value<QColor>()); },
        -100000.0, 100000.0, {}});

    result.push_back({QObject::tr("Scale (%)"), PropertyFieldType::Numeric,
        [scene] { return scene->currentScale(); },
        [scene](const QVariant &v) { scene->setCurrentScale(v.toDouble()); },
        scene->scaleMin(), scene->scaleMax(), {}});

    return result;
}

std::vector<PropertyRow> FieldPropertyPane::worldRows(physics::WorldDesc *world,
                                                      const std::function<void()> &changed)
{
    std::vector<PropertyRow> result;
    const QString section = QObject::tr("World");

    result.push_back({QObject::tr("Gravity X (m/s²)"), PropertyFieldType::Numeric,
        [world] { return world->gravity.x(); },
        [world, changed](const QVariant &v) { world->gravity.setX(v.toDouble()); changed(); },
        -1000.0, 1000.0, {}, 2, 0.1, section});

    result.push_back({QObject::tr("Gravity Y (m/s²)"), PropertyFieldType::Numeric,
        [world] { return world->gravity.y(); },
        [world, changed](const QVariant &v) { world->gravity.setY(v.toDouble()); changed(); },
        -1000.0, 1000.0, {}, 2, 0.1, section});

    // Filled by the run while it is going. Read-only, and loggable like any
    // other row -- right-click the name to watch one.
    for (const auto &live : { qMakePair(QObject::tr("Elapsed Time (s)"), QStringLiteral("time")),
                              qMakePair(QObject::tr("Frame"), QStringLiteral("frame")) }) {
        PropertyRow row;
        row.label = live.first;
        row.key = live.second;
        row.type = PropertyFieldType::Numeric;
        row.section = section;
        row.decimals = live.second == QLatin1String("frame") ? 0 : 2;
        row.minValue = 0.0;
        row.maxValue = 1e12;
        row.getter = [] { return QVariant(); };   // the engine answers while running
        row.setter = [](const QVariant &) {};
        row.readOnly = true;
        row.tooltip = QObject::tr("Counted from the moment the run starts.");
        result.push_back(std::move(row));
    }

    result.push_back({QObject::tr("Pixels per Meter"), PropertyFieldType::Numeric,
        [world] { return world->pixelsPerMeter; },
        [world, changed](const QVariant &v) { world->pixelsPerMeter = qMax(1.0, v.toDouble()); changed(); },
        1.0, 10000.0, {}, -1, 0.0, section});

    // The solver's tuning. Its defaults are Box2D's own, and the reset arrow
    // on each row puts them back.
    const QString solver = QObject::tr("Solver");
    static const physics::WorldDesc factory;

    const auto number = [&](const QString &label, qreal physics::WorldDesc::*field,
                            qreal lo, qreal hi, int decimals, qreal step,
                            const QString &tip) {
        PropertyRow row;
        row.label = label;
        row.type = PropertyFieldType::Numeric;
        row.getter = [world, field] { return world->*field; };
        row.setter = [world, changed, field](const QVariant &v) {
            world->*field = v.toDouble();
            changed();
        };
        row.minValue = lo;
        row.maxValue = hi;
        row.decimals = decimals;
        row.step = step;
        row.section = solver;
        row.defaultValue = factory.*field;
        row.tooltip = tip;
        result.push_back(std::move(row));
    };

    const auto flag = [&](const QString &label, bool physics::WorldDesc::*field,
                          const QString &tip) {
        PropertyRow row;
        row.label = label;
        row.type = PropertyFieldType::Boolean;
        row.getter = [world, field] { return world->*field; };
        row.setter = [world, changed, field](const QVariant &v) {
            world->*field = v.toBool();
            changed();
        };
        row.section = solver;
        row.defaultValue = factory.*field;
        row.tooltip = tip;
        result.push_back(std::move(row));
    };

    number(QObject::tr("Restitution Threshold (m/s)"),
           &physics::WorldDesc::restitutionThreshold, 0.0, 100.0, 2, 0.1,
           QObject::tr("Below this closing speed restitution is ignored, so a "
                       "bouncy shape moving slower than this will not bounce "
                       "at all."));
    number(QObject::tr("Hit Event Threshold (m/s)"),
           &physics::WorldDesc::hitEventThreshold, 0.0, 100.0, 2, 0.1,
           QObject::tr("How hard an impact must be before a shape with Hit "
                       "Events raises one."));
    number(QObject::tr("Contact Stiffness (Hz)"),
           &physics::WorldDesc::contactHertz, 1.0, 240.0, 1, 1.0,
           QObject::tr("How quickly overlapping shapes are pushed apart. "
                       "Higher recovers faster but can jitter."));
    number(QObject::tr("Contact Damping"),
           &physics::WorldDesc::contactDampingRatio, 0.0, 100.0, 1, 0.5,
           QObject::tr("Damping on that recovery. Lower resolves overlap more "
                       "energetically."));
    number(QObject::tr("Max Push Speed (m/s)"),
           &physics::WorldDesc::maxContactPushSpeed, 0.0, 100.0, 2, 0.5,
           QObject::tr("A cap on how fast overlap recovery may push, whatever "
                       "the stiffness asks for."));
    number(QObject::tr("Max Speed (m/s)"),
           &physics::WorldDesc::maximumLinearSpeed, 1.0, 10000.0, 0, 10.0,
           QObject::tr("Nothing in the world may move faster than this."));

    flag(QObject::tr("Allow Sleeping"), &physics::WorldDesc::enableSleep,
         QObject::tr("Lets settled bodies stop being simulated. Turning this "
                     "off costs speed but keeps everything responsive."));
    flag(QObject::tr("Continuous Collision"), &physics::WorldDesc::enableContinuous,
         QObject::tr("Stops fast bodies tunnelling through thin ones. Off is "
                     "cheaper but things can pass through walls."));

    return result;
}

std::vector<PropertyRow> FieldPropertyPane::defaultRows(EditorMode mode) const
{
    std::vector<PropertyRow> result;
    if (mode != EditorMode::Physics)
        return result;

    static physics::WorldDesc pristine;
    for (PropertyRow &row : worldRows(&pristine, [] {}))
        result.push_back(std::move(row));

    result.push_back({QObject::tr("Solid Field Bounds"), PropertyFieldType::Boolean,
        [] { return false; }, [](const QVariant &) {},
        -100000.0, 100000.0, {}, -1, 0.0, QObject::tr("World")});

    return result;
}

void FieldPropertyPane::attach(QObject *target)
{
    if (m_scene)
        disconnect(m_scene, nullptr, this, nullptr);

    m_scene = qobject_cast<CanvasScene *>(target);

    if (m_scene)
        connect(m_scene, &CanvasScene::fieldPropertyChanged, this, &PropertyPane::valueChanged);

    emit rowsChanged();
}
