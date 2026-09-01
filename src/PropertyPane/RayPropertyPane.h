#pragma once

#include "PropertyPane.h"

class CanvasScene;

// A rangefinder: where it sits, where it looks, how far it can see -- and,
// while a run is going, what it found.
class RayPropertyPane : public PropertyPane
{
    Q_OBJECT

public:
    explicit RayPropertyPane(QObject *parent = nullptr) : PropertyPane(parent) {}

    std::vector<PropertyRow> rows(EditorMode mode) const override;
    std::vector<PropertyRow> defaultRows(EditorMode mode) const override;
    void attach(QObject *target) override;

private:
    CanvasScene *m_scene = nullptr;
};
