#include "PhysicsPropertyPane.h"

#include "CatalogueRows.h"
#include "EngineRegistry.h"
#include "../CanvasScene.h"
#include "../PhysicsBody.h"
#include "../ShapeItem.h"

// The physics table: what a body and its shapes are, while one is selected.
//
// Almost none of it is written here. The engine says what a body and a shape
// have -- the names, the ranges, the defaults, what each one means -- and this
// turns that into rows and keeps the values by key. What is written here is
// the handful of things the editor owns rather than the engine: what a thing
// is called, which body a shape belongs to, whether the body takes part in the
// run at all, and which of the three kinds of body it is.

namespace {

const QString &bodySection()
{
    static const QString s = QObject::tr("Body");
    return s;
}

const QString &shapeSection()
{
    static const QString s = QObject::tr("Shape");
    return s;
}

} // namespace

std::vector<PropertyRow> PhysicsPropertyPane::rows(EditorMode mode) const
{
    std::vector<PropertyRow> result;
    if (!m_scene || mode == EditorMode::Edit)
        return result;

    const QVector<ShapeItem *> &selection = m_scene->physicsSelection();
    if (selection.isEmpty())
        return result;

    const auto append = [&result](std::vector<PropertyRow> rows) {
        for (PropertyRow &row : rows)
            result.push_back(std::move(row));
    };

    auto engine = physics::EngineRegistry::create(m_scene->simulationEngineName());
    const bool running = m_scene->simulationRunning();

    if (PhysicsBody *body = m_scene->commonSelectedBody()) {
        const auto notify = [body] { body->notifyPropertyChanged(); };
        result.push_back(bodyTypeRow(&body->props(), notify));
        append(bodyIdentityRows(body));
        result.push_back(enabledRow(&body->props(), notify));
        if (engine) {
            const physics::PropertyList properties = engine->bodyProperties();
            if (running)
                append(liveRowsFromCatalogue(properties, bodySection()));
            append(rowsFromCatalogue(properties, &body->props().params, notify, bodySection()));
        }
    }

    ShapeItem *shape = selection.first();
    append(shapeIdentityRows(shape));
    if (engine) {
        // A shape things pass through has a pane of its own, so a change to
        // whichever property the engine tagged for that has to be noticed.
        // Queued: the rebuild deletes the very control whose signal is running.
        const bool wasSensor = m_scene->isSensorShape(shape);
        auto *pane = const_cast<PhysicsPropertyPane *>(this);
        const auto notify = [this, pane, shape, wasSensor] {
            shape->notifyPropertyChanged();
            if (m_scene->isSensorShape(shape) != wasSensor) {
                QMetaObject::invokeMethod(pane, [pane] { emit pane->paneKindChanged(); },
                                          Qt::QueuedConnection);
            }
        };
        const physics::PropertyList properties = engine->shapeProperties();
        if (running)
            append(liveRowsFromCatalogue(properties, shapeSection()));
        append(rowsFromCatalogue(properties, &shape->part().params, notify, shapeSection()));
    }
    return result;
}

std::vector<PropertyRow> PhysicsPropertyPane::defaultRows(EditorMode mode) const
{
    std::vector<PropertyRow> result;
    if (mode == EditorMode::Edit || !m_scene)
        return result;

    // What an untouched body and shape look like, for the reset arrow on each
    // row. The values come from the same catalogue the rows do, so there is
    // nothing here to keep in step with it.
    static physics::BodyDesc pristineBody;
    static physics::ShapePart pristinePart;
    pristineBody.params.clear();
    pristinePart.params.clear();

    result.push_back(bodyTypeRow(&pristineBody, [] {}));
    result.push_back(enabledRow(&pristineBody, [] {}));
    if (auto engine = physics::EngineRegistry::create(m_scene->simulationEngineName())) {
        for (PropertyRow &row : rowsFromCatalogue(engine->bodyProperties(),
                                                  &pristineBody.params, [] {}, bodySection()))
            result.push_back(std::move(row));
        for (PropertyRow &row : rowsFromCatalogue(engine->shapeProperties(),
                                                  &pristinePart.params, [] {}, shapeSection()))
            result.push_back(std::move(row));
    }
    return result;
}

