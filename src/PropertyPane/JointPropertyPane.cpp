#include "JointPropertyPane.h"

#include <QSet>
#include "../CanvasScene.h"
#include "../Joint.h"
#include "../PhysicsBody.h"
#include "EngineRegistry.h"

namespace {

const QString &jointSection()
{
    static const QString s = QObject::tr("Joint");
    return s;
}

PropertyFieldType fieldTypeFor(physics::ParamType type)
{
    switch (type) {
    case physics::ParamType::Bool:    return PropertyFieldType::Boolean;
    case physics::ParamType::Choice:  return PropertyFieldType::Choice;
    case physics::ParamType::Integer: return PropertyFieldType::Numeric;
    case physics::ParamType::Real:    return PropertyFieldType::Numeric;
    }
    return PropertyFieldType::Numeric;
}

} // namespace

physics::JointType JointPropertyPane::typeOf(Joint *joint) const
{
    if (!joint)
        return {};

    auto engine = physics::EngineRegistry::create(
        m_scene ? m_scene->simulationEngineName() : QString());
    if (!engine)
        return {};

    for (const physics::JointType &type : engine->jointTypes()) {
        if (type.id == joint->typeId())
            return type;
    }
    return {};
}

std::vector<PropertyRow> JointPropertyPane::rows(EditorMode mode) const
{
    std::vector<PropertyRow> result;
    if (!m_scene || mode != EditorMode::Physics)
        return result;

    Joint *joint = m_scene->selectedJoint();
    if (!joint)
        return result;

    const physics::JointType type = typeOf(joint);

    for (PropertyRow &row : identityRows(joint, type)) {
        row.group = jointSection();
        result.push_back(std::move(row));
    }
    for (PropertyRow &row : parameterRows(joint, type))
        result.push_back(std::move(row));
    for (PropertyRow &row : measuredRows(joint, type))
        result.push_back(std::move(row));

    return result;
}

// What the joint reports about itself once it is running -- an angle, a
// translation, the force it is carrying. There is nothing to set here: these
// come from the engine's own getters, and the row exists so they can be read
// and, more to the point, logged.
std::vector<PropertyRow> JointPropertyPane::measuredRows(Joint *joint,
                                                         const physics::JointType &type) const
{
    std::vector<PropertyRow> result;

    auto engine = physics::EngineRegistry::create(
        m_scene ? m_scene->simulationEngineName() : QString());
    if (!engine)
        return result;

    // Whatever the type already offers as a setting. A readable under the same
    // key is that setting read back -- a motor's speed is published both ways,
    // since a rule wants to ask for it -- and showing it again here put "Motor
    // Speed" on the joint twice, once under Motor and once under Measured.
    QSet<QString> settings;
    for (const physics::JointParam &param : type.params)
        settings.insert(param.key);

    // Blank until a run fills them, but always there: a row that only exists
    // while running cannot be added to the log before the run starts.
    const QString group = QObject::tr("Measured");
    for (const physics::JointParam &param : engine->jointReadables(type.id)) {
        // Anything the joint already offers as a setting is shown there; this
        // is only for what can be read and not written. A readable that merely
        // reports such a setting is the same row twice -- under its own key,
        // caught above; under another name, and the engine says so.
        if (param.liveSettable || param.mirrorsSetting || param.rulesOnly)
            continue;
        if (settings.contains(param.key))
            continue;

        PropertyRow row;
        row.label = param.label;
        row.key = param.key;
        row.type = fieldTypeFor(param.type);
        row.section = jointSection();
        row.group = group;
        row.minValue = param.minValue;
        row.maxValue = param.maxValue;
        row.choices = param.choices;
        row.decimals = param.decimals;
        row.step = param.step;
        row.tooltip = param.tooltip;
        row.readOnly = true;
        // Filled by the engine while a run is going, and blank outside one --
        // the same bargain the body's position and speed rows make.
        row.getter = [] { return QVariant(); };
        row.setter = [](const QVariant &) {};

        result.push_back(std::move(row));
    }

    return result;
}

std::vector<PropertyRow> JointPropertyPane::defaultRows(EditorMode mode) const
{
    std::vector<PropertyRow> result;
    if (!m_scene || mode != EditorMode::Physics)
        return result;

    Joint *joint = m_scene->selectedJoint();
    if (!joint)
        return result;

    const physics::JointType type = typeOf(joint);

    if (type.needsAxis) {
        const qreal value = type.defaultAxisDegrees;
        result.push_back({QObject::tr("Axis Angle (deg)"), PropertyFieldType::Numeric,
            [value] { return value; }, [](const QVariant &) {},
            -360.0, 360.0, {}, 1, 1.0, jointSection(), jointSection()});
        result.back().tooltip = QObject::tr("The direction the joint slides or springs"
                                            " along. 0 points right, 90 points down.");
    }

    for (const physics::JointParam &param : type.params) {
        const QVariant value = param.defaultValue;
        result.push_back({param.label, fieldTypeFor(param.type),
            [value] { return value; }, [](const QVariant &) {},
            param.minValue, param.maxValue, param.choices,
            param.decimals, param.step, jointSection(), param.section});
        result.back().tooltip = param.tooltip;
    }
    return result;
}

