#pragma once

#include "PropertyPane.h"
#include "JointTypes.h"

class CanvasScene;
class Joint;

class JointPropertyPane : public PropertyPane
{
public:
    explicit JointPropertyPane(QObject *parent = nullptr) : PropertyPane(parent) {}

    std::vector<PropertyRow> rows(EditorMode mode) const override;
    std::vector<PropertyRow> defaultRows(EditorMode mode) const override;
    void attach(QObject *target) override;

private:
    std::vector<PropertyRow> identityRows(Joint *joint, const physics::JointType &type) const;
    std::vector<PropertyRow> parameterRows(Joint *joint, const physics::JointType &type) const;

    physics::JointType typeOf(Joint *joint) const;

    CanvasScene *m_scene = nullptr;
};
