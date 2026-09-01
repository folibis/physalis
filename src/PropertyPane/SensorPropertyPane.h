#pragma once

#include "PropertyPane.h"

class CanvasScene;

// A sensor is an area that notices what enters it. Underneath it is still a
// shape on a body, because that is the only way Box2D gives it a position and
// lets it move -- but none of that vocabulary means anything to someone
// placing a trigger zone, so this pane shows only what applies to one.
class SensorPropertyPane : public PropertyPane
{
    Q_OBJECT

public:
    explicit SensorPropertyPane(QObject *parent = nullptr) : PropertyPane(parent) {}

    std::vector<PropertyRow> rows(EditorMode mode) const override;
    std::vector<PropertyRow> defaultRows(EditorMode mode) const override;
    void attach(QObject *target) override;

private:
    CanvasScene *m_scene = nullptr;
};