std::vector<PropertyRow> JointPropertyPane::identityRows(Joint *joint,
                                                         const physics::JointType &type) const
{
    std::vector<PropertyRow> result;
    const QString &section = jointSection();

    result.push_back({QObject::tr("Name"), PropertyFieldType::String,
        [joint] { return joint->name(); },
        [joint](const QVariant &v) { joint->setName(v.toString()); },
        -100000.0, 100000.0, {}, -1, 0.0, section});
    result.back().tooltip = QObject::tr("What rules call this joint. Names have to be unique:"
                                        " a rule finds its target by this and nothing else.");

    result.push_back({QObject::tr("Type"), PropertyFieldType::String,
        [joint, type] { return type.label.isEmpty() ? joint->typeId() : type.label; },
        [](const QVariant &) {},   // a joint's type is fixed once it is made
        -100000.0, 100000.0, {}, -1, 0.0, section});
    result.back().readOnly = true;
    result.back().tooltip = type.description.isEmpty()
        ? QObject::tr("What kind of joint this is. Fixed once it is made.")
        : type.description;

    result.push_back({QObject::tr("Body A"), PropertyFieldType::String,
        [joint] { return joint->bodyA() ? joint->bodyA()->name() : QObject::tr("(none)"); },
        [](const QVariant &) {},
        -100000.0, 100000.0, {}, -1, 0.0, section});
    result.back().readOnly = true;   // which bodies it joins is fixed when it is made
    result.back().tooltip = QObject::tr("The first of the two bodies the joint holds"
                                        " together. Anything measured along an axis is"
                                        " measured in this one's frame.");

    result.push_back({QObject::tr("Body B"), PropertyFieldType::String,
        [joint] { return joint->bodyB() ? joint->bodyB()->name() : QObject::tr("(none)"); },
        [](const QVariant &) {},
        -100000.0, 100000.0, {}, -1, 0.0, section});
    result.back().readOnly = true;   // which bodies it joins is fixed when it is made
    result.back().tooltip = QObject::tr("The second body. A motor drives this one relative"
                                        " to the first, so swapping them reverses it.");

    const auto anchorRow = [&result, joint, &section](const QString &label, Joint::End end,
                                                      bool horizontal) {
        result.push_back({label, PropertyFieldType::Numeric,
            [joint, end, horizontal] {
                const QPointF p = joint->anchorScenePos(end);
                return horizontal ? p.x() : p.y();
            },
            [joint, end, horizontal](const QVariant &v) {
                QPointF p = joint->anchorScenePos(end);
                if (horizontal)
                    p.setX(v.toDouble());
                else
                    p.setY(v.toDouble());
                joint->setAnchorScenePos(end, p);
            },
            -1000000.0, 1000000.0, {}, 1, 1.0, section});
        result.back().tooltip =
            QObject::tr("Where the joint holds, in scene coordinates. Each body has its"
                        " own anchor, and they are rarely the same place.");
    };

    if (type.anchorCount > 0) {
        anchorRow(QObject::tr("Anchor A X"), Joint::End::A, true);
        anchorRow(QObject::tr("Anchor A Y"), Joint::End::A, false);
    }
    if (type.anchorCount > 1) {
        anchorRow(QObject::tr("Anchor B X"), Joint::End::B, true);
        anchorRow(QObject::tr("Anchor B Y"), Joint::End::B, false);
    }

    if (type.needsAxis) {
        result.push_back({QObject::tr("Axis Angle (deg)"), PropertyFieldType::Numeric,
            [joint] {
                const QPointF axis = joint->axisScene();
                return qRadiansToDegrees(std::atan2(axis.y(), axis.x()));
            },
            [joint](const QVariant &v) {
                const qreal radians = qDegreesToRadians(v.toDouble());
                joint->setAxisScene(QPointF(std::cos(radians), std::sin(radians)));
            },
            -360.0, 360.0, {}, 1, 1.0, section});
        result.back().tooltip = QObject::tr("The direction the joint slides or springs"
                                            " along. 0 points right, 90 points down.");
    }

    result.push_back({QObject::tr("Bodies Collide"), PropertyFieldType::Boolean,
        [joint] { return joint->collideConnected(); },
        [joint](const QVariant &v) { joint->setCollideConnected(v.toBool()); },
        -100000.0, 100000.0, {}, -1, 0.0, section});
    result.back().tooltip = QObject::tr("While off, the two bodies pass through each other."
                                        " If one of them is scenery, the other falls"
                                        " straight through it.");

    return result;
}

std::vector<PropertyRow> JointPropertyPane::parameterRows(Joint *joint,
                                                          const physics::JointType &type) const
{
    std::vector<PropertyRow> result;

    for (const physics::JointParam &param : type.params) {
        const QString key = param.key;
        const QVariant fallback = param.defaultValue;

        PropertyRow row;
        row.label = param.label;
        row.key = key;
        row.type = fieldTypeFor(param.type);
        row.section = jointSection();
        row.group = param.section;
        row.minValue = param.minValue;
        row.maxValue = param.maxValue;
        row.choices = param.choices;
        row.decimals = param.decimals;
        row.step = param.step;
        row.tooltip = param.tooltip;

        row.getter = [joint, key, fallback] {
            const auto it = joint->params().constFind(key);
            return it == joint->params().constEnd() ? fallback : *it;
        };
        row.setter = [joint, key](const QVariant &value) {
            joint->params().insert(key, value);
            joint->notifyPropertyChanged();
        };

        result.push_back(std::move(row));
    }

    return result;
}

void JointPropertyPane::attach(QObject *target)
{
    if (m_scene)
        disconnect(m_scene, nullptr, this, nullptr);

    m_scene = qobject_cast<CanvasScene *>(target);

    if (m_scene) {
        connect(m_scene, &CanvasScene::selectedJointChanged, this, &PropertyPane::rowsChanged);
        connect(m_scene, &CanvasScene::jointsChanged, this, &PropertyPane::rowsChanged);
    }

    emit rowsChanged();
}