PropertyRow PhysicsPropertyPane::bodyTypeRow(physics::BodyDesc *props,
                                             const std::function<void()> &changed)
{
    // One of three, and the editor's own: it groups shapes into bodies, draws
    // each kind in its own colour, and hands the engine what it built.
    static const physics::BodyType kBodyTypes[] = {
        physics::BodyType::Static, physics::BodyType::Kinematic, physics::BodyType::Dynamic
    };
    PropertyRow row {QObject::tr("Type"), PropertyFieldType::Choice,
        [props] {
            for (int i = 0; i < 3; ++i) {
                if (kBodyTypes[i] == props->type)
                    return i;
            }
            return 2;
        },
        [props, changed](const QVariant &v) {
            props->type = kBodyTypes[qBound(0, v.toInt(), 2)];
            changed();
        },
        -100000.0, 100000.0,
        {QObject::tr("Static"), QObject::tr("Kinematic"), QObject::tr("Dynamic")}, -1, 0.0,
        bodySection()};
    row.tooltip = QObject::tr("Scenery never moves, a kinematic body moves only as it is told,"
                              " and a dynamic one is fully simulated.");
    return row;
}

PropertyRow PhysicsPropertyPane::enabledRow(physics::BodyDesc *props,
                                            const std::function<void()> &changed)
{
    PropertyRow row {QObject::tr("Enabled"), PropertyFieldType::Boolean,
        [props] { return props->isEnabled; },
        [props, changed](const QVariant &v) { props->isEnabled = v.toBool(); changed(); },
        -100000.0, 100000.0, {}, -1, 0.0, bodySection()};
    row.defaultValue = true;
    row.tooltip = QObject::tr("A disabled body is not handed to the engine at all: it stays on"
                              " the canvas and takes no part in the run.");
    return row;
}

std::vector<PropertyRow> PhysicsPropertyPane::bodyIdentityRows(PhysicsBody *body)
{
    std::vector<PropertyRow> result;
    const QString &section = bodySection();

    PropertyRow name {QObject::tr("Name"), PropertyFieldType::String,
        [body] { return body->name(); },
        [body](const QVariant &v) { body->setName(v.toString()); },
        -100000.0, 100000.0, {}, -1, 0.0, section};
    name.tooltip = QObject::tr("What rules call this. Names have to be unique: a rule finds its"
                               " subject by this and nothing else.");
    result.push_back(std::move(name));

    PropertyRow shapes {QObject::tr("Shapes"), PropertyFieldType::String,
        [body] { return QString::number(body->shapes().size()); },
        [](const QVariant &) {},   // membership is changed on the canvas
        -100000.0, 100000.0, {}, -1, 0.0, section};
    shapes.readOnly = true;
    shapes.tooltip = QObject::tr("How many shapes make up this body. They move as one.");
    result.push_back(std::move(shapes));

    return result;
}

std::vector<PropertyRow> PhysicsPropertyPane::shapeIdentityRows(ShapeItem *shape)
{
    std::vector<PropertyRow> result;
    const QString &section = shapeSection();

    PropertyRow name {QObject::tr("Name"), PropertyFieldType::String,
        [shape] { return shape->name(); },
        [shape](const QVariant &v) { shape->setName(v.toString()); },
        -100000.0, 100000.0, {}, -1, 0.0, section};
    name.tooltip = QObject::tr("What rules call this shape.");
    result.push_back(std::move(name));

    PropertyRow body {QObject::tr("Body"), PropertyFieldType::String,
        [shape] { return shape->body() ? shape->body()->name() : QObject::tr("(none)"); },
        [](const QVariant &) {},
        -100000.0, 100000.0, {}, -1, 0.0, section};
    body.readOnly = true;   // grouping is done on the canvas, not by typing a name
    body.tooltip = QObject::tr("Which body this shape belongs to. Shapes are grouped into bodies"
                               " on the canvas, not by typing a name here.");
    result.push_back(std::move(body));

    return result;
}

void PhysicsPropertyPane::attach(QObject *target)
{
    if (m_scene)
        disconnect(m_scene, nullptr, this, nullptr);

    m_scene = qobject_cast<CanvasScene *>(target);

    if (m_scene) {
        connect(m_scene, &CanvasScene::physicsSelectionChanged, this, &PropertyPane::rowsChanged);
        connect(m_scene, &CanvasScene::bodiesChanged, this, &PropertyPane::rowsChanged);
    }

    emit rowsChanged();
}
