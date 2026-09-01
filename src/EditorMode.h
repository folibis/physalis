#pragma once

#include <QColor>
#include <QString>

enum class EditorMode {
    Edit,     // draw and shape geometry: size, position, nodes, appearance
    Physics   // rigid-body behavior: bodies, shapes, materials and joints
};

namespace EditorModes {

inline constexpr EditorMode kAll[] = { EditorMode::Edit, EditorMode::Physics };

inline QString name(EditorMode mode)
{
    switch (mode) {
    case EditorMode::Edit:    return QStringLiteral("Edit");
    case EditorMode::Physics: return QStringLiteral("Physics");
    }
    return {};
}

inline QColor accent(EditorMode mode)
{
    switch (mode) {
    // The two colours the app icon is drawn in.
    case EditorMode::Edit:    return QColor(0x05, 0xC9, 0x36); // leaf green
    case EditorMode::Physics: return QColor(0xFE, 0x54, 0x0F); // lantern orange
    }
    return QColor(0x60, 0x60, 0x60);
}

inline QString tooltip(EditorMode mode)
{
    switch (mode) {
    case EditorMode::Edit:
        return QStringLiteral("Edit mode -- draw shapes and change their geometry and appearance");
    case EditorMode::Physics:
        return QStringLiteral("Physics mode -- group shapes into bodies, connect them with joints,"
                              " and set their physical properties");
    }
    return {};
}

} // namespace EditorModes
