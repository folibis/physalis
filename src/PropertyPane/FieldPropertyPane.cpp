#include "FieldPropertyPane.h"
#include "../CanvasScene.h"
#include "CatalogueRows.h"
#include "EngineRegistry.h"

std::vector<PropertyRow> FieldPropertyPane::rows(EditorMode mode) const
{
    std::vector<PropertyRow> result;
    if (!m_scene)
        return result;

    CanvasScene *scene = m_scene;

    if (mode == EditorMode::Physics) {
        std::vector<PropertyRow> world =
            worldRows(&scene->world(),
                      [scene] { scene->notifyFieldPropertyChanged(); },
                      scene->simulationEngineName());

        world.push_back({QObject::tr("Solid Field Bounds"), PropertyFieldType::Boolean,
            [scene] { return scene->fieldBoundsSolid(); },
            [scene](const QVariant &v) { scene->setFieldBoundsSolid(v.toBool()); },
            -100000.0, 100000.0, {}, -1, 0.0, QObject::tr("World")});
        world.back().tooltip =
            QObject::tr("Walls the edges of the field, so nothing can leave it. Off,"
                        " whatever falls out keeps falling.");

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
    result.back().tooltip = QObject::tr("How wide the drawing area is. The field is centred on the origin,"
                                        " so this is half of it either side.");

    result.push_back({QObject::tr("Field Height"), PropertyFieldType::Numeric,
        [scene] { return scene->fieldHeight(); },
        [scene](const QVariant &v) { scene->setFieldSize(scene->fieldWidth(), qMax(100.0, v.toDouble())); },
        100.0, 1000000.0, {}});
    result.back().tooltip = QObject::tr("How tall the drawing area is. Nothing stops a shape being placed"
                                        " outside it.");

    result.push_back({QObject::tr("Background Color"), PropertyFieldType::Color,
        [scene] { return scene->backgroundColor(); },
        [scene](const QVariant &v) { scene->setBackgroundColor(v.value<QColor>()); },
        -100000.0, 100000.0, {}});
    result.back().tooltip = QObject::tr("What the field is painted with, behind everything else. Saved"
                                        " with the scene, and exports use it too.");

    result.push_back({QObject::tr("Show Grid"), PropertyFieldType::Boolean,
        [scene] { return scene->showGrid(); },
        [scene](const QVariant &v) { scene->setShowGrid(v.toBool()); },
        -100000.0, 100000.0, {}});
    result.back().tooltip = QObject::tr("Whether the grid is drawn. It is a drawing aid only -- snapping"
                                        " is set in Options.");

    result.push_back({QObject::tr("Grid Cell Size"), PropertyFieldType::Numeric,
        [scene] { return scene->gridCellSize(); },
        [scene](const QVariant &v) { scene->setGridCellSize(qMax(1.0, v.toDouble())); },
        1.0, 10000.0, {}});
    result.back().tooltip = QObject::tr("How far apart the grid lines are, in scene units.");

    result.push_back({QObject::tr("Grid Color"), PropertyFieldType::Color,
        [scene] { return scene->gridColor(); },
        [scene](const QVariant &v) { scene->setGridColor(v.value<QColor>()); },
        -100000.0, 100000.0, {}});
    result.back().tooltip = QObject::tr("What the grid lines are drawn in. Appearance only.");

    result.push_back({QObject::tr("Scale (%)"), PropertyFieldType::Numeric,
        [scene] { return scene->currentScale(); },
        [scene](const QVariant &v) { scene->setCurrentScale(v.toDouble()); },
        scene->scaleMin(), scene->scaleMax(), {}});
    result.back().tooltip = QObject::tr("How far the view is zoomed in. It changes what you see and"
                                        " nothing about the scene itself.");

    return result;
}

std::vector<PropertyRow> FieldPropertyPane::worldRows(physics::WorldDesc *world,
                                                      const std::function<void()> &changed,
                                                      const QString &engineName)
{
    std::vector<PropertyRow> result;
    const QString section = QObject::tr("World");

    // Blank until a run fills them, but always there: a row that only exists
    // while running cannot be added to the log before the run starts.
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

    // The scene's own scale, not a physics setting: the editor draws, measures
    // and exports with it, and hands it to whichever engine runs the scene.
    result.push_back({QObject::tr("Pixels per Meter"), PropertyFieldType::Numeric,
        [world] { return world->pixelsPerMeter; },
        [world, changed](const QVariant &v) { world->pixelsPerMeter = qMax(1.0, v.toDouble()); changed(); },
        1.0, 10000.0, {}, -1, 0.0, section});
    result.back().defaultValue = physics::WorldDesc().pixelsPerMeter;
    result.back().tooltip = QObject::tr("How many scene units make a metre. It sets the scale of"
                                        " everything: at a large value a drawn box weighs"
                                        " a few grams, and forces have to shrink to match.");

    // Everything else the world is -- gravity, the solver's tuning, whether
    // bodies may sleep -- is the engine's to describe. Which settings exist
    // and what they are called changes with the engine, and nothing here
    // knows one of them by name.
    if (auto engine = physics::EngineRegistry::create(engineName)) {
        const physics::PropertyList properties = engine->worldProperties();
        for (PropertyRow &row : liveRowsFromCatalogue(properties, section))
            result.push_back(std::move(row));
        for (PropertyRow &row : rowsFromCatalogue(properties, &world->params, changed, section))
            result.push_back(std::move(row));
    }

    return result;
}

std::vector<PropertyRow> FieldPropertyPane::defaultRows(EditorMode mode) const
{
    std::vector<PropertyRow> result;
    if (mode != EditorMode::Physics)
        return result;

    static physics::WorldDesc pristine;
    pristine.params.clear();
    for (PropertyRow &row : worldRows(&pristine, [] {},
                                      m_scene ? m_scene->simulationEngineName() : QString()))
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
